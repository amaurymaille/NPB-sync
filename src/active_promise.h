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

    ~ActivePromise();

    ActivePromise<T>& operator=(const ActivePromise<T>& rhs) = delete;

    void set_value(T const& v);
    void set_value(T&& v);
    
    ActivePromise<T>& get_future();
    T get();

private:
    std::atomic<bool> _ready;
    T _value;
};

template<>
class ActivePromise<void> {
public:
    ActivePromise();
    ActivePromise(ActivePromise<void> const&) = delete;

    ~ActivePromise();

    ActivePromise<void>& operator=(ActivePromise<void> const& other) = delete;

    void set_value();

    ActivePromise<void>& get_future();
    void get();

private:
    std::atomic<bool> _ready;
};

#include "active_promise.tpp"

#endif // ACTIVE_PROMISE_H