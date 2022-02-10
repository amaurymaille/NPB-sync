#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <atomic>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <type_traits>

template<typename T>
class Ringbuffer {
    public:
        Ringbuffer(size_t size) {
            init(size);
        }

        ~Ringbuffer() {
            free(_data);
        }

        inline bool empty() const __attribute__((always_inline)) {
            return _tail == _head;
        }

        inline bool full() const __attribute__((always_inline)) {
            return _head == (_tail - 1 + _size) % _size;
        }

        inline std::optional<T> pop() __attribute__((always_inline)) {
            std::optional<T> result;
            if (empty()) {
                return std::nullopt;
            } else {
                result = _data[_tail];
                ++_tail;
                if (_tail >= _size) {
                    _tail = 0;
                }
            }

            --_n_elements;
            return result;
        }

        inline int push(T const& data) __attribute__((always_inline)) {
            if (full()) {
                return -1;
            }

            _data[_head] = data;
            _head++;
            if (_head == _size) {
                _head = 0;
            }

            ++_n_elements;
            return 0;
        }

        size_t n_elements() const {
            return _n_elements;
        }

        void reinit(size_t size) {
            free(_data);
            init(size);
        }

        size_t size() const {
            return _size;
        }
    private:
        T* _data;
        size_t _n_elements;
        size_t _size;
        int _head, _tail;

        void init(size_t size) {
            _data = static_cast<T*>(malloc(sizeof(T) * (size + 1)));
            _size = size + 1;
            _head = _tail = _n_elements = 0;

            if (_data == nullptr) {
                throw std::runtime_error("Not enough memory");
            }

        }
};

template<typename T>
class NaiveQueue {
    public:
        NaiveQueue(size_t size, int n_producers) : _buf(size) {
            _n_producers = n_producers;
            _n_terminated = 0;
        }

        ~NaiveQueue() {

        }

        void terminate() {
            std::unique_lock<std::mutex> lck(_mutex);
            ++_n_terminated; 

            if (terminated()) {
                _not_empty.notify_all();
            }
        }

        inline int dequeue(Ringbuffer<T>* buf, int limit) __attribute__((always_inline)){
            std::unique_lock<std::mutex> lck(_mutex);
            while (_buf.empty() && !terminated()) {
                _not_empty.wait(lck);
            }

            if (_buf.empty() && terminated()) {
                return -1;
            }

            int i = 0;
            for (; i < limit && !_buf.empty() && !buf->full(); ++i) {
                std::optional<T> temp = _buf.pop();
                buf->push(*temp);
            }

            if (i > 0) {
                _not_full.notify_all();
            }

            return i;
        }

        inline int enqueue(Ringbuffer<T>* buf, int limit) __attribute__ ((always_inline)) {
            std::unique_lock<std::mutex> lck(_mutex);
            while (_buf.full()) {
                _not_full.wait(lck);
            }

            int i = 0;
            for (; i < limit && !_buf.full() && !buf->empty(); ++i) {
                std::optional<T> temp = buf->pop();
                _buf.push(*temp);
            }

            if (i > 0) {
                _not_empty.notify_all();
            }

            return i;
        }

        unsigned int size() {
            return _buf._n_elements();
        }

    private:
        Ringbuffer<T> _buf;
        int _n_producers;
        int _n_terminated;
        std::mutex _mutex;
        std::condition_variable _not_empty, _not_full;


        bool terminated() const {
            return _n_producers == _n_terminated;
        }
};

template<typename T>
class NaiveQueueMaster;

template<typename T>
class NaiveQueueImpl {
    public:
        NaiveQueueImpl(NaiveQueueMaster<T>* master, size_t size, bool reconfigure, 
                unsigned int threshold, unsigned int new_step) : 
            _master(master), _reconfigure(reconfigure), 
            _threshold(threshold), _new_step(new_step) {
            _need_reconfigure.store(false, std::memory_order_relaxed);
            init(size, new_step);
        }

        ~NaiveQueueImpl() {
            free(_data);
        }

        inline bool empty() const __attribute__((always_inline)) {
            return _tail == _head;
        }

        inline bool full() const __attribute__((always_inline)) {
            return _head == (_tail - 1 + _size) % _size;
        }

        inline std::optional<T> pop() __attribute__((always_inline));

        inline std::optional<T> pop_local() __attribute__((always_inline)) {
            if (empty()) {
                // std::cout << "[Pop local] Empty" << std::endl;
                return std::nullopt;
            }

            std::optional<T> result = _data[_tail];
            ++_tail;
            if (_tail >= _size) {
                _tail = 0;
            }

            --_n_elements;
            return result;

        }

