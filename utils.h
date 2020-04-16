#ifndef UTILS_H
#define UTILS_H

#include <atomic>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <tuple>

#include "defines.h"

struct timespec;

template<typename IntType>
class RandomGenerator {
public:
    template<typename... Args>
    RandomGenerator(Args&&... args) : _generator(std::random_device()()), _distribution(std::forward<Args>(args)...) {

    }

    IntType operator()() {
        return _distribution(_generator);
    }

private:
    std::mt19937 _generator;
    std::uniform_int_distribution<IntType> _distribution;
};

template<typename T> 
class ActivePromise {
public:
    ActivePromise() { 
        _moved.store(false, std::memory_order_release); 
        _ready.store(false, std::memory_order_release);
    }
    ActivePromise(ActivePromise<T> const&) = delete;
    ActivePromise(ActivePromise<T>&& other) {
        other._moved.store(true, std::memory_order_release);
        std::cout << "[Constructor] Moved promise " << &other << std::endl;

        _moved.store(false, std::memory_order_release);
        static std::mutex m;
        std::unique_lock<std::mutex> lck(m);
        _ready.store(other._ready.load(std::memory_order_acquire), std::memory_order_release);
    }

    template<typename Alloc>
    ActivePromise(std::allocator_arg_t a, Alloc alloc) {
        _moved.store(false, std::memory_order_release);
        _ready.store(false, std::memory_order_release);
    }

    ~ActivePromise() {
        /* if (!_ready.load(std::memory_order_acquire) && !_moved.load()) {
            std::cerr << "Broken promise, ABORT, ABORT !!!!" << std::endl;
            // exit(EXIT_FAILURE);
        } */
    }

    ActivePromise<T>& operator=(const ActivePromise<T>& rhs) = delete;

    ActivePromise<T>& operator=(ActivePromise<T>&& other) noexcept {
        other._moved.store(true, std::memory_order_release);
        std::cout << "[operator=] Moved promise " << &other << std::endl;

        _moved.store(false, std::memory_order_release);
        static std::mutex m;
        std::unique_lock<std::mutex> lck(m);
        _ready.store(other._ready.load(std::memory_order_acquire), std::memory_order_release);

        return *this;
    }

    void set_value(T const& v) {
        if (_moved.load(std::memory_order_acquire))
            throw std::runtime_error("Promise moved");

        if (_ready.load(std::memory_order_acquire))
            throw std::runtime_error("Promise already fulfilled");

        _value = v;
        _ready.store(true, std::memory_order_release);
    }

    void set_value(T&& v) {
        if (_moved.load(std::memory_order_acquire))
            throw std::runtime_error("Promise moved");

        if (_ready.load(std::memory_order_acquire))
            throw std::runtime_error("Promise already fulfilled");

        _value = std::move(v);
        _ready.store(true, std::memory_order_release);
    }

    ActivePromise<T>& get_future() {
        return *this;
    }

    T get() {
        if (_moved.load(std::memory_order_acquire))
            throw std::runtime_error("Promise moved");

        while (!_ready.load(std::memory_order_acquire))
            ;

        return _value;
    }

private:
    std::atomic<bool> _ready;
    T _value;
    std::atomic<bool> _moved;
};

template<>
class ActivePromise<void> {
public:
    ActivePromise() { 
        _moved.store(false, std::memory_order_release); 
        _ready.store(false, std::memory_order_release);
    }
    ActivePromise(ActivePromise<void> const&) = delete;
    ActivePromise(ActivePromise<void>&& other) {
        other._moved.store(true, std::memory_order_release);
        std::cout << "[Constructor<void>] Moved promise " << &other << std::endl;

        _moved.store(false, std::memory_order_release);
        static std::mutex m;
        std::unique_lock<std::mutex> lck(m);
        _ready.store(other._ready.load(std::memory_order_acquire), std::memory_order_release);

    }
    template<typename Alloc>
    ActivePromise(std::allocator_arg_t a, Alloc alloc) {
        _moved.store(false, std::memory_order_release);
        _ready.store(false, std::memory_order_release);
    }

    ~ActivePromise() {
        /* if (!_ready.load(std::memory_order_acquire) && !_moved.load()) {
            // std::cerr << "Broken promise, ABORT, ABORT !!!!" << std::endl;
            // exit(EXIT_FAILURE);
        } */
    }

    ActivePromise<void>& operator=(ActivePromise<void> const& other) = delete;

    ActivePromise<void>& operator=(ActivePromise<void>&& other) {
        other._moved.store(true, std::memory_order_release);
        std::cout << "[operator=<void>] Moved promise " << &other << std::endl;

        _moved.store(false, std::memory_order_release);
        static std::mutex m;
        std::unique_lock<std::mutex> lck(m);
        _ready.store(other._ready.load(std::memory_order_acquire), std::memory_order_release);

        return *this;
    }

    void set_value() {
        if (_moved.load(std::memory_order_acquire))
            throw std::runtime_error("Promise moved");

        if (_ready.load(std::memory_order_acquire))
            throw std::runtime_error("Promise already fulfilled");

        _ready.store(true, std::memory_order_release);
    }

