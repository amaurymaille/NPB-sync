#pragma once

#include <pthread.h>
#include <semaphore.h>

#include <array>
#include <map>
#include <tuple>
#include <vector>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>

#include "utils.h"

// extern std::map<void*, std::tuple<std::string, std::array<size_t, 2>>> _semaphore_data;
namespace Globals {
    extern std::chrono::time_point<std::chrono::steady_clock> _start_time;
    std::chrono::time_point<std::chrono::steady_clock> now();
}

template<typename T>
class FIFOChunk;

template<typename T>
using FIFOChunkRange = std::tuple<FIFOChunk<T>*, T*, size_t>;

template<typename T>
class SmartFIFOElements;

template<typename T>
class SmartFIFO;

template<typename T>
class SmartFIFOImpl;

/* Special semaphore used for SmartFIFO. Allows increment and decrement of the
 * held value with arbitrary values instead of systematically 1.
 */
class SmartFIFOSemaphore {
public:
    SmartFIFOSemaphore(unsigned int start) {
        _value.store(start, std::memory_order_relaxed);
        _finished.store(false, std::memory_order_relaxed);
        sem_init(&_sem, 0, 0);
    }

    ~SmartFIFOSemaphore() {
    }

    void post(unsigned int i) {
        _value.fetch_add(i, std::memory_order_release);
        /* int waiting;
        sem_getvalue(&_sem, &waiting);
        if (waiting <= 0) {
            sem_post(&_sem);
        } */
    }

    /* Attempt to decrease the value by at most i. The value held by the semaphore
     * must be at least 1 for the operation to succeed. Otherwise, continually 
     * attempt to decrease the value by at most i until the value was at least 1
     * when the attempt was made.
     *
     * Pas de famine si on a un seul consommateur à la fois !
     */
    int wait(unsigned int i, std::optional<std::chrono::nanoseconds> const& timeout) {
        int old_value;
        auto begin = std::chrono::steady_clock::now();
        /* If there was nothing in the semaphore (which should not happen in a 
         * single consumer context), post back what was taken and then always_wait.
         */
        while ((old_value = always_wait(i)) <= 0 && !_finished.load(std::memory_order_acquire)) {
            post(i);

            if (timeout) {
                if (std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin) >= *timeout) {
                    return -1;
                }
            }
            // sem_wait(&_sem);
        } 

        if (_finished.load(std::memory_order_acquire)) {
            return 0;
        }

        if (old_value < i) {
            /* Corner case: we took more than what was there. So only take what was 
             * there and give back the excess.
             */
            post(i - old_value);
        }

        // There wasn't enough in the FIFO. i > old_value, return old_value
        if (old_value < i) {
            return old_value;
        } else {
            return i;
        }
    }

    void finish() {
        _finished.store(true, std::memory_order_release);
    }

    int get_value() {
        return _value.load(std::memory_order_acquire);
    }

private:
    int always_wait(unsigned int i) {
        return _value.fetch_sub(i, std::memory_order_release);
    }

    std::atomic<int> _value;
    std::atomic<bool> _finished;
    sem_t _sem;
};

template<typename T>
class FIFOChunk {
    struct size_constructor_hint_t { };
public:
    static size_constructor_hint_t size_constructor_hint;

    friend SmartFIFOElements<T>;
    friend SmartFIFOImpl<T>;
    friend SmartFIFO<T>;

    FIFOChunk(size_t size, size_constructor_hint_t const&) : _size(size), _elements(new T[size]) {
        _head = _elements;
        _nb_available__has_next.store(0, std::memory_order_relaxed);
        _next.store(nullptr, std::memory_order_relaxed);
        _references.store(1, std::memory_order_relaxed);
    }

    ~FIFOChunk() {
        delete[] _elements;
    }

    FIFOChunk(FIFOChunk<T> const&) = delete;
    FIFOChunk<T>& operator=(FIFOChunk<T> const&) = delete;

    FIFOChunk(FIFOChunk<T>&&) = delete;
    FIFOChunk<T>& operator=(FIFOChunk<T>&&) = delete;

    bool empty(bool& has_next) const {
        // std::unique_lock<std::mutex> lck(_m);
        size_t data = _nb_available__has_next.load(std::memory_order_acquire);
        has_next = (data & 1) != 0;
        return data >> 1 == 0;
    }