        inline void push(T const& data) __attribute__((always_inline));

        inline bool push_local(T const& data) __attribute__((always_inline)) {
            if (full()) {
                // std::cout << "[Push local] Full" << std::endl;
                return false;
            }

            _data[_head] = data;
            _head++;
            if (_head == _size) {
                _head = 0;
            }

            ++_n_elements;
            return true;
        }

        size_t n_elements() const {
            return _n_elements;
        }

        void reinit(size_t size) {
            free(_data);
            init(size);
        }

        bool resize(size_t size) {
            // Stupid corner case.
            if (size == _size - 1) {
                return true;
            }

            bool reduce = size < _size - 1;

            // If the buffer gets reduced, we need to make sure that the resulting buffer
            // still holds all elements. If reducing the buffer would result in a loss of 
            // elements, abort the resize and notify the push / pop operations so they can 
            // take the necessary action (push will forcibly enqueue, pop will leave the 
            // local buffer unchanged until space is available).
            //
            // Note that we use _size - 1 because the size passed as parameter to resize is
            // the desired size - 1 because fuck the guy who wrote the original ringbuffer in
            // dedup with its stupid -1 rule.
            if (size < _size - 1 && n_elements() >= size) {
                // printf("Reducing (size = %d, _size = %d) and n_elements = %d\n", size, _size, n_elements());
                return false;
            }

            // printf("Resizing %p with new_size = %d (received %d) (old size = %d)\n", this, _new_step, size, _size);
            // printf("Resize pre %p: _head = %d, _tail = %d\n", this, _head, _tail);

            // Empty. Both _head and _tail get reset to 0.
            if (_head == _tail) {
                _head = _tail = 0;
                ;
            } else if (_head > _tail) {
                // If the buffer becomes bigger nothing changes because the memory range
                // used in the "new" buffer contains the entirety of the current range.
                // If the buffer becomes smaller, then we need to shift the elements
                // to the "left" (i.e. toward lower memory addresses) if the resulting 
                // memory range wouldn't contain the current range.
                //
                // Increase from 10 to 15 :
                // H is _head, T is _tail. Letters are elements.
                //
                // 0 1 2 3 4 5 6 7 8 9
                //     a b c d e 
                //     T         H
                //
                // ------------------->
                //
                // 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14
                //     a b c d e 
                //     T         H
                //
                //
                // Decrease from 10 to 8, _head <= 7 :
                // H is _head, T is _tail. Letters are elements.
                //
                // 0 1 2 3 4 5 6 7 8 9
                //     a b c d 
                //     T       H
                //
                // ------------------->
                //
                // 0 1 2 3 4 5 6 7
                //     a b c d
                //     T       H
                //
                //
                // Decrease from 10 to 8, _head is > 7
                // 0 1 2 3 4 5 6 7 8 9
                //         a b c d e 
                //         T         H
                //
                // ------------------->
                //
                // 0 1 2 3 4 5 6 7
                // a b c d e 
                // T         H
                //
                // We just shift to the beginning of the array.

                if (reduce && _head > size) {
                    // printf("Resize: reducing and _head (%d) is greater than size (%d)\n", _head, size);
                    // Move to the base of the array as it wouldn't change much to shift
                    // in such a way that _head is now at the extremity of the array.
                    // memmove will induce a small overhead because of the intermediate 
                    // buffer it uses.
                    memmove(_data, _data + _tail, sizeof(T) * n_elements());
                    _head = n_elements();
                    _tail = 0;
                }
            } else {
                // If the buffer becomes bigger we need to avoid having an empty space 
                // after the current _tail. Shift the elements that starts from _tail 
                // in such a way that they are at the end of the new buffer (simpler than
                // moving the beginning to fill the new space and then moving what remains 
                // of the beginning to fill the freed space).
                //
                // If the buffer becomes smaller, we need to shift the "tail" in such a way
                // that it ends at the end of the new buffer.

                if (!reduce) {
                    // printf("Resize: increasing and _head (%d) before _tail (%d)\n", _head, _tail);
                    // Assume the buffer can hold 10 elements, and we resize so that it can store
                    // 15 elements. T is _tail, H is _head. Letters represent elements.
                    // 
                    // 0 1 2 3 4 5 6 7 8 9
                    // a         f g h i j 
                    //   H       T
                    //
                    // --------------->
                    //
                    // 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14
                    // a                    f  g  h  i  j
                    //   H                  T
                    //
                    // We move 5 elements = _size (10) - _tail (5).
                    // Elements are shifted 5 positions = size (14) + 1 (15) - _size (10) to the right.
                    // Don't forget that to reach a buffer of 15 elements, you need to pass 14 as 
                    // parameter to resize. 
                    memmove(_data + _tail + size + 1 - _size, _data + _tail, sizeof(T) * (_size - _tail));
                    _tail += (size + 1 - _size);
                } else {
                    // printf("Resize: reducing and _head (%d) before _tail (%d)\n", _head, _tail);
                    // Assume the buffer can hold 10 elements, and we resize so that it can 
                    // store 8 elements. T is _tail, H is _head. Letters represent elements.
                    //
                    // 0 1 2 3 4 5 6 7 8 9
                    // a         f g h i j
                    //   H       T
                    //
                    // --------------->
                    //
                    // 0 1 2 3 4 5 6 7
                    // a     f g h i j
                    //   H   T
                    //
                    // We move 5 elements = _size (10) - _tail (5).
                    // Elements are shifted 2 positions = _size (10) - (size (7) + 1) (8) to the left.
                    // Don't forget that to reach a buffer of 8 elements, you need to pass 7 as 
                    // parameter to resize.
                    memmove(_data + _tail - (_size - (size + 1)), _data + _tail, sizeof(T) * (_size - _tail));
                    _tail -= (_size - (size + 1));
                }
            }

            _size = size + 1;

            // printf("Resize post %p: _head = %d, _tail = %d\n", this, _head, _tail);
            // dump();
            // printf("DUMP DONE\n");
            
            return true;
        }

