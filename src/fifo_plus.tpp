template<typename T>
FIFOPlus<T>::FIFOPlus(FIFOPlusPopPolicy policy, ThreadIdentifier* identifier, size_t n_producers, size_t n_consumers) : _data(identifier, n_producers + n_consumers), _pop_policy(policy), _n_producers(n_producers) {

}

template<typename T>
// template<template<typename> typename Container>
// void FIFOPlus<T>::push(Container<T>&& elements) {
void FIFOPlus<T>::push(const T& value, bool reconfigure) {
    _data->_inner_buffer.push(value);

    // WARNING: deadlock possible. I guess we need a finalizer (ProdConsData::transfer ?).
#pragma message "This thing is probably going to deadlock one day! Use finalizers!"
    if (_data->_inner_buffer.size() < _data->_n)
        return;

    std::unique_lock<std::mutex> lck(_m);

    if (_buffer.size() != 0) {
        if (_buffer.size() < _data->_work_amount_threshold) {
            _transfer();
        }

        _data->_n_no_work = 0;
        ++_data->_n_with_work;

        if (_data->_n_with_work >= _data->_with_work_threshold && reconfigure) {
            _reconfigure(ReconfigureReason::WORK);
        }
    } else {
        _data->_n_with_work = 0;
        ++_data->_n_no_work;

        _transfer();

        if (_data->_n_no_work >= _data->_no_work_threshold && reconfigure) {
            _reconfigure(ReconfigureReason::NO_WORK);
        }
    }
}

template<typename T>
void FIFOPlus<T>::push_immediate(const T& value, bool reconfigure) {
    _data->_inner_buffer.push(value);

    std::unique_lock<std::mutex> lck(_m);
    _transfer();
}

template<typename T>
void FIFOPlus<T>::pop(std::optional<T>& opt, bool reconfigure) {
    if (_data->_inner_buffer.empty()) {
        std::unique_lock<std::mutex> lck(_m);
        if (_buffer.empty()) {
            if (_pop_policy == FIFOPlusPopPolicy::POP_NO_WAIT)
                return;

            /* Maybe we should count how many times in a row the buffer was empty
             * once we ran out of work. If the buffer is always empty, maybe we
             * should work less ? It would mean that either other consumers are
             * consumming too much or that producers are too slow.
             *
             * On the other hand, if we consume N at once and then process each of
             * them, or process one by one N times... ? Is there a strong difference ?
             * Maybe we need vTune here...
             */
            _cv.wait(lck);
        }

        _reverse_transfer();
    }

    opt = std::move(_data->_inner_buffer.front());
    _data->_inner_buffer.pop();
}

/* template<typename T>
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
} */

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
void FIFOPlus<T>::_transfer(bool check_empty) {
    std::queue<T>& fifo = _data->_inner_buffer;

    while (!fifo.empty()) {
        _buffer.push(std::move(fifo.front()));
        fifo.pop();
    }

    _cv.notify_all();
}

template<typename T>
void FIFOPlus<T>::_reverse_transfer(bool check_empty) {
    std::queue<T>& fifo = _data->_inner_buffer;
    unsigned int n = _data->_n;

    for (int i = 0; i < n && !_buffer.empty(); ++i) {
        fifo.push(_buffer.front());
        _buffer.pop();
    }
}

template<typename T>
void FIFOPlus<T>::_reconfigure(ReconfigureReason reason) {
    switch (reason) {
    case ReconfigureReason::WORK: {
        float diff = _data->_n_with_work / _data->_with_work_threshold;
        if (diff < 1.f) {
            throw std::runtime_error("Cannot call _reconfigure when ratio is lower than 1");
        }
        // 
        break;
    }

    case ReconfigureReason::NO_WORK: {
        float diff = _data->_n_no_work / _data->_no_work_threshold;
        if (diff < 1.f) {
            throw std::runtime_error("Cannot call _reconfigure when ratio is lower than 1");
        }
        break;
    }

    default:
        break;
    }
}

template<typename T>
void FIFOPlus<T>::ProdConsData::transfer() {
    switch (_role) {
    case FIFORole::NONE:
        throw std::runtime_error("Cannot transfer when no role has been set");

    case FIFORole::PRODUCER:
        {
            std::unique_lock<std::mutex> lck(_m);
            _transfer(false);
        }
        break;

    case FIFORole::CONSUMER:
        _reverse_transfer(false);
        break;
    }
}
