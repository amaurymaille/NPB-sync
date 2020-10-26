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
#ifdef PROMISE_PLUS_DEBUG_COUNTERS
    auto& times = _base._common._set_times;
    times.resize(nb_values, 0);
#endif
}

PassiveStaticStepPromise<void>::PassiveStaticStepPromise(int nb_values, unsigned int step) : 
    PromisePlus<void>(nb_values), _base(step) {

}

void ActiveStaticStepPromise<void>::get(int index) {
    if (!_base.ready_index_weak(index)) {
#ifdef PROMISE_PLUS_DEBUG_COUNTERS
        ++_base._common._nb_get_strong;
#endif
        int ready_index = _base._current_index_strong.load(std::memory_order_acquire);

        while (ready_index < index) {
#ifdef PROMISE_PLUS_DEBUG_COUNTERS
            ++_base._common._nb_wait_loops;
#endif
            ready_index = _base._current_index_strong.load(std::memory_order_acquire);
        }

        _base._common._current_index_weak[omp_get_thread_num()] = _base._current_index_strong.load(std::memory_order_acquire);
        _base._common._current_index_weak[omp_get_thread_num()] = ready_index;
    } 
#ifdef PROMISE_PLUS_DEBUG_COUNTERS
    else {
        ++_base._common._nb_get_weak;
    }
#endif
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
    // std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);
#ifdef PROMISE_PLUS_DEBUG_COUNTERS
    struct timespec begin, end;
    clock_gettime(CLOCK_MONOTONIC, &begin);
#endif

    _base.assert_free_index_weak(index);

    if (_base._common._step == 1 || (index - _base._current_index_strong.load(std::memory_order_acquire) >= 
        _base._common._step)) {
        _base._current_index_strong.store(index, std::memory_order_release);
    }

#ifdef PROMISE_PLUS_DEBUG_COUNTERS
    clock_gettime(CLOCK_MONOTONIC, &end);
    _base._common._set_times[index] = clock_diff(&end, &begin);
#endif
}

void PassiveStaticStepPromise<void>::set(int index) {
    // std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    if (_base._common._step == 1 || (index - _base._current_index_strong >= _base._common._step)) {
        std::unique_lock<std::mutex> index_lck(_base._index_m);
        _base._current_index_strong = index;
        _base._index_c.notify_all();
    }
}

void ActiveStaticStepPromise<void>::set_final(int index) {
    // std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    _base._current_index_strong.store(index, std::memory_order_release);
    // _base._common._current_index_weak[omp_get_thread_num()] = index;
}

void PassiveStaticStepPromise<void>::set_final(int index) {
    // std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    std::unique_lock<std::mutex> index_lck(_base._index_m);
    _base._current_index_strong = index;
    _base._index_c.notify_all();
    // _base._common._current_index_weak[omp_get_thread_num()] = index;
}
