#include "fifo_plus.h"

#include <cmath>
#include <iostream>
#include <sstream>

#ifdef FIFO_PLUS_TIMESTAMP_DATA
template<typename T>
FIFOPlus<T>::FIFOPlus(FIFOPlusPopPolicy policy, FIFOReconfigure reconfiguration_policy, ThreadIdentifier* identifier, size_t n_producers, size_t n_consumers, size_t history_size, std::string&& description, std::chrono::time_point<std::chrono::steady_clock> const& start_time) :
    _producer_events(history_size), _reconfigure_method(reconfiguration_policy), _consumer_events(history_size), _data(identifier, n_producers + n_consumers), _pop_policy(policy), _n_producers(n_producers), _description(std::move(description)), _start_time(start_time) {

}
#else
template<typename T>
FIFOPlus<T>::FIFOPlus(FIFOPlusPopPolicy policy, FIFOReconfigure reconfiguration_policy, ThreadIdentifier* identifier, size_t n_producers, size_t n_consumers, size_t history_size, std::string&& description) :
    _producer_events(history_size), _reconfigure_method(reconfiguration_policy), _consumer_events(history_size), _data(identifier, n_producers + n_consumers), _pop_policy(policy), _n_producers(n_producers), _description(std::move(description)) {

}
#endif

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

    if (_reconfigure_method == FIFOReconfigure::GRADIENT) {
        if (_buffer.size() != 0) {
            if (_buffer.size() < _data->_work_amount_threshold) {
                _data->_producer_events.push_back(ProducerEvents::PUSH_LOW);
                _producer_events.push_back(ProducerEvents::PUSH_LOW);
            } else {
                _data->_producer_events.push_back(ProducerEvents::PUSH_CONTENT);
                _producer_events.push_back(ProducerEvents::PUSH_CONTENT);
            }
            
            _data->_n_no_work = 0;
            ++_data->_n_with_work;

            if (_data->_n_with_work >= _data->_with_work_threshold && reconfigure) {
                _reconfigure_producer_gradient(ReconfigureReason::WORK);
            }
        } else {
            _data->_n_with_work = 0;
            ++_data->_n_no_work;

            _data->_producer_events.push_back(ProducerEvents::PUSH_EMPTY);
            _producer_events.push_back(ProducerEvents::PUSH_EMPTY);

            if (_data->_n_no_work >= _data->_no_work_threshold && reconfigure) {
                _reconfigure_producer_gradient(ReconfigureReason::NO_WORK);
            }
        } 

        _transfer();
    } else if (_reconfigure_method == FIFOReconfigure::PHASE) {
        if (_buffer.size() != 0) {
            if (_buffer.size() < _data->_work_amount_threshold) {
                _data->_producer_events.push_back(ProducerEvents::PUSH_LOW);
                _producer_events.push_back(ProducerEvents::PUSH_LOW);
            } else {
                _data->_producer_events.push_back(ProducerEvents::PUSH_CONTENT);
                _producer_events.push_back(ProducerEvents::PUSH_CONTENT);
            }
        } else {
            _data->_producer_events.push_back(ProducerEvents::PUSH_EMPTY);
            _producer_events.push_back(ProducerEvents::PUSH_EMPTY);
        }

        _transfer();
        _reconfigure_phase();
    }
}

template<typename T>
void FIFOPlus<T>::push_immediate(const T& value, bool reconfigure) {
    _data->_inner_buffer.push(value);

    std::unique_lock<std::mutex> lck(_m);
    
    _data->_producer_events.push_back(ProducerEvents::PUSH_IMMEDIATE);
    _producer_events.push_back(ProducerEvents::PUSH_IMMEDIATE);
    _transfer();
}

