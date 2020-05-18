#include "promises/naive_promise.h"

NaivePromiseBase::NaivePromiseBase(int nb_values, PromisePlusWaitMode wait_mode) {
    if (wait_mode == PromisePlusWaitMode::ACTIVE) {
        _ready_strong = std::make_unique<std::atomic<bool>[]>(nb_values);
    } else {
        _wait_m = std::make_unique<std::pair<std::mutex, std::condition_variable>[]>(nb_values);
        _ready_weak = std::make_unique<bool[]>(nb_values);
    }

    _set_m = std::make_unique<SetMutex[]>(nb_values);
}

NaivePromise<void>::NaivePromise(int nb_values, PromisePlusWaitMode wait_mode) :
    _base(nb_values, wait_mode) {
    
}

void NaivePromise<void>::get(int index) {
    if (ready_index(index)) {
        return;
    } else {
        if (this->passive()) {
            std::unique_lock<std::mutex> lck(_base._wait_m[index].first);
            while (!_base._ready_weak[index])
                _base._wait_m[index].second.wait(lck);
        } else {
            while (!_base._ready_strong[index].load(std::memory_order_acquire))
                ;
        }

        return; 
    }
}

void NaivePromise<void>::assert_free_index(int index) const {
#ifndef NDEBUG
    if (ready_index(index)) {
        std::stringstream str;
        str << "NaivePromise: index " << index << " already fulfiled" << std::endl;
        throw std::runtime_error(str.str());
    }
#endif
}

void NaivePromise<void>::set(int index) {
    std::unique_lock<SetMutex> lock_s(_base._set_m[index]);

    assert_free_index(index);

    if (this->passive()) {
        std::unique_lock<std::mutex> lck(_base._wait_m[index].first);
        _base._ready_weak[index] = true;
        _base._wait_m[index].second.notify_all();
    } else if (this->active()) {
        _base._ready_strong[index].store(true, std::memory_order_consume);
    }
}

void NaivePromise<void>::set_final(int index) {
    set(index);
}

bool NaivePromise<void>::ready_index(int index) const {
    return ((this->passive() && _base._ready_weak[index]) ||
            (this->active() && _base._ready_strong[index].load(std::memory_order_acquire)));
}
