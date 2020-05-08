#include <stdexcept>

template<typename T>
ActivePromise<T>::ActivePromise() { 
    _ready.store(false, std::memory_order_release);
}

template<typename T>
ActivePromise<T>::~ActivePromise() {
    
}

template<typename T>
void ActivePromise<T>::set_value(T const& v) {
    if (_ready.load(std::memory_order_acquire))
        throw std::runtime_error("Promise already fulfilled");

    _value = v;
    _ready.store(true, std::memory_order_release);
}

template<typename T>
void ActivePromise<T>::set_value(T&& v) {
    if (_ready.load(std::memory_order_acquire))
        throw std::runtime_error("Promise already fulfilled");

    _value = std::move(v);
    _ready.store(true, std::memory_order_release);
}

template<typename T>
ActivePromise<T>& ActivePromise<T>::get_future() {
    return *this;
}

template<typename T>
T ActivePromise<T>::get() {
    while (!_ready.load(std::memory_order_acquire))
        ;

    return _value;
}