#include <stdexcept>

#include "active_promise.h"

// =============================================================================
// ActivePromise<void>

ActivePromise<void>::ActivePromise() { 
    _ready.store(false, std::memory_order_release);
}

ActivePromise<void>::~ActivePromise() {
    
}

void ActivePromise<void>::set_value() {
    if (_ready.load(std::memory_order_acquire))
        throw std::runtime_error("Promise already fulfilled");

    _ready.store(true, std::memory_order_release);
}

ActivePromise<void>& ActivePromise<void>::get_future() {
    return *this;
}

void ActivePromise<void>::get() {
    while (!_ready.load(std::memory_order_acquire))
        ;
}