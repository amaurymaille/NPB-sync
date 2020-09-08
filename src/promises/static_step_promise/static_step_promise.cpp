#include <cassert>

#include <stdexcept>
#include <sstream>

#include <omp.h>

#include "dynamic_defines.h"
#include "promises/static_step_promise.h"

// -----------------------------------------------------------------------------
// Bases

StaticStepPromiseCommonBase::StaticStepPromiseCommonBase(unsigned int step) : _step(step) {
    assert(step != 0);
}


ActiveStaticStepPromiseBase::ActiveStaticStepPromiseBase(unsigned int step) : _common(step) {
    _current_index_strong.store(-1, std::memory_order_release);
}

bool ActiveStaticStepPromiseBase::ready_index_strong(int index) {
    return _current_index_strong.load(std::memory_order_release) >= index;
}

bool ActiveStaticStepPromiseBase::ready_index_weak(int index) {
    return _common._current_index_weak[omp_get_thread_num()] >= index;
}


PassiveStaticStepPromiseBase::PassiveStaticStepPromiseBase(unsigned int step) : _common(step) {
    _current_index_strong = -1;
}

bool PassiveStaticStepPromiseBase::ready_index_strong(int index) {
    std::unique_lock<std::mutex> lck(_index_m);
    return _current_index_strong >= index;
}

bool PassiveStaticStepPromiseBase::ready_index_weak(int index) {
    return _common._current_index_weak[omp_get_thread_num()] >= index;
}

// -----------------------------------------------------------------------------
// StaticStepPromise<void>

ActiveStaticStepPromise<void>::ActiveStaticStepPromise(int nb_values, unsigned int step) : 
    PromisePlus<void>(nb_values), _base(step) {

}

PassiveStaticStepPromise<void>::PassiveStaticStepPromise(int nb_values, unsigned int step) : 
    PromisePlus<void>(nb_values), _base(step) {

}

void ActiveStaticStepPromise<void>::get(int index) {
    if (!_base.ready_index_weak(index)) {
        int ready_index = _base._current_index_strong.load(std::memory_order_acquire);
        while (ready_index < index)
            ready_index = _base._current_index_strong.load(std::memory_order_acquire);

        _base._common._current_index_weak[omp_get_thread_num()] = _base._current_index_strong.load(std::memory_order_acquire);
    }
}

void PassiveStaticStepPromise<void>::get(int index) {
    if (!_base.ready_index_weak(index)) {
        std::unique_lock<std::mutex> lck(_base._index_m);
        while (_base._current_index_strong < index)
            _base._index_c.wait(lck);

        _base._common._current_index_weak[omp_get_thread_num()] = _base._current_index_strong;
    }
}

void ActiveStaticStepPromise<void>::set(int index) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    if (_base._common._step == 1 || (index - _base._current_index_strong.load(std::memory_order_acquire) >= 
        _base._common._step)) {
        _base._current_index_strong.store(index, std::memory_order_release);
    }
}

void PassiveStaticStepPromise<void>::set(int index) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    if (_base._common._step == 1 || (index - _base._current_index_strong >= _base._common._step)) {
        std::unique_lock<std::mutex> index_lck(_base._index_m);
        _base._current_index_strong = index;
        _base._index_c.notify_all();
    }
}

void ActiveStaticStepPromise<void>::set_final(int index) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    _base._current_index_strong.store(index, std::memory_order_release);
}

void PassiveStaticStepPromise<void>::set_final(int index) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    std::unique_lock<std::mutex> index_lck(_base._index_m);
    _base._current_index_strong = index;
    _base._index_c.notify_all();
}
