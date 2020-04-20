#include <stdexcept>

#include "active_promise.h"

// =============================================================================
// ActivePromise<void>

ActivePromise<void>::ActivePromise() { 
    _moved.store(false, std::memory_order_release); 
    _ready.store(false, std::memory_order_release);
}

ActivePromise<void>::ActivePromise(ActivePromise<void>&& other) {
    /* other._moved.store(true, std::memory_order_release);
    std::cout << "[Constructor<void>] Moved promise " << &other << std::endl;

    _moved.store(false, std::memory_order_release);
    static std::mutex m;
    std::unique_lock<std::mutex> lck(m);
    _ready.store(other._ready.load(std::memory_order_acquire), std::memory_order_release); */

    (void)other;
    throw std::runtime_error("You shouldn't move ActivePromise");
}

ActivePromise<void>::~ActivePromise() {
    /* if (!_ready.load(std::memory_order_acquire) && !_moved.load()) {
        // std::cerr << "Broken promise, ABORT, ABORT !!!!" << std::endl;
        // exit(EXIT_FAILURE);
    } */
}

ActivePromise<void>& ActivePromise<void>::operator=(ActivePromise<void>&& other) {
    /* other._moved.store(true, std::memory_order_release);
    std::cout << "[operator=<void>] Moved promise " << &other << std::endl;

    _moved.store(false, std::memory_order_release);
    static std::mutex m;
    std::unique_lock<std::mutex> lck(m);
    _ready.store(other._ready.load(std::memory_order_acquire), std::memory_order_release); */

    (void)other;
    throw std::runtime_error("You shouldn't move ActivePromise");
    return *this;
}

void ActivePromise<void>::set_value() {
    if (_moved.load(std::memory_order_acquire))
        throw std::runtime_error("Promise moved");

    if (_ready.load(std::memory_order_acquire))
        throw std::runtime_error("Promise already fulfilled");

    _ready.store(true, std::memory_order_release);
}

ActivePromise<void>& ActivePromise<void>::get_future() {
    return *this;
}

void ActivePromise<void>::get() {
    if (_moved.load(std::memory_order_acquire))
        throw std::runtime_error("Promise moved");

    while (!_ready.load(std::memory_order_acquire))
        ;
}