    template<typename T2>
    decay_enable_if_t<T, T2, FIFOChunk<T>*> push(T2&& element) {
        std::unique_lock<std::mutex> lck(_m);
        if (_size == _nb_elements) {
            FIFOChunk<T>* chunk = new FIFOChunk<T>(_size);
            chunk->push(std::forward<T>(element));
            _next.store(chunk, std::memory_order_release);
            _nb_available__has_next.fetch_add(1, std::memory_order_release);
            return chunk;
        } else {
            _elements[_nb_elements++] = std::forward<T>(element);
            _nb_available__has_next.fetch_add(2, std::memory_order_release);
            return nullptr;
        }
    }

    template<typename T2>
    decay_enable_if_t<T, T2, FIFOChunk<T>*> unsafe_push(T2&& element) {
        if (_size == _nb_elements) {
            FIFOChunk<T>* chunk = new FIFOChunk<T>(_size, FIFOChunk<T>::size_constructor_hint);
            chunk->unsafe_push(std::forward<T>(element));
            _next.store(chunk, std::memory_order_relaxed);
            _nb_available__has_next.fetch_add(1, std::memory_order_relaxed);
            return chunk;
        } else {
            _elements[_nb_elements++] = std::forward<T>(element);
            _nb_available__has_next.fetch_add(2, std::memory_order_relaxed);
            return nullptr;
        }
    }

    /* over becomes true in two possible cases:
     *  1) There is a request for more elements than what is available AND 
     * this is the last chunk in the chain.
     *  2) There were enough elements in the chunk to completely fulfil the
     * request.
     */
    FIFOChunkRange<T> pop(size_t& nb_elements, bool& over) {
        // std::unique_lock<std::mutex> lck(_m);
        _references.fetch_add(1, std::memory_order_release);
        size_t data = _nb_available__has_next.load(std::memory_order_acquire);
        size_t nb_available = data >> 1;

        bool has_next = (data & 1) != 0;
        if (nb_available == 0) {
            if (!has_next) {
                over = true;
                // throw std::runtime_error("Halp 2");
            }
            return std::make_tuple(this, nullptr, 0);
        }

        T* start = _head; 

        if (nb_available > nb_elements) {
            nb_available = nb_elements;
        }

        _head += nb_available;
        _nb_available__has_next.fetch_sub(nb_available << 1, std::memory_order_release);

        if (nb_elements == nb_available || (nb_available < nb_elements && !has_next)) {
            over = true;
        }

        nb_elements -= nb_available;

        return std::make_tuple(this, start, nb_available);
    }

    void reset(size_t new_size) {
        _size = new_size;
        _elements = new T[_size];
        _head = _elements;
        _nb_elements = 0;
        _nb_available__has_next.store(0, std::memory_order_relaxed);
        _next.store(nullptr, std::memory_order_relaxed);
        _references.store(1, std::memory_order_relaxed);
    }

    void dump() const {
        std::cout << "Chunk " << this << std::endl;
        std::cout << "\tElements: " << std::endl;
        for (int i = 0; i < _nb_elements; ++i) {
            std::cout << "\t\t" << _elements[i] << std::endl;
        }
        FIFOChunk<T>* next =  _next.load(std::memory_order_acquire);
        std::cout << "\tNext: " << next << std::endl;
        if (next) {
            next->dump();
        }
    }

    void set_next(FIFOChunk<T>* next) {
        std::unique_lock<std::mutex> lck(_m);
        _next.store(next, std::memory_order_release);
        _nb_available__has_next.fetch_add(1, std::memory_order_release);
    }

    void unsafe_set_next(FIFOChunk<T>* next) {
        _next.store(next, std::memory_order_relaxed);
        _nb_available__has_next.fetch_add(1, std::memory_order_relaxed);
    }

    size_t nb_elements() const {
        if (FIFOChunk<T>* next = _next.load(std::memory_order_relaxed)) {
            return _nb_elements + next->nb_elements();
        } else {
            return _nb_elements;
        }
    }

