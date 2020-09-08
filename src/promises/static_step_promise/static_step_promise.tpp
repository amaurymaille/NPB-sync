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
        _base._common._current_index_weak[omp_get_thread_num()] = _base._current_index_strong.load(std::memory_order_acquire);
    }

    return this->_values[index];
}

template<typename T>
T& PassiveStaticStepPromise<T>::get(int index) {
    if (!_base.ready_index_weak(index)) {
        std::unique_lock<std::mutex> lck(_base._index_m);
        while (_base._current_index_strong > index)
            _base._index_c.wait(lck);

        _base._common._current_index_weak[omp_get_thread_num()] = _base._current_index_strong;
    }

    return this->_values[index];
}

template<typename T>
void ActiveStaticStepPromise<T>::set(int index, const T& value) {
    std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_value[index] = value;
    
    if (_base._common._step == 1 || (index - _base._current_index_strong.load(std::memory_order_acquire) >= 
        _base._common._step)) {
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
    
    if (_base._common._step == 1 || (index - _base._current_index_strong.load(std::memory_order_acquire) >= 
        _base._common._step)) {
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

template<typename T>
StaticStepPromiseBuilder<T>::StaticStepPromiseBuilder(int nb_values, unsigned int step, unsigned int n_threads,
                                                      PromisePlusWaitMode wait_mode) {
    _nb_values = nb_values;
    _step = step;
    _n_threads = n_threads;
    _wait_mode = wait_mode;
}

template<typename T>
PromisePlus<T>* StaticStepPromiseBuilder<T>::new_promise() const {
    if (_wait_mode == PromisePlusWaitMode::ACTIVE) {
        ActiveStaticStepPromise<T>* ptr = new ActiveStaticStepPromise<T>(_nb_values, _step);
        ptr->_base._common._current_index_weak.resize(_n_threads, -1);
        return ptr;
    } else {
        PassiveStaticStepPromise<T>* ptr = new PassiveStaticStepPromise<T>(_nb_values, _step);
        ptr->_base._common._current_index_weak.resize(_n_threads, -1);
        return ptr;
    }
}

