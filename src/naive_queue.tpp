#include <cmath>

#include <algorithm>
#include <numeric>

template<typename T>
Observer<T>::Observer(uint64_t cost_sync, uint64_t iter) : 
    _prod_size(0), _cons_size(0), _cost_p_size(0) {
    // _data._cost_p = cost_push;
    _data._cost_s = cost_sync;
    _data._iter = iter;
}

/* template<typename T>
void Observer<T>::set_consumer(NaiveQueueImpl<T>* consumer) {
    _consumer = consumer;
}

template<typename T>
void Observer<T>::set_producer(NaiveQueueImpl<T>* producer) {
    _producer = producer;
} */

template<typename T>
void Observer<T>::add_producer(NaiveQueueImpl<T>* producer) {
    MapData& data = _times[producer];
    data._work_times = data._push_times = nullptr;
    data._n_work = data._n_push = 0;
    data._producer = true;
}

template<typename T>
void Observer<T>::add_consumer(NaiveQueueImpl<T>* consumer) {
    MapData& data = _times[consumer];
    data._work_times = data._push_times = nullptr;
    data._n_work = data._n_push = 0;
    data._producer = false;
}

template<typename T>
void Observer<T>::set_prod_size(size_t prod_size) {
    // _prod_times = (uint64_t*)malloc(prod_size * sizeof(uint64_t));
    for (auto& [_, data]: _times) {
        if (data._producer) {
            if (data._work_times) {
                throw std::runtime_error("You cannot change the size of the time");
            }
            data._work_times = (uint64_t*)malloc(sizeof(uint64_t) * prod_size);
        }
    }
    _prod_size = prod_size;
}

template<typename T>
void Observer<T>::set_cons_size(size_t cons_size) {
    for (auto& [_, data]: _times) {
        if (!data._producer) {
            if (data._work_times) {
                throw std::runtime_error("You cannot change the size of the time");
            }
            data._work_times = (uint64_t*)malloc(sizeof(uint64_t) * cons_size);
        }
    }
    _cons_size = cons_size;
}

template<typename T>
void Observer<T>::add_producer_time(NaiveQueueImpl<T>* producer, uint64_t time) {
    MapData& data = _times[producer];
    data._work_times[data._n_work++] = time;
    if (data._n_work == _prod_size) {
        trigger_reconfigure();
    }
}

template<typename T>
void Observer<T>::add_consumer_time(NaiveQueueImpl<T>* consumer, uint64_t time) {
    MapData& data = _times[consumer];
    data._work_times[data._n_work++] = time;
    if (data._n_work == _cons_size) {
        trigger_reconfigure();
    }
}

template<typename T>
void Observer<T>::trigger_reconfigure() {
    std::unique_lock<std::mutex> lck(_m);
    if (!_reconfigured && std::all_of(_times.begin(), _times.end(), [this](auto const& value) {
        auto const& [_, data] = value;
        if (data._producer) {
            return data._n_work == this->_prod_size && data._n_push == this->_cost_p_size;
        } else {
            return data._n_work == this->_cons_size;
        }
    })) {
        _reconfigured = true;
        std::vector<uint64_t> cost_p;
        cost_p.reserve(_times.size());
        std::vector<unsigned int> averages(_times.size());

        auto avg = [](uint64_t* arr, size_t s) {
            return std::accumulate(arr, arr + s, 0) / s;
        };

        for (auto& [queue, data]: _times) {
            if (data._producer) {
                auto step = queue->get_step();
                uint64_t sum = std::accumulate(data._push_times, data._push_times + data._n_push, 0);
                uint64_t cost = (sum - (_cost_p_size * _data._cost_s) / step) / (2 * _cost_p_size);
                cost_p.push_back(cost);
            }

            averages.push_back(avg(data._work_times, data._n_work));
        }

        auto worst_avg = *std::max_element(averages.begin(), averages.end());
        _data._cost_p = *std::min_element(cost_p.begin(), cost_p.end());
        // auto select_avg = std::max(cons_avg, prod_avg);
        // unsigned int best_step = std::sqrt((_data._iter * _data._cost_s) / (select_avg + _data._cost_p));
        unsigned int best_step = std::sqrt((_data._iter * _data._cost_s) / (worst_avg + _data._cost_p));

        for (auto& [queue, _]: _times) {
            queue->prepare_reconfigure(best_step);
        }
        // _consumer->prepare_reconfigure(best_step);
        // _producer->prepare_reconfigure(best_step);

        printf("Reconfiguring producer and consumer to %d\n", best_step);
    }
}

template<typename T>
void Observer<T>::set_cost_p_size(size_t cost_p_size) {
    for (auto& [_, data]: _times) {
        if (data._producer) {
            if (data._push_times) {
                throw std::runtime_error("You cannot change the size of the time");
            }
            data._push_times = (uint64_t*)malloc(sizeof(uint64_t) * cost_p_size);
        }
    }

    _cost_p_size = cost_p_size;
}

template<typename T>
void Observer<T>::add_cost_p_time(NaiveQueueImpl<T>* producer, uint64_t time) {
    MapData& data = _times[producer];
    data._push_times[data._n_push++] = time;
    if (data._n_push == _cost_p_size) {
        trigger_reconfigure();
    }
}