        void terminate() {
            while (!empty()) {
                _master->enqueue(this, _size - 1);
            }
            _master->terminate();
        }

        void dump() {
            for (int i = _tail; i != _head; ) {
                std::cout << i << " => " << _data[i] << std::endl;
                ++i;
                if (i == _size) {
                    i = 0;
                }
            }
            std::cout << "_head = " << _head << ", _tail = " << _tail << std::endl;
        }

        void prepare_reconfigure(size_t size) {
            _new_step = size;
            _need_reconfigure.store(true, std::memory_order_release);
        }

        size_t get_step() const {
            return _size;
        }

    private:
        T* _data;
        size_t _n_elements;
        size_t _size;
        int _head, _tail;
        NaiveQueueMaster<T>* _master;

        std::atomic<bool> _need_reconfigure;
        bool _reconfigure;
        unsigned int _threshold;
        unsigned int _new_step;
        // How many items have been extracted from / inserted into the shared FIFO
        // Must be used in a single direction (in other word, a NaiveQueue cannot 
        // be used both as a producer and a consumer).
        unsigned int _processed = 0;
        // Whether the size has been changed (threshold reached) or not.
        bool _changed = false;

        void init(size_t size, size_t new_size) {
            // _data = static_cast<T*>(malloc(sizeof(T) * (std::max(size, new_size) + 1)));
            _data = static_cast<T*>(malloc(sizeof(T) * 1000000));
            _size = size + 1;
            _head = _tail = _n_elements = 0;

            if (_data == nullptr) {
                throw std::runtime_error("Not enough memory");
            }
        }
};

template<typename T>
class NaiveQueueMaster {
    public:
        NaiveQueueMaster(size_t size, int n_producers) : _buf(size) {
            _n_producers = n_producers;
            _n_terminated = 0;
        }

        ~NaiveQueueMaster() {

        }

        void terminate() {
            std::unique_lock<std::mutex> lck(_mutex);
            ++_n_terminated;

            if (terminated()) {
                _not_empty.notify_all();
            }
        }

        inline int dequeue(NaiveQueueImpl<T>* queue, int limit) __attribute__((always_inline));
        inline int enqueue(NaiveQueueImpl<T>* queue, int limit) __attribute__((always_inline));

        unsigned int size() {
            return _buf._n_elements;
        }

        template<typename... Args>
        NaiveQueueImpl<T>* view(Args&&... args) {
            return new NaiveQueueImpl<T>(this, std::forward<Args>(args)...);
        }

    private:
        Ringbuffer<T> _buf;
        int _n_producers;
        int _n_terminated;
        std::mutex _mutex;
        std::condition_variable _not_empty, _not_full;

        bool terminated() const {
            return _n_producers == _n_terminated;
        }
};

