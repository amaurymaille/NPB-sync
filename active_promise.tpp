#include <stdexcept>

template<typename T>
ActivePromise<T>::ActivePromise() { 
    _moved.store(false, std::memory_order_release); 
    _ready.store(false, std::memory_order_release);
    // _safe.store(true, std::memory_order_release);
}

template<typename T>
ActivePromise<T>::ActivePromise(ActivePromise<T>&& other) {
    /* bool safety = true;
    _safe.store(false, std::memory_order_release);
    while (!other._safe.strong_compare_exchange(safety, false, std::memory_order_acq_rel, std::memory_order_acquire))
        ;

    if (other._moved.load(std::memory_order_acquire)) {
        _moved.store(true, std::memory_order_release);
    }

    other._moved.store(true, std::memory_order_release);
    std::cout << "[Constructor] Moved promise " << &other << std::endl;

    _moved.store(false, std::memory_order_release);
    _ready.store(other._ready.load(std::memory_order_acquire), std::memory_order_release);

    other._safe.store(true, std::memory_order_release);
    _safe.store(true, std::memory_order_release); */

    (void)other;
    throw std::runtime_error("You shouldn't move ActivePromise");
}

template<typename T>
ActivePromise<T>::~ActivePromise() {
    /* if (!_ready.load(std::memory_order_acquire) && !_moved.load()) {
        std::cerr << "Broken promise, ABORT, ABORT !!!!" << std::endl;
        // exit(EXIT_FAILURE);
    } */
}

template<typename T>
ActivePromise<T>& ActivePromise<T>::operator=(ActivePromise<T>&& other) noexcept {
    /* other._moved.store(true, std::memory_order_release);
    std::cout << "[operator=] Moved promise " << &other << std::endl;

    _moved.store(false, std::memory_order_release);
    _ready.store(other._ready.load(std::memory_order_acquire), std::memory_order_release); */

    (void)other;
    throw std::runtime_error("You shouldn't move ActivePromise");
    return *this;
}

template<typename T>
void ActivePromise<T>::set_value(T const& v) {
    if (_moved.load(std::memory_order_acquire))
        throw std::runtime_error("Promise moved");

    if (_ready.load(std::memory_order_acquire))
        throw std::runtime_error("Promise already fulfilled");

    _value = v;
    _ready.store(true, std::memory_order_release);
}

template<typename T>
void ActivePromise<T>::set_value(T&& v) {
    if (_moved.load(std::memory_order_acquire))
        throw std::runtime_error("Promise moved");

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
    if (_moved.load(std::memory_order_acquire))
        throw std::runtime_error("Promise moved");

    while (!_ready.load(std::memory_order_acquire))
        ;

    return _value;
}