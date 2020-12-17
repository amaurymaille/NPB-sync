#include "promises/naive_promise.h"

// -----------------------------------------------------------------------------
// Bases

NaivePromiseCommonBase::NaivePromiseCommonBase(int nb_values) {
    _set_m = std::make_unique<NaiveSetMutex[]>(nb_values);
}

PassiveNaivePromiseBase::PassiveNaivePromiseBase(int nb_values) : _common(nb_values) {
    _wait = std::make_unique<std::pair<std::mutex, std::condition_variable>[]>(nb_values);
    _ready = std::make_unique<bool[]>(nb_values);
}

bool PassiveNaivePromiseBase::ready_index_strong(int index) {
    std::unique_lock<std::mutex> lck(_wait[index].first);
    return _ready[index];
}

bool PassiveNaivePromiseBase::ready_index_weak(int index) {
    return _ready[index];
}

ActiveNaivePromiseBase::ActiveNaivePromiseBase(int nb_values) : _common(nb_values) {
    _ready = std::make_unique<std::atomic<bool>[]>(nb_values);
}

bool ActiveNaivePromiseBase::ready_index_strong(int index) {
    return _ready[index].load(std::memory_order_acquire);
}

bool ActiveNaivePromiseBase::ready_index_weak(int index) {
    return _ready[index].load(std::memory_order_relaxed);
}

// -----------------------------------------------------------------------------
// NaivePromise<void>

PassiveNaivePromise<void>::PassiveNaivePromise(int nb_values) : PromisePlus<void>(nb_values), _base(nb_values)  {

}

ActiveNaivePromise<void>::ActiveNaivePromise(int nb_values) : PromisePlus<void>(nb_values), _base(nb_values) {

}

void PassiveNaivePromise<void>::get(int index) {
    if (!_base.ready_index_strong(index)) {
        std::unique_lock<std::mutex> lck(_base._wait[index].first);
        while (!_base._ready[index])
            _base._wait[index].second.wait(lck);
    }
}

void ActiveNaivePromise<void>::get(int index) {
    if (!_base.ready_index_strong(index)) {
        while (!_base._ready[index].load(std::memory_order_acquire))
            ;
    }
}

void PassiveNaivePromise<void>::set(int index) {
    set_maybe_check(index, true);
}

void PassiveNaivePromise<void>::set_immediate(int index) {
    set_maybe_check(index, false);
}

void ActiveNaivePromise<void>::set(int index) {
    set_maybe_check(index, true);
}

void ActiveNaivePromise<void>::set_immediate(int index) {
    set_maybe_check(index, false);
}

void PassiveNaivePromise<void>::set_maybe_check(int index, bool check) {
    std::unique_lock<NaiveSetMutex> lock_s(_base._common._set_m[index]);

    if (check)
        _base.assert_free_index_strong(index);

    std::unique_lock<std::mutex> lck(_base._wait[index].first);
    _base._ready[index] = true;
    _base._wait[index].second.notify_all();
}

void ActiveNaivePromise<void>::set_maybe_check(int index, bool check) {
    std::unique_lock<NaiveSetMutex> lock_s(_base._common._set_m[index]);

    if (check)
        _base.assert_free_index_strong(index);

    _base._ready[index].store(true, std::memory_order_release);
}