    size_t unsafe_nb_elements_self() const {
        return _nb_elements;
    }

private:
    size_t _size;
    T* _elements = nullptr;
    T* _head;
    // How many elements have been pushed (indicates fullness).
    size_t _nb_elements = 0;
    // How many elements can be taken (indicates availability).
    // First bit indicates if _next stores a null pointer or not.
    // Next 63 bits are the number of elements available.
    std::atomic<size_t> _nb_available__has_next;
    // The maximum amount of elements that can be inserted.
    std::atomic<FIFOChunk<T>*> _next;
    std::atomic<unsigned int> _references;
    std::mutex _m;

private:
    FIFOChunk(FIFOChunk<T>* chunk) : _size(chunk->_size), _elements(chunk->_elements) {
        _head = _elements;
        _nb_available__has_next.store(chunk->_nb_available__has_next.load(std::memory_order_relaxed), std::memory_order_relaxed);
        _next.store(chunk->_next.load(std::memory_order_relaxed), std::memory_order_relaxed);
        _references.store(1, std::memory_order_relaxed);
    }

    void destroy() {
        std::unique_lock<std::mutex> lck(_m);
        _references.fetch_sub(1, std::memory_order_release);

        if (_references.load(std::memory_order_acquire) == 0 && _nb_available__has_next.load(std::memory_order_acquire) >> 1 == 0) {
            // delete this;
        }
    }

    void freeze() {
        // std::unique_lock<std::mutex> lck(_m);
        _size = _nb_elements;
    }
};

template<typename T>
typename FIFOChunk<T>::size_constructor_hint_t FIFOChunk<T>::size_constructor_hint;

template<typename T>
using Ranges = std::vector<FIFOChunkRange<T>>;

template<typename T>
class SmartFIFOElements {
public:
    SmartFIFOElements(Ranges<T>&& ranges) : _ranges(std::move(ranges)) {
        _current_range = _ranges.cbegin();
        _current_index = 0;
    }

    SmartFIFOElements(SmartFIFOElements<T> const&) = delete;
    SmartFIFOElements& operator=(SmartFIFOElements<T> const&) = delete;

    SmartFIFOElements(SmartFIFOElements<T>&&) = delete;
    SmartFIFOElements& operator=(SmartFIFOElements<T>&&) = delete;

    ~SmartFIFOElements() {

    }

    T* next() const {
        if (!has_next()) {
            return nullptr;
        } else {
            T* retval = std::get<T*>(*_current_range) + _current_index;
            ++_current_index;

            if (_current_index == std::get<size_t>(*_current_range)) {
                ++_current_range;
                _current_index = 0;

                while (_current_range != _ranges.cend() && std::get<size_t>(*_current_range) == 0) {
                    ++_current_range;
                }
            }

            return retval;
        }
    }

    bool has_next() const {
        return !empty() && _current_range != _ranges.cend();
    }

    bool empty() const {
        return _ranges.empty() || (_ranges.size() == 1 && std::get<size_t>(_ranges.front()) == 0 && std::get<FIFOChunk<T>*>(_ranges.front())->_next.load(std::memory_order_acquire) == nullptr);
    }

    void set(Ranges<T>&& ranges) {
        _ranges = std::move(ranges);
        _current_range = _ranges.cbegin();
        _current_index = 0;
    }

    void clear() {
        for (FIFOChunkRange<T> const& range: _ranges) {
            std::get<FIFOChunk<T>*>(range)->destroy();
        }
    }

    size_t size() const {
        size_t total = 0;
        for (FIFOChunkRange<T> const& range: _ranges) {
            total += std::get<size_t>(range);
        }
        return total;
    }

private:
    Ranges<T> _ranges;
    mutable typename Ranges<T>::const_iterator _current_range;
    mutable size_t _current_index;
};

template<typename T2>
class SmartFIFO {
public:
    SmartFIFO(SmartFIFOImpl<T2>* fifo, size_t step, bool reconfigure, unsigned int change_after = 0, unsigned int new_step = 0) : _fifo(fifo), _step(step), _elements(Ranges<T2>()), _chunk(step, FIFOChunk<T2>::size_constructor_hint), _reconfigure(reconfigure), _change_after(change_after), _new_step(new_step) {
    }

