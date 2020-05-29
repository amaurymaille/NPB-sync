#include <cassert>

#include <stdexcept>
#include <sstream>

#include "promises/static_step_promise.h"

// -----------------------------------------------------------------------------
// Bases

StaticStepPromiseCommonBase::StaticStepPromiseCommonBase(unsigned int step) : _step(step) {
    assert(step != 0);
}


ActiveStaticStepPromiseBase::ActiveStaticStepPromiseBase(unsigned int step) : _common(step) {

}

bool ActiveStaticStepPromiseBase::ready_index_strong(int index) {
    return _current_index_strong.load(std::memory_order_release) >= index;
}

bool ActiveStaticStepPromiseBase::ready_index_weak(int index) {
    if (!(*_common._current_index_weak))
        _common._current_index_weak.reset(new int(-1));
        
    return *_common._current_index_weak >= index;
}


PassiveStaticStepPromiseBase::PassiveStaticStepPromiseBase(unsigned int step) : _common(step) {

}

bool PassiveStaticStepPromiseBase::ready_index_strong(int index) {
    std::unique_lock<std::mutex> lck(_index_m);
    return _current_index_strong >= index;
}

bool PassiveStaticStepPromiseBase::ready_index_weak(int index) {
    if (!(*_common._current_index_weak))
        _common._current_index_weak.reset(new int(-1));
        
    return *_common._current_index_weak >= index;
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

        // Not sure...
        *_base._common._current_index_weak = _base._current_index_strong.load(std::memory_order_acquire);
    }
}

void PassiveStaticStepPromise<void>::get(int index) {
    if (!_base.ready_index_weak(index)) {
        std::unique_lock<std::mutex> lck(_base._index_m);
        while (_base._current_index_strong > index)
            _base._index_c.wait(lck);

        *_base._common._current_index_weak = _base._current_index_strong;
    }
}

void ActiveStaticStepPromise<void>::set(int index) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    // In THEORY : thread T + 1 will always perform a get before a set (at least
    // in our synchronization pattern). get already has a strong memory ordering
    // so we should be able to read the proper value even if we use a relaxed 
    // memory ordering. 
    if (index - _base._current_index_strong.load(std::memory_order_acquire) >= 
        _base._common._step) {
        _base._current_index_strong.store(index, std::memory_order_release);
    }
}

void PassiveStaticStepPromise<void>::set(int index) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    if (index - _base._current_index_strong) {
        std::unique_lock<std::mutex> index_lck(_base._index_m);
        _base._current_index_strong = index;
        _base._index_c.notify_all();
    }
}

void ActiveStaticStepPromise<void>::set_final(int index) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    // Should I use relaxed instead ?
    _base._current_index_strong.store(index, std::memory_order_release);
}

void PassiveStaticStepPromise<void>::set_final(int index) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    std::unique_lock<std::mutex> index_lck(_base._index_m);
    _base._current_index_strong = index;
    _base._index_c.notify_all();
}