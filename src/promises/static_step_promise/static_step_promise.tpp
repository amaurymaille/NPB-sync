#include <utility>

template<typename T>
ActiveStaticStepPromise<T>::ActiveStaticStepPromise(int nb_values, unsigned int step) : 
    PromisePlus<void>(nb_values), _base(step) {

}

template<typename T>
PassiveStaticStepPromise<T>::PassiveStaticStepPromise(int nb_values, unsigned int step) : 
    PromisePlus<void>(nb_values), _base(step) {

}

template<typename T>
T& ActiveStaticStepPromise<T>::get(int index) {
    if (!_base.ready_index_weak(index)) {
        int ready_index = _base._current_index_strong.load(std::memory_order_acquire);
        while (ready_index < index)
            ready_index = _base._current_index_strong.load(std::memory_order_acquire);

        // Not sure...
        _base._common._current_index_weak = _base._current_index_strong.load(std::memory_order_acquire);
    }
}

template<typename T>
T& PassiveStaticStepPromise<T>::get(int index) {
    if (!_base.ready_index_weak(index)) {
        std::unique_lock<std::mutex> lck(_base._index_m);
        while (_base._current_index_strong > index)
            _base._index_c.wait(lck);

        *_base._common._current_index_weak = _base._current_index_strong;
    }
}

template<typename T>
void ActiveStaticStepPromise<T>::set(int index, const T& value) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_value[index] = value;
    // In THEORY : thread T + 1 will always perform a get before a set (at least
    // in our synchronization pattern). get already has a strong memory ordering
    // so we should be able to read the proper value even if we use a relaxed 
    // memory ordering. 
    if (index - _base._current_index_strong.load(std::memory_order_acquire) >= 
        _base._common._step) {
        _base._current_index_strong.store(index, std::memory_order_release);
    }
}

template<typename T>
void PassiveStaticStepPromise<T>::set(int index, const T& value) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_value[index] = value;

    if (index - _base._current_index_strong) {
        std::unique_lock<std::mutex> index_lck(_base._index_m);
        _base._current_index_strong = index;
        _base._index_c.notify_all();
    }
}

template<typename T>
void ActiveStaticStepPromise<T>::set(int index, T&& value) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_value[index] = std::move(value);
    // In THEORY : thread T + 1 will always perform a get before a set (at least
    // in our synchronization pattern). get already has a strong memory ordering
    // so we should be able to read the proper value even if we use a relaxed 
    // memory ordering. 
    if (index - _base._current_index_strong.load(std::memory_order_acquire) >= 
        _base._common._step) {
        _base._current_index_strong.store(index, std::memory_order_release);
    }
}

template<typename T>
void PassiveStaticStepPromise<T>::set(int index, T&& value) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_value[index] = std::move(value);

    if (index - _base._current_index_strong) {
        std::unique_lock<std::mutex> index_lck(_base._index_m);
        _base._current_index_strong = index;
        _base._index_c.notify_all();
    }
}

template<typename T>
void ActiveStaticStepPromise<T>::set_final(int index, const T& value) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_value[index] = value;
    // Should I use relaxed instead ?
    _base._current_index_strong.store(index, std::memory_order_release);
}

template<typename T>
void PassiveStaticStepPromise<T>::set_final(int index, const T& value) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_value[index] = value;

    std::unique_lock<std::mutex> index_lck(_base._index_m);
    _base._current_index_strong = index;
    _base._index_c.notify_all();
}

template<typename T>
void ActiveStaticStepPromise<T>::set_final(int index, T&& value) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_value[index] = std::move(value);
    // Should I use relaxed instead ?
    _base._current_index_strong.store(index, std::memory_order_release);
}

template<typename T>
void PassiveStaticStepPromise<T>::set_final(int index, T&& value) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_value[index] = std::move(value);

    std::unique_lock<std::mutex> index_lck(_base._index_m);
    _base._current_index_strong = index;
    _base._index_c.notify_all();
}