#include <cassert>

#include <stdexcept>
#include <sstream>

#include "promises/static_step_promise.h"

StaticStepPromiseBase::StaticStepPromiseBase(int nb_values, unsigned int step) : _step(step) {
    assert(step != 0);
    _current_index_strong.store(-1, std::memory_order_relaxed);
    _current_index_weak = -1;

#ifndef NDEBUG
    _current_index_internal_strong.store(-1, std::memory_order_relaxed);
    _current_index_internal_weak = -1;
#endif

    _wait_m.reset(new std::pair<std::mutex, std::condition_variable>[nb_values]);
    _finalized.store(false, std::memory_order_relaxed);
}

void StaticStepPromiseBase::assert_okay_index(int index, bool passive) {
#ifndef NDEBUG
    if ((passive && index <= _current_index_internal_weak) || 
         index <= _current_index_internal_strong.load(std::memory_order_acquire)) {
        std::stringstream str;
        str << "StaticStepPromise: index " << index << " already fulfiled" << std::endl;
        throw std::runtime_error(str.str());
    }

    if (_finalized.load(std::memory_order_acquire)) {
        std::stringstream str;
        str << "StaticStepPromise: promise has been finalized, can't set value at index " << index << std::endl;
        throw std::runtime_error(str.str());
    }
#endif 
}

bool StaticStepPromiseBase::ready_index(int index, bool passive) {
    return ((passive && _current_index_weak >= index) || 
            _current_index_strong.load(std::memory_order_acquire) >= index);
}

StaticStepPromise<void>::StaticStepPromise(int nb_values, unsigned int step, PromisePlusWaitMode wait_mode) : 
    PromisePlus<void>(nb_values, wait_mode), _base(nb_values, step) {

}

void StaticStepPromise<void>::get(int index) {
    if (_base.ready_index(index, this->passive()))
        return;

    if (this->passive()) {
        std::unique_lock<std::mutex> lck(_base._wait_m[index].first);
        while (_base._current_index_weak < index)
            _base._wait_m[index].second.wait(lck);
    } else {
        while (_base._current_index_strong.load(std::memory_order_acquire) < index)
            ;
    }
}

// STRONG ASSUMPTION : at most one thread in set() for all index values at the
// same time
void StaticStepPromise<void>::set(int index) {
    std::unique_lock<StaticStepSetMutex> lck(_base._set_m);

    _base.assert_okay_index(index, this->passive());

    if (this->passive()) {
        std::unique_lock<std::mutex> lock(_base._wait_m[index].first);
        if (index >= _base._current_index_weak + _base._step) {
            unsigned int old_index = _base._current_index_weak;
            _base._current_index_weak = index;
            for (int i = old_index; i <= index; ++i)
                _base._wait_m[i].second.notify_all();
        }

#ifndef NDEBUG
        _base._current_index_internal_weak = index;
#endif
    } else {
        if (index >= _base._current_index_strong.load(std::memory_order_acquire)) {
            _base._current_index_strong.store(index, std::memory_order_release);
        }

#ifndef NDEBUG
        _base._current_index_internal_strong = index;
#endif
    }
}

void StaticStepPromise<void>::set_final(int index) {
    std::unique_lock<StaticStepSetMutex> lck(_base._set_m);

    if (this->passive()) {
        std::unique_lock<std::mutex> lock(_base._wait_m[index].first);
        _base._current_index_weak = index;
#ifndef NDEBUG
        _base._current_index_internal_weak = index;
#endif
    } else {
        _base._current_index_strong.store(index, std::memory_order_release);
#ifndef NDEBUG
        _base._current_index_internal_strong.store(index, std::memory_order_release);
#endif
    }

    _base._finalized.store(true, std::memory_order_release);
}
