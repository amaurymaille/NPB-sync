#include <cmath>

#include <algorithm>
#include <iterator>
#include <numeric>

template<typename T>
Observer<T>::Observer(uint64_t cost_sync, uint64_t iter) : 
    _prod_size(0), _cons_size(0), _cost_p_cost_s_size(0) {
    // _data._cost_p = cost_push;
    _data._cost_s = cost_sync;
    _data._iter = iter;
    // sem_init(&_reconfigure_sem, 0, 0);
}

template<typename T>
Observer<T>::~Observer() {
    // std::cout << "Found best step = " << _best_step << ", push cost = " << _data._cost_p << ", worst average Wi = " << _worst_avg << std::endl;
}

/* template<typename T>
void Observer<T>::set_consumer(NaiveQueueImpl<T>* consumer) {
    _consumer = consumer;
}

template<typename T>
void Observer<T>::set_producer(NaiveQueueImpl<T>* producer) {
    _producer = producer;
} */

/* template<typename T>
void Observer<T>::begin() {
    _begin = std::chrono::steady_clock::now();
} */

template<typename T>
void Observer<T>::add_producer(NaiveQueueImpl<T>* producer) {
    MapData& data = _times[producer];
    data._work_times = data._push_times = nullptr;
    data._n_work = data._n_push = data._n_sync = 0;
    data._producer = true;
}

template<typename T>
void Observer<T>::add_consumer(NaiveQueueImpl<T>* consumer) {
    MapData& data = _times[consumer];
    data._work_times = data._push_times = nullptr;
    data._n_work = data._n_push = data._n_sync = 0;
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

/* template<typename T>
void Observer<T>::measure() {
    while (!_reconfigured) {
        sem_wait(&_reconfigure_sem);
    }
    _time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - _begin).count();
    std::cout << _time << std::endl;
} */

template<typename T>
void Observer<T>::trigger_reconfigure() {
    std::unique_lock<std::mutex> lck(_m);
    if (!_reconfigured && std::all_of(_times.begin(), _times.end(), [this](auto const& value) {
        auto const& [_, data] = value;
        if (data._producer) {
            return data._n_work == this->_prod_size && data._n_push == this->_cost_p_cost_s_size;
        } else {
            return data._n_work == this->_cons_size;
        }
    })) {
        _reconfigured = true;
        // sem_post(&_reconfigure_sem);
        std::vector<uint64_t> cost_p;
        cost_p.reserve(_times.size());
        std::vector<unsigned int> averages(_times.size());

        auto avg = [](uint64_t* arr, size_t s) {
            return std::accumulate(arr, arr + s, 0) / s;
        };

        for (auto& [queue, data]: _times) {
            if (data._producer) {
                cost_p.push_back(avg(data._push_times, data._n_push));
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

        _best_step = best_step;
        _worst_avg = worst_avg;

        _cost_p = std::move(cost_p);
        _averages = std::move(averages);

        // _time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - _begin).count();
        // printf("Reconfiguring producer and consumer to %d\n", best_step);
    }
}

template<typename T>
void Observer<T>::set_cost_p_cost_s_size(size_t cost_p_cost_s_size) {
    for (auto& [_, data]: _times) {
        if (data._producer) {
            if (data._push_times) {
                throw std::runtime_error("You cannot change the size of the time");
            }
            data._push_times = (uint64_t*)malloc(sizeof(uint64_t) * cost_p_cost_s_size);
            data._sync_times = (uint64_t*)malloc(sizeof(uint64_t) * cost_p_cost_s_size);
        }
    }

    _cost_p_cost_s_size = cost_p_cost_s_size;
}

template<typename T>
void Observer<T>::add_cost_p_cost_s_time(NaiveQueueImpl<T>* producer, uint64_t push_time, uint64_t sync_time) {
    // printf("Adding CostP = %llu\n", time);
    MapData& data = _times[producer];
    data._push_times[data._n_push++] = push_time;
    data._sync_times[data._n_sync++] = sync_time;
    if (data._n_push == _cost_p_cost_s_size) {
        trigger_reconfigure();
    }
}

template<typename T>
json Observer<T>::serialize() const {
    json result;
    result["best_step"] = _best_step;
    result["worst_avg"] = _worst_avg;
    result["cost_p"] = _data._cost_p;

    json extra;
    extra["cost_p"] = _cost_p;
    extra["average"] = _averages;
    
    json cost_s = json::array();
    for (auto const& [_, data]: _times) {
        if (data._producer) {
            json this_producer = json::array();
            for (int i = 0; i < data._n_sync; ++i) {
                this_producer.push_back(data._sync_times[i]);
            }
            cost_s.push_back(this_producer);
        }
    }
    
    extra["cost_s"] = cost_s;
    result["extra"] = extra;
    return result;
}
