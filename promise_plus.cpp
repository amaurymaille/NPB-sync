#include <cassert>

#include "promise_plus.h"

PromisePlusBase::PromisePlusBase() {
    _max_index = -1;
    _wait_mode = DEFAULT_WAIT_MODE;
    _ready_index.store(-1, std::memory_order_release);
}

PromisePlusBase::PromisePlusBase(int max_index, PromisePlusWaitMode wait_mode) {
    _max_index = max_index;
    _wait_mode = wait_mode;
    _ready_index.store(-1, std::memory_order_release);

    init_locks();
}

void PromisePlusBase::set_max_index(int max_index) {
    bool has_changed = max_index > _max_index;

    _max_index = max_index;
    if (has_changed) {
        init_locks();
    }
}

void PromisePlusBase::init_locks() {
    for (int i = 0; i < _max_index; ++i)
        _locks[i];
}

PromisePlus<void>::PromisePlus() : PromisePlusBase() {
    
}

PromisePlus<void>::PromisePlus(int max_index, PromisePlusWaitMode wait_mode) : PromisePlusBase(max_index, wait_mode) {
    
}

void PromisePlus<void>::get(int index) {
    if (_wait_mode == PromisePlusWaitMode::ACTIVE) {
        while (!(_ready_index.load(std::memory_order_acquire) >= index))
            ;
    } else {
        std::unique_lock<std::mutex> lck(_locks[index].first);
        while (!(_ready_index.load(std::memory_order_acquire) >= index))
            _locks[index].second.wait(lck);
    }
}

void PromisePlus<void>::get_slice(int begin, int end, int step) {
    (void)begin;
    (void)step;

    get(end);
}

void PromisePlus<void>::set(int index) {
    assert(_ready_index.load(std::memory_order_acquire) < index);

    _ready_index.store(index, std::memory_order_release);

    if (_wait_mode == PromisePlusWaitMode::PASSIVE) {
        // If index is 0, and someone sets it to 2, we need to release threads
        // waiting for 1 and 2
        for (int i = 0; i <= index; ++i) {
            std::unique_lock<std::mutex> lck(_locks[index].first);
            _locks[index].second.notify_all();
        }
    }
}
