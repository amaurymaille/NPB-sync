#include <utility>

template<typename T>
ActiveStaticStepPromise<T>::ActiveStaticStepPromise(int nb_values, unsigned int max_index, unsigned int step) : 
    PromisePlus<T>(nb_values, max_index), _base(step) {
}

template<typename T>
PassiveStaticStepPromise<T>::PassiveStaticStepPromise(int nb_values, unsigned int max_index, unsigned int step) : 
    PromisePlus<T>(nb_values, max_index), _base(step) {

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
    // std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_values[index] = value;
    
    if (_base._common._step == 1 || (index - _base._current_index >= _base._common._step)) {
        _base._current_index = index;
        _base._current_index_strong.store(index, std::memory_order_release);
    }
}

template<typename T>
void PassiveStaticStepPromise<T>::set(int index, const T& value) {
    // std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_values[index] = value;

    if (index - _base._current_index_strong) {
        std::unique_lock<std::mutex> index_lck(_base._index_m);
        _base._current_index_strong = index;
        _base._index_c.notify_all();
    }
}

template<typename T>
void ActiveStaticStepPromise<T>::set(int index, T&& value) {
    // std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_values[index] = std::move(value);
    
    if (_base._common._step == 1 || (index - _base._current_index >= _base._common._step)) {
        _base._current_index = index;
        _base._current_index_strong.store(index, std::memory_order_release);
    }
}

template<typename T>
void PassiveStaticStepPromise<T>::set(int index, T&& value) {
    // std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_values[index] = std::move(value);

    if (index - _base._current_index_strong) {
        std::unique_lock<std::mutex> index_lck(_base._index_m);
        _base._current_index_strong = index;
        _base._index_c.notify_all();
    }
}

template<typename T>
void ActiveStaticStepPromise<T>::set_immediate(int index, const T& value) {
    // std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_values[index] = value;
    
    _base._current_index = index;
    _base._current_index_strong.store(index, std::memory_order_release);
}

template<typename T>
void PassiveStaticStepPromise<T>::set_immediate(int index, const T& value) {
    // std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_values[index] = value;

    std::unique_lock<std::mutex> index_lck(_base._index_m);
    _base._current_index_strong = index;
    _base._index_c.notify_all();
}

template<typename T>
void ActiveStaticStepPromise<T>::set_immediate(int index, T&& value) {
    // std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_values[index] = std::move(value);
    
    _base._current_index_strong.store(index, std::memory_order_release);
}

template<typename T>
void PassiveStaticStepPromise<T>::set_immediate(int index, T&& value) {
    // std::unique_lock<StaticStepSetMutex> lck(_base._common._set_m);

    _base.assert_free_index_weak(index);

    this->_values[index] = std::move(value);

    std::unique_lock<std::mutex> index_lck(_base._index_m);
    _base._current_index_strong = index;
    _base._index_c.notify_all();
}

template<typename T>
StaticStepPromiseBuilder<T>::StaticStepPromiseBuilder(int nb_values, unsigned int max_index, unsigned int step, unsigned int n_threads) {
    _nb_values = nb_values;
    _max_index = max_index;
    _step = step;
    _n_threads = n_threads;
}

template<typename T>
PromisePlus<T>* StaticStepPromiseBuilder<T>::new_promise() const {
    ActiveStaticStepPromise<T>* ptr = new ActiveStaticStepPromise<T>(_nb_values, _max_index, _step);
    ptr->_base._common._current_index_weak.resize(_n_threads, -1);
    return ptr;
}


