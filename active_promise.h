#ifndef ACTIVE_PROMISE_H
#define ACTIVE_PROMISE_H

#include <atomic>
#include <memory>
#include <type_traits>

template<typename T> 
class ActivePromise {
public:
    ActivePromise();
    ActivePromise(ActivePromise<T> const&) = delete;
    ActivePromise(ActivePromise<T>&& other);

    template<typename Alloc>
    ActivePromise(std::allocator_arg_t a, Alloc alloc) {
        _moved.store(false, std::memory_order_release);
        _ready.store(false, std::memory_order_release);
        // _safe.store(true, std::memory_order_release);
    }

    ~ActivePromise();

    ActivePromise<T>& operator=(const ActivePromise<T>& rhs) = delete;
    ActivePromise<T>& operator=(ActivePromise<T>&& other) noexcept;

    void set_value(T const& v);
    void set_value(T&& v);
    
    ActivePromise<T>& get_future();
    T get();

private:
    std::atomic<bool> _ready;
    T _value;
    std::atomic<bool> _moved;
    // std::atomic<bool> _safe;
};

template<>
class ActivePromise<void> {
public:
    ActivePromise();
    ActivePromise(ActivePromise<void> const&) = delete;
    ActivePromise(ActivePromise<void>&& other);

    template<typename Alloc>
    ActivePromise(std::allocator_arg_t a, Alloc alloc) {
        _moved.store(false, std::memory_order_release);
        _ready.store(false, std::memory_order_release);
    }

    ~ActivePromise();

    ActivePromise<void>& operator=(ActivePromise<void> const& other) = delete;
    ActivePromise<void>& operator=(ActivePromise<void>&& other);

    void set_value();

    ActivePromise<void>& get_future();
    void get();

private:
    std::atomic<bool> _ready;
    std::atomic<bool> _moved;
};

template<typename R, typename Alloc>
struct std::uses_allocator<ActivePromise<R>, Alloc> : std::true_type { };

#include "active_promise.tpp"

#endif // ACTIVE_PROMISE_H