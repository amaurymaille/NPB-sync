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

#include <semaphore.h>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

template<typename T>
class NaiveQueueImpl;

template<typename T>
class Ringbuffer {
    public:
        friend NaiveQueueImpl<T>;

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

        inline int available() const {
            return _size - _n_elements;
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
            printf("_buf._n_elements = %d\n", _buf.n_elements());
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

/* This class works as a ringbuffer.
 * Internally, it uses an array with a head and a tail.
 * The head is used to represent the position at which the next insertion will happen.
 * The tail is used to represent the position at which the current element sits.
 * If the array is empty, both the head and the tail are at the same position.
 * 
 * The array uses a sentinel cell in order to prevent head and tail reaching the same
 * position in two different scenarios (empty and full). This removes the need for an
 * extra boolean, which makes the structures slightly more cache-friendly, as th sentinel
 * cell will fit into the cache, while the boolean may not.
 */
template<typename T>
class NaiveQueueImpl {
    public:
        NaiveQueueImpl(NaiveQueueMaster<T>* master, size_t size, bool reconfigure, 
                unsigned int threshold, unsigned int new_step) : 
            _master(master), _reconfigure(reconfigure), 
            _threshold(threshold), _new_step(new_step) {
            _reconfigured.store(false, std::memory_order_relaxed);
            _need_reconfigure.store(false, std::memory_order_relaxed);
            _begin = std::chrono::steady_clock::now();
            init(size, new_step);
        }

        ~NaiveQueueImpl() {
            free(_data);
            // printf("%p finished at step = %d\n", this, _size);
        }

        inline bool empty() const __attribute__((always_inline)) {
            return _tail == _head;
        }

        inline bool full() const __attribute__((always_inline)) {
            return _head == (_tail - 1 + _size) % _size;
        }

        inline void shared_transfer(Ringbuffer<T>& _buf, int limit) __attribute__((always_inline));

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
        inline std::tuple<uint64_t, uint64_t> timed_push(T const& data) __attribute__((always_inline));

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
            return _size - 1;
        }

        bool was_reconfigured() const {
            return _reconfigured.load(std::memory_order_acquire);
        }

    private:
        T* _data;
        size_t _n_elements;
        size_t _size;
        int _head, _tail;
        NaiveQueueMaster<T>* _master;
        std::chrono::time_point<std::chrono::steady_clock> _begin;

        // New step. Used both in manual and automatic reconfiguration.
        // Not atomic, but needs to be written before _need_reconfigure is set to true, 
        // in order for memory_order_acquire to cause the new value to be visible.
        unsigned int _new_step;

        /// Automatic reconfiguration

        // Do we need to perform a reconfiguration ?
        std::atomic<bool> _need_reconfigure;
        // Was the reconfiguration done ?
        std::atomic<bool> _reconfigured;


        /// Manual reconfiguration

        // Do we trigger a manually configured reconfiguration ?
        bool _reconfigure;
        // If so, when?
        unsigned int _threshold;
        // How many items have been extracted from / inserted into the shared FIFO
        // Must be used in a single direction (in other word, a NaiveQueue cannot 
        // be used both as a producer and a consumer).
        unsigned int _processed = 0;
        // Whether the size has been changed (threshold reached) or not.
        bool _changed = false;

        void init(size_t size, size_t new_size) {
            // _data = static_cast<T*>(malloc(sizeof(T) * (std::max(size, new_size) + 1)));
            _data = static_cast<T*>(malloc(sizeof(T) * 1000000));
            // printf("data = %p\n", _data);
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

        if (_reconfigure && !_changed) {
            _processed += n_elements();
            if (_processed >= _threshold) {
                _changed = resize(_new_step);
            }
        }

    }
    
    if (_need_reconfigure.load(std::memory_order_acquire)) {
        // printf("Reconfiguring %p\n", this);
        if (resize(_new_step)) {
            // auto now = std::chrono::steady_clock::now();
            // printf("Reconfigured at %llu\n", std::chrono::duration_cast<std::chrono::nanoseconds>(now - _begin).count());
            _reconfigured.store(true, std::memory_order_release);
            _need_reconfigure.store(false, std::memory_order_relaxed);
        } else {
            // printf("Error while resizing %p\n", this);
        }
    }

    return pop_local();
}

template<typename T>
inline std::tuple<uint64_t, uint64_t> NaiveQueueImpl<T>::timed_push(T const& data) {
    std::chrono::time_point<std::chrono::steady_clock> begin = std::chrono::steady_clock::now();
    push_local(data);
    std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::steady_clock::now();
    std::chrono::time_point<std::chrono::steady_clock> begin_enqueue; 
    std::chrono::time_point<std::chrono::steady_clock> end_enqueue;
    bool enqueued = false;

    if (full()) {
        // dump();
        // std::cout << "[Push] Full" << std::endl;
        
        enqueued = true;
        unsigned int amount = n_elements();
        begin_enqueue = std::chrono::steady_clock::now();
        _master->enqueue(this, _size - 1);
        end_enqueue = std::chrono::steady_clock::now();

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
            // printf("Reconfiguring %p\n", this);
            // auto now = std::chrono::steady_clock::now();
            // printf("Reconfigured at %llu\n", std::chrono::duration_cast<std::chrono::nanoseconds>(now - _begin).count());
            _reconfigured.store(true, std::memory_order_release);
            _need_reconfigure.store(false, std::memory_order_relaxed);
        } else {
            // printf("Error while resizing %p\n", this);
        }
    }