    template<typename T3>
    decay_enable_if_t<T2, T3, size_t> push(T3&& value) {
        if (_over) {
            throw std::runtime_error("Trying to push in terminated FIFO !");
        }

        FIFOChunk<T2>* next = _chunk.unsafe_push(std::forward<T3>(value));
        if (next) {
            _fifo->push_chunk(&_chunk, _step + 1);
            if (_reconfigure) {
                _reconfigure_step_push(_step + 1);
            }
            _chunk.reset(_step);
            return _step + 1;
        } else {
            return 0;
        }
    }

    std::tuple<bool, size_t> pop_copy(std::optional<T2>& opt, std::optional<std::chrono::nanoseconds> const& duration = std::nullopt, SmartFIFO<T2>* target_fifo = nullptr) {
        auto [valid, nb_elements, prepared] = prepare_elements(duration, target_fifo);
        if (nb_elements == 0 && !prepared) {
            throw std::runtime_error("Niah ?");
        }
        if (!prepared) {
            opt = std::nullopt;
        } else {
            T2* next = _elements.next();
            opt = *next;
            /* if constexpr (std::is_pointer_v<std::decay_t<T2>>) {
                if (*opt == nullptr) {
                    throw std::runtime_error("Halp");
                }
            } */
        }

        return std::make_tuple(valid && prepared, nb_elements);
    }

    std::tuple<bool, size_t> pop(std::optional<T2*>& opt, std::optional<std::chrono::nanoseconds> const& duration = std::nullopt, SmartFIFO<T2>* target_fifo = nullptr) {
        auto [valid, nb_elements, prepared] = prepare_elements(duration, target_fifo);
        if (!prepared) {
            opt = std::nullopt;
        } else {
            opt = _elements.next();
        }

        return std::make_tuple(valid && nb_elements != 0, nb_elements);
    }

    void push_immediate() {
        _fifo->push_chunk(&_chunk, _step);
        if (_reconfigure) {
            _reconfigure_step_push(_step);
        }
        _chunk.reset(_step);
    }

    void safe_push_immediate() {
        if (_chunk.unsafe_nb_elements_self() != 0) {
            push_immediate();
        }
    }

    void terminate_producer() {
        _chunk.freeze();
        _fifo->push_chunk(&_chunk, _chunk.nb_elements());
        _fifo->terminate_producer();
        _chunk.reset(_step);
        _over = true;
    }

    size_t get_step() const {
        return _step;
    }

    void dump() const {
        _fifo->dump();
    }

    SmartFIFOImpl<T2>* impl() {
        return _fifo;
    }

private:
    // Return valid (?), number of elements extracted, and
    std::tuple<bool, size_t, bool> prepare_elements(std::optional<std::chrono::nanoseconds> const& duration = std::nullopt, SmartFIFO<T2>* target_fifo = nullptr) {
        if (_elements.empty()) {
            if (!_fifo->pop(_elements, _step, duration)) {
                // printf("Timed out !\n");
                if (target_fifo) {
                    target_fifo->safe_push_immediate();
                }
                return prepare_elements();
            }

            if (_reconfigure) {
                _reconfigure_step_pop(_step);
            }
            
            if (_elements.empty()) {
                return std::make_tuple(true, 0, false);
            } else {
                return std::make_tuple(true, _elements.size(), true);
            }
        } else if (!_elements.has_next()) {
            _elements.clear();
            if (!_fifo->pop(_elements, _step, duration)) {
                // printf("Timed out !\n");
                if (target_fifo) {
                    target_fifo->safe_push_immediate();
                }
                return prepare_elements();
            }

            if (_reconfigure) {
                _reconfigure_step_pop(_step);
            }

            if (_elements.empty()) {
                return std::make_tuple(true, 0, false);
            } else {
                return std::make_tuple(true, _elements.size(), true);
            }
        } 

        // !_elements.empty() && _elements.has_next()
        return std::make_tuple(false, _elements.size(), true);
    }

    void _reconfigure_step_push(unsigned int added) {
        _inserted += added;
        if (_inserted >= _change_after && !_changed) {
            // printf("Changed push step from %d to %d\n", _step, _new_step);
            // auto now = Globals::now();
            // printf("[Change PUSH] %lu:%lu\n", _step, std::chrono::duration_cast<std::chrono::nanoseconds>(now - Globals::_start_time).count());
            _step = _new_step;
            _changed = true;
        }
    }

