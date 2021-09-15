#pragma once

#include <semaphore.h>

#include <map>
#include <tuple>
#include <vector>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>

#include "utils.h"

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

template<typename T>
class FIFOChunk {
public:
    friend SmartFIFOElements<T>;
    friend SmartFIFOImpl<T>;
    friend SmartFIFO<T>;

    FIFOChunk(size_t size) : _size(size), _elements(new T[size]) {
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
    SmartFIFO(SmartFIFOImpl<T2>* fifo, size_t step) : _fifo(fifo), _step(step), _elements(Ranges<T2>()), _chunk(step) {
        
    }

    template<typename T3>
    decay_enable_if_t<T2, T3, size_t> push(T3&& value) {
        if (_over) {
            throw std::runtime_error("Trying to push in terminated FIFO !");
        }

        FIFOChunk<T2>* next = _chunk.push(std::forward<T3>(value));
        if (next) {
            _fifo->push_chunk(&_chunk);
            _chunk.reset(_step);
            return _step;
        } else {
            return 0;
        }
    }

    std::tuple<bool, size_t> pop_copy(std::optional<T2>& opt) {
        auto [valid, nb_elements, prepared] = prepare_elements();
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

        return std::make_tuple(valid && nb_elements != 0, nb_elements);
    }

    std::tuple<bool, size_t> pop(std::optional<T2*>& opt) {
        auto [valid, nb_elements, prepared] = prepare_elements();
        if (!prepared) {
            opt = std::nullopt;
        } else {
            opt = _elements.next();
        }

        return std::make_tuple(valid && nb_elements != 0, nb_elements);
    }

    void terminate_producer() {
        _chunk.freeze();
        _fifo->push_chunk(&_chunk);
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
    std::tuple<bool, size_t, bool> prepare_elements() {
        if (_elements.empty()) {
            _fifo->pop(_elements, _step);
            
            if (_elements.empty()) {
                return std::make_tuple(true, 0, false);
            } else {
                return std::make_tuple(true, _elements.size(), true);
            }
        } else if (!_elements.has_next()) {
            _elements.clear();
            _fifo->pop(_elements, _step);

            if (_elements.empty()) {
                return std::make_tuple(true, 0, false);
            } else {
                return std::make_tuple(true, _elements.size(), true);
            }
        } 

        return std::make_tuple(false, _elements.size(), true);
    }
    
    SmartFIFOImpl<T2>* _fifo;
    size_t _step;
    size_t _nb_elements = 0;
    SmartFIFOElements<T2> _elements;
    FIFOChunk<T2> _chunk;
    bool _over = false;
};

template<typename T>
class SmartFIFOImpl {
public:
    
    typedef SmartFIFO<T> smart_fifo;

public:
    SmartFIFOImpl(size_t chunk_size) : _chunk_size(chunk_size) {
        _tail.store(new FIFOChunk<T>(chunk_size), std::memory_order_relaxed);
        _head = _tail;
        _nb_producers__done.store(0, std::memory_order_relaxed);
        sem_init(&_sem, 0, 0);
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

    SmartFIFO<T> get_proxy(size_t step) {
        return SmartFIFO(this, step);
    }

    template<typename T2>
    decay_enable_if_t<T, T2, void> push(T2&& element) {
        std::unique_lock<std::mutex> lck(_prod_mutex);
        FIFOChunk<T>* tail = _tail.load(std::memory_order_relaxed);
        FIFOChunk<T>* next = tail->push(std::move(element));
        if (next) {
            _tail.store(next, std::memory_order_release);
        }

        int waiting;
        sem_getvalue(&_sem, &waiting);
        if (waiting <= 0) {
            sem_post(&_sem);
        }
    }
    
    void push_chunk(FIFOChunk<T>* chunk) {
        std::unique_lock<std::mutex> lck(_prod_mutex);
        FIFOChunk<T>* tail = _tail.load(std::memory_order_relaxed);
        // std::unique_lock<std::mutex> lck(_m);
        if (tail->_next) {
            throw std::runtime_error("Inconsistency detected: _tail isn't the tail");
        }

        FIFOChunk<T>* next = new FIFOChunk<T>(chunk);
        tail->freeze();
        tail->set_next(next);

        while (FIFOChunk<T>* n = next->_next.load(std::memory_order_acquire)) {
            next = n;
        }
        _tail.store(next, std::memory_order_release);

        int waiting;
        sem_getvalue(&_sem, &waiting);
        if (waiting <= 0) {
            sem_post(&_sem);
        }
        // _cv.notify_all();
    }

    void pop(SmartFIFOElements<T>& elements, size_t nb_elements) {
        std::unique_lock<std::mutex> lck(_cons_mutex);
        // std::unique_lock<std::mutex> lck(_m);
        
        bool has_next;
        while (_head->empty(has_next) && !terminated()) {
            if (has_next) {
                FIFOChunk<T>* next = _head->_next.load(std::memory_order_acquire);
                _head->destroy();
                _head = next;
            } else {
                sem_wait(&_sem);
                // _cv.wait(lck);
            }
        }

        while (terminated() && _head->empty(has_next)) {
            if (!has_next) {
                elements.set(Ranges<T>());
                return;
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
                    return;
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
             sem_post(&_sem);
            // _cv.notify_all();
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

private:
    // 32 high bits are producers done, 32 low bits are producers.
    std::atomic<size_t> _nb_producers__done;
    FIFOChunk<T>* _head = nullptr;
    std::atomic<FIFOChunk<T>*> _tail;
    std::mutex _prod_mutex;
    std::mutex _cons_mutex;
    size_t _chunk_size;
    // std::mutex _m;
    // std::condition_variable _cv;
    sem_t _sem;
};

/* template<typename T>
using SmartFIFO = typename SmartFIFOImpl<T>::smart_fifo; */