    return { std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count(),
        enqueued ? std::chrono::duration_cast<std::chrono::nanoseconds>(end_enqueue - begin_enqueue).count() : 0 };
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
            // printf("Reconfiguring %p\n", this);
            // auto now = std::chrono::steady_clock::now();
            // printf("Reconfigured at %llu\n", std::chrono::duration_cast<std::chrono::nanoseconds>(now - _begin).count());
            _reconfigured.store(true, std::memory_order_release);
            _need_reconfigure.store(false, std::memory_order_relaxed);
        } else {
            // printf("Error while resizing %p\n", this);
        }
    }
}

template<typename T>
inline void NaiveQueueImpl<T>::shared_transfer(Ringbuffer<T>& buffer, int limit) {
    int available = buffer.available(); 
    int hard_limit = std::min({(size_t)available, (size_t)limit, n_elements()});

    // Nothing to transfer, empty.
    if (_head == _tail) {
        return;
    } else if (_head > _tail) {
        // Easy case, the buffer doesn't wrap around at the end.
        // However, we need to check the state of the target ringbuffer beforehand.

        // No need to wrap around. This covers the following cases:
        //   * buffer._tail <= buffer._head
        //   * buffer._head < buffer._tail
        if (buffer._head + hard_limit < buffer._size) {
            memcpy(buffer._data + buffer._head, _data + _tail, hard_limit * sizeof(T));
            buffer._head += hard_limit;
        } else {
            // Need to wrap around. There is only one situation in which this can arise,
            // which is buffer._tail < buffer._head. We cannot have buffer._head < buffer._tail
            // and need to wrap around as this would implu we wrap around twice, which is not
            // authorized.
            memcpy(buffer._data + buffer._head, _data + _tail, sizeof(T) * (buffer._size - 1 - buffer._head));
            memcpy(buffer._data, _data + _tail + (buffer._size - 1 - buffer._head), sizeof(T) * (hard_limit - (buffer._size - 1 - buffer._head)));
            buffer._head = hard_limit - (buffer._size - 1 - buffer._head);
        }

        _tail += hard_limit;
    } else {
        // Annoying case, the buffer wraps around at the end.
        // Split the present ringbuffer into its two parts (tail -> end (upper part), begin -> head (lower part)).
        // We also need to check the destination ringbuffer to check if we need to wrap data around in that one
        // as well.

        // Indicates if we need to split the source ringbuffer. If we are copying only a slice
        // of the upper part of the source ringbuffer, and do not need to wrap around, there is
        // no need to split. Otherwise, split the copy into two parts: copy the upper part, then
        // copy the lower part.
        bool need_source_split = (_tail + hard_limit) >= _size;
        size_t upper_count = 0;
        if (need_source_split) {
            upper_count = hard_limit - (_size - 1 - _tail);
        }
        
        // Same as above, check if we need to wrap around in the target buffer.
        if (buffer._head + hard_limit < buffer._size) {
            if (need_source_split) {
                memcpy(buffer._data + buffer._head, _data + _tail, upper_count * sizeof(T));
                memcpy(buffer._data + buffer._head + upper_count, _data, (hard_limit - upper_count) * sizeof(T));
            } else {
                memcpy(buffer._data + buffer._head, _data + _tail, hard_limit * sizeof(T));
            }

            buffer._head += hard_limit;
        } else {
            if (need_source_split) {
                // We may have to perform a second split. When attempting to copy either the upper
                // or lower part of the source ringbuffer, there is no guarantee that each parts will fit
                // neatly into the destination ringbuffer as the wrap around may occur at any point during
                // the copy. First check if we need to perform a split.
                
                // Do we need to split the upper part of the source buffer ?
                bool need_upper_split = (buffer._head + upper_count) >= buffer._size;
                // Do we need to split the lower part of the source buffer ? Only if the upper part of the source
                // buffer fits neatly in the destination buffer.
                bool need_lower_split = !need_upper_split && (buffer._head + upper_count) < buffer._size;

                if (need_upper_split && need_lower_split) {
                    throw std::runtime_error("Shared transfer would require two splits.");
                }

                if (need_upper_split) {
                    size_t free_dest_space = buffer._size - 1 - buffer._head;
                    // Fill the free space in the dest buffer with the lower part of the upper part of the
                    // source buffer.
                    memcpy(buffer._data + buffer._head, _data + _tail, free_dest_space * sizeof(T));
                    // Fill the beginning of the dest buffer with the remaining data in the upper part of
                    // the source buffer.
                    // _size - 1 - _tail is the amount of data in the upper part. Remove what was written
                    // to the dest buffer.
                    size_t remaining_in_upper = _size - 1 - (_tail + free_dest_space);
                    memcpy(buffer._data, _data + _tail + free_dest_space, remaining_in_upper * sizeof(T));
                    // Fill the destination buffer from what was written with the content in the lower part
                    // of the source buffeR.
                    memcpy(buffer._data + remaining_in_upper, _data, _head * sizeof(T));
                } else if (need_lower_split) {
                    // Start filling the free space in the destination buffer with everything from the upper
                    // part of the source buffer.
                    size_t upper_part_size = _size - 1 - _tail;
                    memcpy(buffer._data + buffer._head, _data + _tail, upper_part_size * sizeof(T));
                    // Continue filling the free space in the destination buffer with some of the lower
                    // part of the source buffer.
                    size_t remaining_space = buffer._size - 1 - (buffer._head + _size - 1 - _tail);
                    memcpy(buffer._data + buffer._head + upper_part_size, _data, remaining_space * sizeof(T));
                    // Write the remaining of the lower part of the source buffer at the beginning of the
                    // destination buffer.
                    memcpy(buffer._data, _data + remaining_space, (_head - remaining_space) * sizeof(T));
                } else {
                    memcpy(buffer._data + buffer._head, _data + _tail, upper_count * sizeof(T));
                    memcpy(buffer._data, _data, _head * sizeof(T));
                }
            } else {
                memcpy(buffer._data + buffer._head, _data + _tail, sizeof(T) * (buffer._size - 1 - buffer._head));
                memcpy(buffer._data, _data + _tail + (buffer._size - 1 - buffer._head), sizeof(T) * (hard_limit - (buffer._size - 1 - buffer._head)));
            }

            buffer._head = hard_limit - (buffer._size - 1 - buffer._head);
        }

        if (need_source_split) {
            _tail = hard_limit - upper_count;
        } else {
            _tail += hard_limit;
        }
    }

    buffer._n_elements += hard_limit;
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
    // printf("_buf._n_elements = %d\n", _buf.n_elements());
    while (_buf.full()) {
        _not_full.wait(lck);
    }

    int i = 0;
    /* for (; i < limit && !_buf.full() && !queue->empty(); ++i) {
        _buf.push(*queue->pop_local());
    } */
    queue->shared_transfer(_buf, limit);

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
        uint64_t _wi;
    };

    struct MapData {
        bool _producer;
        uint64_t* _work_times;
        size_t _n_work;
        uint64_t* _push_times;
        size_t _n_push;
        uint64_t* _sync_times;
        uint64_t _n_sync;
    };

    public:
        Observer(uint64_t cost_sync, uint64_t iter_prod, int n_threads);
        ~Observer();

        /* void set_consumer(NaiveQueueImpl<T>* consumer);
        void set_producer(NaiveQueueImpl<T>* producer); */

        void add_producer(NaiveQueueImpl<T>* producer);
        void add_consumer(NaiveQueueImpl<T>* consumer);

        void set_prod_size(size_t prod_size);
        void set_cons_size(size_t cons_size);
        void set_cost_p_cost_s_size(size_t cost_p_cost_s_size);

        void add_producer_time(NaiveQueueImpl<T>* producer, uint64_t time);
        void add_consumer_time(NaiveQueueImpl<T>* consumer, uint64_t time);
        void add_cost_p_cost_s_time(NaiveQueueImpl<T>* producer, uint64_t push_time, uint64_t sync_time);

        void set_cost_s_size(size_t cost_s_size);
        bool add_cost_s_time(NaiveQueueImpl<T>* producer, uint64_t sync_time);

        json serialize() const;

        // void begin();
        // void measure();

    private:
        void trigger_reconfigure(bool first);

        /* uint64_t* _prod_times;
        uint64_t* _cons_times;
        uint64_t* _cost_p_times; */

        std::map<NaiveQueueImpl<T>*, MapData> _times;
        std::map<NaiveQueueImpl<T>*, std::vector<uint64_t>> _cost_s;
        /* std::chrono::time_point<std::chrono::steady_clock> _begin;
        unsigned long long _time;
        sem_t _reconfigure_sem; */

        /* size_t _n_prod;
        size_t _n_cons;
        size_t _n_cost_p; */

        size_t _prod_size;
        size_t _cons_size;
        size_t _cost_p_cost_s_size;
        size_t _cost_s_size;

        unsigned int _best_step = 0;
        unsigned int _worst_avg = 0;
        std::vector<uint64_t> _cost_p;
        std::vector<uint64_t> _averages;

        bool _reconfigured = false;
        bool _reconfigured_twice = false;

        /* NaiveQueueImpl<T>* _consumer;
        NaiveQueueImpl<T>* _producer; */

        Data _data;
        std::mutex _m;

        int _n_threads;
};

#include "naive_queue.tpp"