    void _reconfigure_step_pop(unsigned int removed) {
        _removed += removed;
        if (_removed >= _change_after && !_changed) {
            // printf("Changed pop step from %d to %d\n", _step, _new_step);
            //auto now = Globals::now();
            //printf("[Change POP] %lu:%lu\n", _step, std::chrono::duration_cast<std::chrono::nanoseconds>(now - Globals::_start_time).count());
            _step = _new_step;
            _changed = true;
        }
    }

    SmartFIFOImpl<T2>* _fifo;
    size_t _step;
    size_t _nb_elements = 0;
    SmartFIFOElements<T2> _elements;
    FIFOChunk<T2> _chunk;
    bool _over = false;
    bool _reconfigure = false;
    unsigned int _change_after = 0;
    unsigned int _new_step = 0;
    unsigned int _inserted = 0;
    unsigned int _removed = 0;
    bool _changed = false;
};

struct TimestampData {
    unsigned long long begin;
    unsigned long long end;
    unsigned long long diff;
    unsigned long long count;
};

extern TimestampData timestamp_data[1000000];
extern size_t _log_n;

template<typename T>
class SmartFIFOImpl {
public:
    
    typedef SmartFIFO<T> smart_fifo;

public:
    SmartFIFOImpl(bool log = false) : _log(log), _sem(0), _description() {
        _tail.store(new FIFOChunk<T>(0, FIFOChunk<T>::size_constructor_hint), std::memory_order_relaxed);
        _head = _tail;
        _nb_producers__done.store(0, std::memory_order_relaxed);

    }

    SmartFIFOImpl(std::string&& description) : SmartFIFOImpl() {
        _description = std::move(description);
    }

    ~SmartFIFOImpl() {
        FIFOChunk<T>* next = _head;
        while (next) {
            FIFOChunk<T>* copy = next;
            next = copy->_next.load(std::memory_order_acquire);
            copy->destroy();
        }
    }

    SmartFIFOImpl(SmartFIFO<T> const&) = delete;
    SmartFIFOImpl(SmartFIFO<T>&&) = delete;

    SmartFIFOImpl<T>& operator=(SmartFIFO<T> const&) = delete;
    SmartFIFOImpl<T>& operator=(SmartFIFO<T>&&) = delete;

    void set_description(std::string&& description) {
        _description = std::move(description);
    }

    void set_log(bool on) {
        _log = on;
    }

    SmartFIFO<T> get_proxy(size_t step) {
        return SmartFIFO(this, step);
    }

//    template<typename T2>
//    decay_enable_if_t<T, T2, void> push(T2&& element) {
//        std::unique_lock<std::mutex> lck(_prod_mutex);
//        FIFOChunk<T>* tail = _tail.load(std::memory_order_relaxed);
//        FIFOChunk<T>* next = tail->push(std::move(element));
//        if (next) {
//            _tail.store(next, std::memory_order_release);
//        }
//
//        /* int waiting;
//        sem_getvalue(&_sem, &waiting);
//        if (waiting <= 0) {
//            sem_post(&_sem);
//        } */
//        _sem.post(1);
//         _sem_data[0] += 1;
//    }
    