    ActivePromise<void>& get_future() {
        return *this;
    }

    void get() {
        if (_moved.load(std::memory_order_acquire))
            throw std::runtime_error("Promise moved");

        while (!_ready.load(std::memory_order_acquire))
            ;
    }

private:
    std::atomic<bool> _ready;
    std::atomic<bool> _moved;
};

template<typename T>
class PromisePlus {
public:
    PromisePlus(int num_threads, int nb_values) {
        _ready_indexes.resize(num_threads);
        _values.resize(nb_values);
    }

    T& get(int thread_id, int index) {
#ifdef ACTIVE_PROMISES
        while (!(_ready_indexes[thread_id].load(std::memory_order_acquire) >= index))
            ;
#else
        std::unique_lock<std::mutex> lck(_locks[thread_id][index].first);
        while (!(_ready_indexes[thread_id].load(std::memory_order_acquire) >= index))
            _locks[thread_id][index].second.wait(lck);
#endif
        return _values[index];
    }

    std::unique_ptr<T[]> get_slice(int thread_id, int begin, int end, int step = 1) {
        T* values = new T[(end - begin) / step + 1];

#ifdef ACTIVE_PROMISES
        while (!(_ready_indexes[thread_id].load(std::memory_order_acquire) >= end))
            ;
#else
        std::unique_lock<std::mutex> lck(_locks[thread_id][end].first);
        while (!(_ready_indexes[thread_id].load(std::memory_order_acquire) >= end))
            _locks[thread_id][end].second.wait(lck);
#endif
        for (int i = begin; i < end; i += step)
            values[i] = _values[i];

        return std::unique_ptr<T[]>(values);
    }

    void set(int thread_id, int index, const T& value) {
        assert(_ready_indexes[thread_id].load(std::memory_order_acquire) < index);

        _values[index] = value;
        
#ifndef ACTIVE_PROMISES
        std::unique_lock<std::mutex> lck(_locks[thread_id][index].first);
#endif
        _ready_indexes[thread_id].store(index, std::memory_order_release);
#ifndef ACTIVE_PROMISES
        _locks[thread_id][index].second.notify_one();
#endif
    }

private:
    std::vector<std::atomic<int>> _ready_indexes;
    std::vector<T> _values;

#ifndef ACTIVE_PROMISES
    std::vector<std::map<int, std::pair<std::mutex, std::condition_variable>>> _locks;
#endif
};

template<>
class PromisePlus<void> {
public:
    PromisePlus() {

    }

    void get(int thread_id, int index) {
#ifdef ACTIVE_PROMISES
        while (!(_ready_indexes[thread_id].load(std::memory_order_acquire) >= index))
            ;
#else
        std::unique_lock<std::mutex> lck(_locks[thread_id][index].first);
        while (!(_ready_indexes[thread_id].load(std::memory_order_acquire) >= index))
            _locks[thread_id][index].second.wait(lck);
#endif
    }

    void get_slice(int thread_id, int begin, int end, int step) {
        (void)begin;
        (void)step;

        get(thread_id, end);
    }

    void set(int thread_id, int index) {
        assert(_ready_indexes[thread_id].load(std::memory_order_acquire) < index);

        _ready_indexes[thread_id].store(index, std::memory_order_release);
#ifndef ACTIVE_PROMISES
        _locks[thread_id][index].second.notify_one();
#endif
    }

private:
    std::map<int, std::atomic<int>> _ready_indexes;

#ifndef ACTIVE_PROMISES
    std::vector<std::map<int, std::pair<std::mutex, std::condition_variable>>> _locks;
#endif
}; 

template<typename R, typename Alloc>
struct std::uses_allocator<ActivePromise<R>, Alloc> : std::true_type { };

namespace Globals {
    extern RandomGenerator<unsigned int> sleep_generator;
    extern RandomGenerator<unsigned char> binary_generator;
}

template<typename T, typename R>
auto count_duration_cast(std::chrono::duration<R> const& tp) {
    return std::chrono::duration_cast<T>(tp).count();
}

size_t to1d(size_t w, size_t x, size_t y, size_t z);
std::tuple<size_t, size_t, size_t, size_t> to4d(size_t n);
void init_matrix(int* ptr);
void assert_okay_init(Matrix matrix);
std::string get_time_fmt(const char* fmt);
const char* get_time_fmt_cstr(const char* fmt);
const char* get_time_default_fmt();
void omp_debug();
uint64 clock_diff(const struct timespec*, const struct timespec*);

template<typename T, typename F>
std::optional<typename std::result_of<F(T const&)>::type> operator>>=(std::optional<T> const& lhs, F const& fn) {
    if (!lhs.has_value()) {
        return std::nullopt;
    } else {
        return std::make_optional(fn(*lhs));
    }
}

void assert_matrix_equals(Matrix lhs, Matrix rhs);

void init_start_matrix_once();
void init_from_start_matrix(Matrix);

void init_expected_matrix_once();

#endif /* UTILS_H */