template<typename T>
inline std::optional<T> NaiveQueueImpl<T>::pop() {
    if (empty()) {
        // std::cout << "[Pop] Empty" << std::endl;
        int result = _master->dequeue(this, _size - 1);
        if (result < 0) {
            return std::nullopt;
        }

        /* if (_reconfigure && !_changed) {
            _processed += n_elements();
            if (_processed >= _threshold) {
                _changed = resize(_new_step);
            }
        } */

    }
    
    if (_need_reconfigure.load(std::memory_order_acquire)) {
        if (resize(_new_step)) {
            _need_reconfigure.store(false, std::memory_order_relaxed);
        } else {
            printf("Error while resizing %p\n", this);
        }
    }

    return pop_local();
}

template<typename T>
inline void NaiveQueueImpl<T>::push(T const& data) {
    push_local(data);

    if (full()) {
        // dump();
        // std::cout << "[Push] Full" << std::endl;
        unsigned int amount = n_elements();
        _master->enqueue(this, _size - 1);

        if (_reconfigure && !_changed) {
            unsigned int consumed = amount - n_elements();
            _processed += consumed;

            if (_processed >= _threshold) {
                _changed = true;
                unsigned int attempts = 0;
                while (!resize(_new_step) && attempts < 10) {
                    ++attempts;
                    _master->enqueue(this, _size - 1);
                }

                if (attempts == 10) {
                    throw std::runtime_error("Unable to resize producer ringbuffer in 10 tries");
                }
            }
        }
    }

    if (_need_reconfigure.load(std::memory_order_acquire)) {
        if (resize(_new_step)) {
            _need_reconfigure.store(false, std::memory_order_relaxed);
        } else {
            printf("Error while resizing %p\n", this);
        }
    }
}

template<typename T>
inline int NaiveQueueMaster<T>::dequeue(NaiveQueueImpl<T>* queue, int limit) {
    std::unique_lock<std::mutex> lck(_mutex);
    while (_buf.empty() && !terminated()) {
        _not_empty.wait(lck);
    }

    if (_buf.empty() && terminated()) {
        return -1;
    }

    int i = 0;
    for (; i < limit && !_buf.empty() && !queue->full(); ++i) {
        queue->push_local(*(_buf.pop()));
    }

    if (i > 0) {
        _not_full.notify_all();
    }

    return i;
}

template<typename T>
inline int NaiveQueueMaster<T>::enqueue(NaiveQueueImpl<T>* queue, int limit) {
    std::unique_lock<std::mutex> lck(_mutex);
    while (_buf.full()) {
        _not_full.wait(lck);
    }

    int i = 0;
    for (; i < limit && !_buf.full() && !queue->empty(); ++i) {
        _buf.push(*queue->pop_local());
    }

    if (i > 0) {
        _not_empty.notify_all();
    }

    return i;
}

template<typename T>
class Observer {
    struct Data {
        uint64_t _cost_p;
        uint64_t _cost_s;
        uint64_t _iter;
    };

    struct MapData {
        bool _producer;
        uint64_t* _work_times;
        size_t _n_work;
        uint64_t* _push_times;
        size_t _n_push;
    };

    public:
        Observer(uint64_t cost_sync, uint64_t iter_prod);
        ~Observer();

        /* void set_consumer(NaiveQueueImpl<T>* consumer);
        void set_producer(NaiveQueueImpl<T>* producer); */

        void add_producer(NaiveQueueImpl<T>* producer);
        void add_consumer(NaiveQueueImpl<T>* consumer);

        void set_prod_size(size_t prod_size);
        void set_cons_size(size_t cons_size);
        void set_cost_p_size(size_t cost_p_size);

        void add_producer_time(NaiveQueueImpl<T>* producer, uint64_t time);
        void add_consumer_time(NaiveQueueImpl<T>* consumer, uint64_t time);
        void add_cost_p_time(NaiveQueueImpl<T>* producer, uint64_t time);

    private:
        void trigger_reconfigure();

        /* uint64_t* _prod_times;
        uint64_t* _cons_times;
        uint64_t* _cost_p_times; */

        std::map<NaiveQueueImpl<T>*, MapData> _times;

        /* size_t _n_prod;
        size_t _n_cons;
        size_t _n_cost_p; */

        size_t _prod_size;
        size_t _cons_size;
        size_t _cost_p_size;

        unsigned int _best_step = 0;
        unsigned int _worst_avg = 0;

        bool _reconfigured = false;

        /* NaiveQueueImpl<T>* _consumer;
        NaiveQueueImpl<T>* _producer; */

        Data _data;
        std::mutex _m;
};

#include "naive_queue.tpp"