    void push_chunk(FIFOChunk<T>* chunk, size_t nb_elements) {
        std::unique_lock<std::mutex> lck(_prod_mutex);
        // We can use memory_order_relaxed, because the potential write happens
        // inside a mutex protected zone. Therefore, if another thread attempts
        // to load _tail in this very function, the effects of the next write 
        // are visible by virtue of the mutex performing an automatic release
        // when it unlocks.
        FIFOChunk<T>* tail = _tail.load(std::memory_order_relaxed);
        // std::unique_lock<std::mutex> lck(_m);
        if (tail->_next) {
            throw std::runtime_error("Inconsistency detected: _tail isn't the tail");
        }

        FIFOChunk<T>* next = new FIFOChunk<T>(chunk);
        tail->freeze();
        tail->set_next(next);

        // Load next->_next with memory_order_acquire because I'm honestly not sure
        // whether I can use memory_order_relaxed or not.
        // In theory, there is no way at all to change the value of next->_next without
        // going through a producer thread, so it would make sense to use a relaxed 
        // ordering by applying the same principle as above for _tail, however there 
        // are way more references to next->_next through the code, and making sure
        // every single one of them properly ties to a producer thread is complicated.
        while (FIFOChunk<T>* n = next->_next.load(std::memory_order_acquire)) {
            next = n;
        }
        // Mandatory release here, because the consumer threads need to read the value of 
        // _tail up-to-date in order to determine if they can continue extracting data.
        // Theoretically it doesn't matter because it may just lead to early stop.
        _tail.store(next, std::memory_order_release);

        /* Here comes the nightmare. Post only if a consumer is waiting. A consumer is 
         * waiting only if the head is empty AND the head has no next element AND the 
         * FIFO is not terminated.
         * Consumers are mutually excluded and a consumer inside pop() will never release
         * the mutex unless it leaves the function. Therefore, there can be AT MOST one
         * consumer waiting on the semaphore.
         * Assume the FIFO is not empty. A consumer locks the mutex, sees the FIFO is not
         * empty, therefore it doesn't wait on the semaphore. We skip this.
         * Assume the FIFO is empty. A consumer locks the mutex, sees the FIFO is empty.
         * Branch:
         *  - The consumer reaches the semaphore before we post. Free the consumer immediately.
         *  - The consumer doesn't reach the semaphore before we check the semaphore. It means
         * that there is no invariant "Waiting on semaphore => FIFO is empty". Proof that this
         * does not deadlock:
         *
         * Assume program is well-formed, i.e. every single producer calls terminate_producer() 
         * once it is done, every consumer exhausts its SmartFIFO and a producer thread that has
         * called terminate_producer() doesn't call push() nor push_chunk() afterwards. 
         * Recall that the potential deadlock scenario is the case where a consumer thread is 
         * waiting on the semaphore while there is data inside the FIFO. This scenario can only
         * occur if a consumer thread checked for emptiness while a non-terminated producer thread
         * was writing data. The order of execution must be the following:
         * 1.a) Producer thread starts pushing
         * 1.b) Consumer thread starts poping 
         * 2) Consumer thread checks for emptiness. Emptiness checks returns true.
         * 3) Consumer thread checks for termination. Check returns false.
         * 4) Consumer thread checks for chaining of chunks. Check returns false.
         * 5) Consumer DOES NOT IMMEDIATELY WAIT
         * 2, 3, 4 //) Producer thread performs its own operations and checks semaphore. Semaphore
         * checks return zero. Do not post in semaphore.
         * 6) Consumer waits on semaphore. Wait for post.
         * At this point, the consumer mutex is held by the waiting consumer thread and won't unlock
         * until a producer performs a post.
         * WCS: there is no more data to push. Answer: producer needs to call terminate_producer().
         * Assume terminate_producer() is called before the consumer reaches the semaphore. It doesn't
         * matter: the last producer will unblock the semaphore in all cases. It doesn't matter which
         * value the semaphore holds because a consumer NEVER waits on the semaphore if the FIFO
         * is terminated. This doesn't deadlock.
         * If terminate_producer() is called but is not the last, the consumer thread will hold onto
         * the semaphore until either a producer pushes data, or all producers are done.
         */
        /* int waiting;
        sem_getvalue(&_sem, &waiting);
        if (waiting <= 0) {
            sem_post(&_sem);
        } */
        _sem.post(nb_elements);
        // _sem_data[0] += nb_elements;
        // _cv.notify_all();
    }

