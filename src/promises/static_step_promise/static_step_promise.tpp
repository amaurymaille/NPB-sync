#include <utility>

template<typename T>
StaticStepPromise<T>::StaticStepPromise(int nb_values, unsigned int step, PromisePlusWaitMode wait_mode) : 
    PromisePlus<T>(nb_values), _base(nb_values, step) {

}

template<typename T>
T& StaticStepPromise<T>::get(int index) {
    if (_base.ready_index(index, this->passive()))
        return this->_values[index];

    if (this->passive()) {
        std::unique_lock<std::mutex> lck(_base._index_m);
        while (_base._current_index_weak < index)
            _base._cond_m.wait(lck);
    } else {
        while (_base._current_index_strong.load(std::memory_order_acquire) < index)
            ;
    }

    return this->_values[index];
}

// STRONG ASSUMPTION : at most one thread in set() for all index values at the
// same time
template<typename T>
void StaticStepPromise<T>::set(int index, T&& value) {
    std::unique_lock<StaticStepSetMutex> lck(_base._set_m);

    _base.assert_okay_index(index, this->passive());

    this->_values[index] = std::move(value);

    if (this->passive()) {
        {
            if (index >= _base._current_index_weak + _base._step) {
                {
                    std::unique_lock<std::mutex> lock(_base._index_m);
                    _base._current_index_weak = index;
                }

                _base._cond_m.notify_all();
            }
        } 

#ifndef NDEBUG
        _base._current_index_internal_weak = index;
#endif
    } else {
        if (index >= _base._current_index_strong.load(std::memory_order_acquire)) {
            if (index + 1 % _base._step == 0) {
                _base._current_index_strong.store(index, std::memory_order_release);
            } else {
                _base._current_index_strong.store(index - (index % _base._step) - 1, std::memory_order_release);
            }
        }

#ifndef NDEBUG
        _base._current_index_internal_strong = index;
#endif
    }
}

// STRONG ASSUMPTION : at most one thread in set() for all index values at the
// same time
template<typename T>
void StaticStepPromise<T>::set(int index, const T& value) {
    std::unique_lock<StaticStepSetMutex> lck(_base._set_m);

    _base.assert_okay_index(index, this->passive());

    this->_values[index] = value;

    if (this->passive()) {
        {
            if (index >= _base._current_index_weak + _base._step) {
                {
                    std::unique_lock<std::mutex> lock(_base._index_m);
                    _base._current_index_weak = index;
                }

                _base._cond_m.notify_all();
            }
        }
#ifndef NDEBUG
        _base._current_index_internal_weak = index;
#endif
    } else {
        if (index >= _base._current_index_strong.load(std::memory_order_acquire)) {
            if (index + 1 % _base._step == 0) {
                _base._current_index_strong.store(index, std::memory_order_release);
            } else {
                _base._current_index_strong.store(index - (index % _base._step) - 1, std::memory_order_release);
            }
        }

#ifndef NDEBUG
        _base._current_index_internal_strong = index;
#endif
    }
}

template<typename T>
void StaticStepPromise<T>::set_final(int index, T&& value) {
    std::unique_lock<StaticStepSetMutex> lck(_base._set_m);

    this->_values[index] = std::move(value);

    if (this->passive()) {
        {
            std::unique_lock<std::mutex> lock(_base._index_m);
            _base._current_index_weak = index;
        }
        _base._cond_m.notify_all();
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

template<typename T>
void StaticStepPromise<T>::set_final(int index, const T& value) {
    std::unique_lock<StaticStepSetMutex> lck(_base._set_m);

    _base.assert_okay_index(index, this->passive());

    this->_values[index] = value;

    if (this->passive()) {
        {
            std::unique_lock<std::mutex> lock(_base._index_m);
            _base._current_index_weak = index;
        }
        _base._cond_m.notify_all();

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