template<typename T>
void FIFOPlus<T>::pop(std::optional<T>& opt, bool reconfigure) {
    if (_data->_inner_buffer.empty()) {
        std::unique_lock<std::mutex> lck(_m);
        if (_buffer.empty()) {
            if (_pop_policy == FIFOPlusPopPolicy::POP_NO_WAIT) {
                _consumer_events.push_back(ConsumerEvents::POP_EMPTY_NW);
                return;
            } else {
                _consumer_events.push_back(ConsumerEvents::POP_EMPTY);
            }

            while (_buffer.empty() && !terminated()) {
                _data->_n_no_work++;
                _data->_n_with_work = 0;

                if (_data->_n_no_work >= _data->_no_work_threshold && reconfigure) {
                    _reconfigure_consumer_gradient(ReconfigureReason::NO_WORK);
                }

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

            if (_buffer.empty() && terminated()) {
                return;
            }
        } else {
            _consumer_events.push_back(ConsumerEvents::POP_CONTENT);

            _data->_n_no_work = 0;
            _data->_n_with_work++;

            if (_data->_n_with_work >= _data->_with_work_threshold && reconfigure) {
                _reconfigure_consumer_gradient(ReconfigureReason::WORK);
            }
        }

        // printf("[%p, queue %s] has %lu elements available, will take %lu\n", &(_data->_inner_buffer), _description.c_str(), _buffer.size(), _data->_n);
        _reverse_transfer();
        assert (_data->_inner_buffer.size() != 0);
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
    bool push = false;

#ifdef FIFO_PLUS_TIMESTAMP_DATA
    add_timestamp_data(Actions::PUSH, fifo.size());
#endif

    while (!fifo.empty()) {
        _buffer.push(std::move(fifo.front()));
        fifo.pop();
        push = true;
    }

    if (push)
        _cv.notify_one();
}

template<typename T>
void FIFOPlus<T>::_reverse_transfer(bool check_empty) {
    std::queue<T>& fifo = _data->_inner_buffer;
    unsigned int n = _data->_n;

#ifdef FIFO_PLUS_TIMESTAMP_DATA
    add_timestamp_data(Actions::POP, std::min((size_t)n, _buffer.size()));
#endif

    for (int i = 0; i < n && !_buffer.empty(); ++i) {
        fifo.push(_buffer.front());
        _buffer.pop();
    }
}

template<typename T>
void FIFOPlus<T>::_reconfigure_producer_gradient(ReconfigureReason reason, typename FIFOPlus<T>::Gradients in_gradient) {
    Gradients gradient = _producer_gradient(reason);

    switch (reason) {
    case ReconfigureReason::WORK: {
        float diff = _data->_n_with_work / _data->_with_work_threshold;
        if (diff < 1.f && in_gradient == COHERENT) {
            std::ostringstream stream;
            stream << "Cannot call _reconfigure_producer when ratio is lower than 1: " << diff;
            throw std::runtime_error(stream.str());
        }

        if (in_gradient == COHERENT) {
            switch (gradient) {
                case INCOHERENT:
                case FLUCTUATING:
                    _reconfigure_producer_gradient(ReconfigureReason::NO_WORK, gradient);
                    break;

                case COHERENT:
                    _data->_n *= _data->_increase_mult;
                    _data->_n_with_work = 0;
                    break;
            }
        } else {
            switch (in_gradient) {
                case FLUCTUATING:
                    _data->_n = _data->_n * (1 + _data->_increase_mult * 0.75 - 0.75);
                    break;

                case INCOHERENT:
                    _data->_n = _data->_n * (1 + _data->_increase_mult * 0.5 - 0.5);
                    break;

                default:
                    break;
            }
        }

        // If the multiplier is small enough, then we may end up forever blocked
        // at 1. Forcibly set the value to 2 to avoid this pitfall.
        if (_data->_n == 1) {
            _data->_n = 2;
        }
        break;
    }

    case ReconfigureReason::NO_WORK: {
        float diff = _data->_n_no_work / _data->_no_work_threshold;
        if (diff < 1.f && in_gradient == COHERENT) {
            std::ostringstream stream;
            stream << "Cannot call _reconfigure_producer when ratio is lower than 1: " << diff;
            throw std::runtime_error(stream.str());
        }

        if (in_gradient == COHERENT) {
            switch (gradient) {
                case INCOHERENT:
                case FLUCTUATING:
                    _reconfigure_producer_gradient(ReconfigureReason::WORK, gradient);
                    break;

                case COHERENT:
                    _data->_n *= _data->_decrease_mult;
                    // Corner case, cannot decrease below 1
                    if (_data->_n <= 0) {
                        _data->_n = 1;
                    }
                    _data->_n_no_work = 0;
            }
        } else {
            switch (in_gradient) {
                case FLUCTUATING:
                    _data->_n = _data->_n * (1 - _data->_decrease_mult * 0.25 + 0.25);
                    break;

                case INCOHERENT:
                    _data->_n = _data->_n * (1 - _data->_decrease_mult * 0.5 + 0.5);
                    break;

                default:
                    break;
            }

            if (_data->_n <= 1) {
                _data->_n = 1;
            }
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

template<typename T>
typename FIFOPlus<T>::Gradients FIFOPlus<T>::_producer_gradient(ReconfigureReason reason) const {
    std::vector<unsigned int> counts(ProducerEvents::MAX_PUSH, 0);
    auto populate_vector = [&](size_t limit) -> void {
        unsigned int min = std::min(_producer_events.size(), limit);
        unsigned int count = 0;
        for (typename boost::circular_buffer<ProducerEvents>::const_reverse_iterator iter = _producer_events.rbegin(); iter != _producer_events.rend() && count < min; ++iter) {
            ++counts[*iter];
            ++count;
        }
    };

    switch (reason) {
    case ReconfigureReason::WORK: {
        populate_vector(_data->_no_work_threshold);
        // Give a lower weight to critical pushs
        float diff = counts[ProducerEvents::PUSH_EMPTY] -
                counts[ProducerEvents::PUSH_CONTENT] +
                counts[ProducerEvents::PUSH_LOW] * 0.5f;
        const int FLUCTUATING_LIMIT = 3;

        if (-FLUCTUATING_LIMIT < diff && diff < FLUCTUATING_LIMIT)
            return FLUCTUATING;
        else if (diff >= FLUCTUATING_LIMIT)
            return INCOHERENT;
        else
            return COHERENT;
    }

    case ReconfigureReason::NO_WORK: {
        populate_vector(_data->_with_work_threshold);
        // Give a lower weight to critical pushs
        float diff = counts[ProducerEvents::PUSH_EMPTY] - 
                counts[ProducerEvents::PUSH_CONTENT] + 
                counts[ProducerEvents::PUSH_LOW] * 0.5f;
        const int FLUCTUATING_LIMIT = 3;

        if (-FLUCTUATING_LIMIT < diff && diff < FLUCTUATING_LIMIT)
            return FLUCTUATING;
        else if (diff <= -FLUCTUATING_LIMIT)
            return INCOHERENT;
        else
            return COHERENT;
    }

    default:
        return INCOHERENT;
    }
}

template<typename T>
void FIFOPlus<T>::_reconfigure_consumer_gradient(ReconfigureReason reason, typename FIFOPlus<T>::Gradients in_gradient) {

}

template<typename T>
typename FIFOPlus<T>::Gradients FIFOPlus<T>::_consumer_gradient(ReconfigureReason reason) const {
    return COHERENT;
}

template<typename T>
void FIFOPlus<T>::_reconfigure_phase() {
    switch (_data->_phase) {
    case FIFOPhase::HEATING:
        if (_data->__n * _data->_increase_mult > _data->_max) {
            _data->__n = _data->_max;
            _data->_n = _data->_max;
            _data->_phase = FIFOPhase::RUNNING;
        } else {
            _data->__n *= _data->_increase_mult;
            _data->_n = std::floor(_data->__n);
        }
        break;

    case FIFOPhase::RUNNING: {
        Gradients gradient = _phase_gradient();
        if (gradient == FLUCTUATING) {
            _data->_phase = FIFOPhase::MAYBE_COOLING;
        } else if (gradient == INCOHERENT) {
            _data->_phase = FIFOPhase::COOLING;
        }
        break;
    }

    case FIFOPhase::MAYBE_COOLING: {
        Gradients gradient = _phase_gradient();
        switch (gradient) {
        case COHERENT:
            _data->_phase = FIFOPhase::COOLING;
            break;

        case INCOHERENT:
            _data->_phase = FIFOPhase::RUNNING;
            break;

        default:
            break;
        }
        break;
    }

    case FIFOPhase::COOLING:
        if (_data->__n * _data->_decrease_mult < _data->_min) {
            _data->__n = _data->_min;
            _data->_n = _data->_min;
        } else {
            _data->__n *= _data->_decrease_mult;
            _data->_n = std::ceil(_data->__n);
        }
        break;

    default:
        break;
    }
}

template<typename T>
typename FIFOPlus<T>::Gradients FIFOPlus<T>::_phase_gradient() const {
    if (_data->_phase == FIFOPhase::HEATING || _data->_phase == FIFOPhase::COOLING)
        return COHERENT;

    /* Return FLUCTUATING to indicate that the cooling phase may be arriving.
     * Return INCOHERENT to indicate that cooling phase has arrived.
     * Return COHERENT to indicate that running phase can continue.
     */
    if (_data->_phase == FIFOPhase::RUNNING) {
        return COHERENT; // Remain in the running phase for now
    } else {
        /* Return COHERENT to indicate that the cooling phase has arrived.
         * Return INCOHERENT to indicate that the running phase is still ongoing.
         * Return FLUCTUATING to indicate that the cooling phase may be arriving.
         */
        assert(_data->_phase == FIFOPhase::MAYBE_COOLING);
        
    }
}
