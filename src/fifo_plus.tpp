template<typename T>
FIFOPlus<T>::FIFOPlus(FIFOPlusPopPolicy policy, ThreadIdentifier* identifier, size_t n_producers) : _producers_data(identifier, n_producers), _pop_policy(policy) {

}

template<typename T>
// template<template<typename> typename Container>
// void FIFOPlus<T>::push(Container<T>&& elements) {
void FIFOPlus<T>::push(const T& value) {
    _producers_data->_inner_buffer.push(value);
    ++_producers_data->_n;

    if (_producers_data->_inner_buffer.size() < _producers_data->_n)
        return;

    std::unique_lock<std::mutex> lck(_m);

    if (_buffer.size() != 0) {
        if (_buffer.size() < _producers_data->_work_amount_threshold) {
            _transfer();
        }

        _producers_data->_n_no_work = 0;
        ++_producers_data->_n_with_work;

        if (_producers_data->_n_with_work >= _producers_data->_with_work_threshold) {
            _reconfigure();
        }
    } else {
        _producers_data->_n_with_work = 0;
        ++_producers_data->_n_no_work;

        _transfer();

        if (_producers_data->_n_no_work >= _producers_data->_no_work_threshold) {
            _reconfigure();
        }
    }
}

template<typename T>
template<template<typename> typename Container>
void FIFOPlus<T>::pop(Container<T>& target, size_t n) {
    switch (_pop_policy) {
    case FIFOPlusPopPolicy::POP_NO_WAIT:
        {
        std::unique_lock<std::mutex> lck(_m);
        target.reserve(n);
        int i = 0;
        for (; i < n && !_buffer.empty(); ++i) {
            target.push_back(std::move(_buffer.front()));
            _buffer.pop();
        }
        }
        break;

    case FIFOPlusPopPolicy::POP_WAIT:
        {
        std::unique_lock<std::mutex> lck(_m);
        while (_buffer.size() < n) {
            _cv.wait(lck);
        }
        target.reserve(n);
        for (int i = 0; i < n; ++i) {
            target.push_back(std::move(_buffer.front()));
            _buffer.pop();
        }
        }
        break;

    default:
        break;
    }
}

template<typename T>
template<template<typename> typename Container>
void FIFOPlus<T>::empty(Container<T>& target) {
    std::unique_lock<std::mutex> lck(_m);
    target.reserve(_buffer.size());
    while (!_buffer.empty()) {
        target.push_back(_buffer.front());
        _buffer.pop();
    }
}

template<typename T>
void FIFOPlus<T>::_transfer() {
    std::queue<T>& fifo = _producers_data->_inner_buffer;

    while (!fifo.empty()) {
        _buffer.push(std::move(fifo.front()));
        fifo.pop();
    }
}

template<typename T>
void FIFOPlus<T>::_reconfigure() {

}