    bool pop(SmartFIFOElements<T>& elements, size_t nb_elements, std::optional<std::chrono::nanoseconds> const& timeout = std::nullopt) {
        // bool requires_diff = false;
        /* std::unique_lock<std::mutex> lck;
        if (!_cons_mutex.try_lock()) {
            begin = Globals::now();
            requires_diff = true;
            lck = std::move(std::unique_lock<std::mutex>(_cons_mutex));
            auto end = Globals::now();
            auto diff_begin = std::chrono::duration_cast<std::chrono::nanoseconds>(begin - Globals::_start_time).count();
            auto diff_wait = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
            printf("Waited on lock from %lu to %lu, duration = %lu\n", diff_begin, diff_begin + diff_wait, diff_wait);
        } else {
            lck = std::move(std::unique_lock<std::mutex>(_cons_mutex, std::adopt_lock));
        } */
        // std::unique_lock<std::mutex> lck(_cons_mutex);
        std::unique_lock<std::timed_mutex> lck(_cons_mutex, std::defer_lock);
        if (timeout) {
            if (!lck.try_lock_for(*timeout)) {
                return false;
            }
        } else {
            lck.lock();
        }
        
        bool has_next;
        while (_head->empty(has_next) && !terminated()) {
            if (has_next) {
                FIFOChunk<T>* next = _head->_next.load(std::memory_order_acquire);
                _head->destroy();
                _head = next;
            } else {
                // auto now = Globals::now();
                int count = _sem.wait(nb_elements, timeout);
                /* if (_log) {
                    auto then = Globals::now();
                    auto diff_begin = std::chrono::duration_cast<std::chrono::nanoseconds>(now - Globals::_start_time).count();
                    auto diff_wait = std::chrono::duration_cast<std::chrono::nanoseconds>(then - now).count();
                    TimestampData& td = timestamp_data[_log_n++];
                    td.begin = diff_begin;
                    td.end = diff_begin + diff_wait;
                    td.diff = diff_wait;
                    td.count = count;
                } */
                // printf("%p waited %d items\n", this, count);
                // _sem_data[1] += count;
                // sem_wait(&_sem);
                // _cv.wait(lck);
                if (count < 0) {
                    return false;
                }
            }
        }

        while (terminated() && _head->empty(has_next)) {
            if (!has_next) {
                elements.set(Ranges<T>());
                return true;
            } else {
                FIFOChunk<T>* next = _head->_next.load(std::memory_order_acquire);
                _head->destroy();
                _head = next;
            }
        }

        Ranges<T> pairs; // (nb_elements / _head->_size);
        bool done = false;

        while (nb_elements != 0 && !done) {
            pairs.push_back(_head->pop(nb_elements, done));

            /* Move head only if we are not done. Even if the current chunk is 
             * completely empty, the next push will take care of everything.
             */
            if (!done) {
                if (_head == _tail.load(std::memory_order_acquire)) {
                    elements.set(std::move(pairs));
                    return true;
                }
                FIFOChunk<T>* next = _head->_next.load(std::memory_order_acquire);
                if (next == nullptr) {
                    throw std::runtime_error("Oups ?");
                }
                _head->destroy();
                _head = next;

                
            }
        }

        elements.set(std::move(pairs));
        return true;
    }

    template<typename... Args>
    SmartFIFO<T>* view(bool producer, Args&&... args) {
        if (producer) {
            _nb_producers__done.fetch_add(1, std::memory_order_release);
        }
        return new SmartFIFO<T>(this, std::forward<Args>(args)...);
    }

    void add_producer() {
        _nb_producers__done.fetch_add(1, std::memory_order_release);
    }

    void add_producers(unsigned long long n) {
        _nb_producers__done.fetch_add(n, std::memory_order_release);
    }

    void terminate_producer() {
        // std::unique_lock<std::mutex> lck(_m);
        _nb_producers__done.fetch_add(1ULL << 32, std::memory_order_release);

        if (terminated()) {
            /* There is no need to post multiple times,because there can be at
             * most one consumer waiting on the semaphore. Consumers that arrive
             * later will see the FIFO as terminated and won't wait on the 
             * semaphore.
             */
             // sem_post(&_sem);
            // _cv.notify_all();
            // _sem.post(0);
            _sem.finish();
        }
    }

    bool terminated() const {
        size_t nb_producers__done = _nb_producers__done.load(std::memory_order_acquire);
        unsigned int nb_producers = nb_producers__done & ~(1U << 31);
        unsigned int nb_done = nb_producers__done >> 32;
        return nb_producers == nb_done;
    }

    void dump() const {
        _head->dump();
    }

    std::string const& description() const {
        return _description;
    }

private:
    bool _log;
    // 32 high bits are producers done, 32 low bits are producers.
    std::atomic<size_t> _nb_producers__done;
    FIFOChunk<T>* _head = nullptr;
    std::atomic<FIFOChunk<T>*> _tail;
    std::mutex _prod_mutex;
    std::timed_mutex _cons_mutex;
    // std::mutex _m;
    // std::condition_variable _cv;
    // sem_t _sem;
    SmartFIFOSemaphore _sem;
    std::string _description;
    // std::array<size_t, 2>& _sem_data;
};


/* template<typename T>
using SmartFIFO = typename SmartFIFOImpl<T>::smart_fifo; */
