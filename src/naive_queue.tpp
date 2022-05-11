#include <cmath>

#include <algorithm>
#include <iostream>
#include <iterator>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <tuple>

#define BEST_STEP 100
#define SECOND_BEST_STEP 100
#define RECONFIGURE 1

template<typename T>
Observer<T>::Observer() { }

template<typename T>
Observer<T>::Observer(uint64_t iter, int n_threads) : 
    _prod_size(0), _cons_size(0), _cost_p_cost_s_size(0), _n_threads(n_threads) {
    // _data._cost_p = cost_push;
    // _data._cost_s = cost_sync;
    _data._iter = iter;
    // sem_init(&_reconfigure_sem, 0, 0);
    // printf("Observer %p\n", this);
}

template<typename T>
Observer<T>::~Observer() {
    std::cout << "Found best step = " << _best_step << ", second best step = " << _second_best_step << ", push cost = " << _data._cost_p << ", worst average Wi = " << _worst_avg << ", sync cost = (" << _data._cost_wl << ", " << _data._cost_cc << ", " << _data._cost_u << ")" << std::endl;

    for (auto& [queue, data]: _times) {
        free(data._work_times);
        free(data._push_times);
        free(data._lock_times);
        free(data._unlock_times);
        free(data._copy_times);
    }
}

template<typename T>
void Observer<T>::delayed_init(uint64_t iter, int n_threads) {
    _prod_size = _cons_size = _cost_p_cost_s_size = 0;
    _n_threads = n_threads;
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

    _cost_s[producer];
    _cs_data[producer]._producer = true;
}

template<typename T>
void Observer<T>::add_consumer(NaiveQueueImpl<T>* consumer) {
    MapData& data = _times[consumer];
    data._work_times = data._push_times = nullptr;
    data._n_work = data._n_push = data._n_sync = 0;
    data._producer = false;
    _cs_data[consumer]._producer = false;
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
    if (_times.find(producer) == _times.end()) {
        for (auto const& [addr, _]: _times) {
            printf("XXX %p\n", addr);
        }
        throw std::runtime_error("Producer not registered\n");
    }

    MapData& data = _times[producer];
    if (data._n_work < _prod_size) {
        data._work_times[data._n_work++] = time;
    }

    if (data._n_work == _prod_size) {
        trigger_reconfigure(true);
    }
}

template<typename T>
void Observer<T>::add_consumer_time(NaiveQueueImpl<T>* consumer, uint64_t time) {
    if (_times.find(consumer) == _times.end()) {
        for (auto const& [addr, _]: _times) {
            printf("XXX %p\n", addr);
        }
        throw std::runtime_error("Consumer not registered\n");
    }

    MapData& data = _times[consumer];
    if (data._n_work < _cons_size) {
        data._work_times[data._n_work++] = time;
    }

    if (data._n_work == _cons_size) {
        trigger_reconfigure(true);
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
void Observer<T>::trigger_reconfigure(bool first) {
    std::unique_lock<std::mutex> lck(_m);
    auto avg = [](uint64_t* arr, size_t s, float ignore = 0) {
        std::sort(arr, arr + s);
        return std::accumulate(arr + int(s * ignore), arr + int(s * (1 - ignore)), 0) / int(s * (1 - 2 * ignore));
    };

    auto sorted_median = [](uint64_t* arr, size_t s) {
        if (s % 2 == 0) {
            return (arr[s / 2 - 1] + arr[s / 2]) / 2;
        } else {
            return arr[s / 2];
        }
    };

    auto unsorted_median = [=](uint64_t* arr, size_t s) {
        std::sort(arr, arr + s);
        return sorted_median(arr, s);
    };

    auto quart = [=](uint64_t* arr, size_t s) {
        return unsorted_median(arr, s / 2);
    };

    /* auto avg_cost_s_fix = [](uint64_t* arr, size_t s, NaiveQueueImpl<T>* queue, uint64_t cost_p) {
        uint64_t sum = 0;
        for (int i = 0; i < s; ++i) {
            if (arr[i] >= queue->get_step() * cost_p)
                arr[i] -= queue->get_step() * cost_p;
            sum += arr[i];
        }

        return sum / s;
    }; */

    if (first) {
        if (!_reconfigured && std::all_of(_times.begin(), _times.end(), [this](auto const& value) {
            auto const& [_, data] = value;
            if (data._producer) {
                bool result = data._n_work == this->_prod_size && data._n_push == this->_cost_p_cost_s_size;
                return result;
            } else {
                bool result = data._n_work == this->_cons_size;
                return result;
            }
        })) {
            // printf("Reconfiguration (first)\n");
            // sem_post(&_reconfigure_sem);
            std::vector<uint64_t> cost_p;
            cost_p.reserve(_times.size());
            std::vector<uint64_t> averages(_times.size());
            std::vector<uint64_t> producers, consumers;
            // std::vector<uint64_t> costs_s;
            std::vector<uint64_t> locks, unlocks, copies;
            
            for (auto& [queue, data]: _times) {
                unsigned int average = avg(data._work_times, data._n_work);
                if (data._producer) {
                    cost_p.push_back(avg(data._push_times, data._n_push));
                    producers.push_back(average);
                } else {
                    consumers.push_back(average);
                }
                averages.push_back(average);
            }

            auto worst_avg = std::max(avg(producers.data(), producers.size()), avg(consumers.data(), consumers.size()));
            _data._wi = worst_avg;
            _data._cost_p = *std::min_element(cost_p.begin(), cost_p.end());

            for (auto& [queue, data]: _times) {
                if (data._producer) {
                    // costs_s.push_back(avg_cost_s_fix(data._sync_times, data._n_sync, queue, _data._cost_p));
                    locks.push_back(quart(data._lock_times, data._n_sync));
                    unlocks.push_back(quart(data._unlock_times, data._n_sync));
                    copies.push_back(quart(data._copy_times, data._n_sync));
                }
            }

            _data._cost_wl = avg(locks.data(), locks.size());
            _data._cost_cc = avg(copies.data(), copies.size());
            _data._cost_u = avg(unlocks.data(), unlocks.size());
            //_data._cost_s = avg(costs_s.data(), costs_s.size());
            // auto select_avg = std::max(cons_avg, prod_avg);
            // unsigned int best_step = std::sqrt((_data._iter * _data._cost_s) / (select_avg + _data._cost_p));
            // unsigned int best_step = std::sqrt((_data._iter * _data._cost_s * _n_threads) / (worst_avg + _data._cost_p));
            unsigned int best_step = std::sqrt((_data._iter * (_data._cost_wl + /* _data._cost_cc + */ _data._cost_u)) / (_data._cost_p * _times.size() + _data._wi));

            for (auto& [queue, _]: _times) {
#if RECONFIGURE == 1
                queue->prepare_reconfigure(BEST_STEP);
#endif
            }
            // _consumer->prepare_reconfigure(best_step);
            // _producer->prepare_reconfigure(best_step);

            // printf("Iter = %lu, CostS = (%lu, %lu, %lu), CostP = %lu, Times.size() = %lu, Wi = %lu, Step = %lu\n", _data._iter, _data._cost_wl, _data._cost_cc, _data._cost_u, _data._cost_p, _times.size(), _data._wi, best_step);
            _best_step = best_step;
            _worst_avg = worst_avg;

            _cost_p = std::move(cost_p);
            _averages = std::move(averages);

            // _time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - _begin).count();
            // printf("Reconfiguring producer and consumer to %d\n", best_step);
            _reconfigured = true;
        }
    } else {
        if (!_reconfigured_twice && std::all_of(_cost_s.begin(), _cost_s.end(), [this](auto const& value) {
            auto const& [_, costs] = value;
            if (costs.size() != this->_cost_s_size) {
                return false;
            }

            return true;
        })) {
            // printf("Reconfiguration (second)\n");
            _reconfigured_twice = true;
            std::vector<uint64_t> cost_s(_cost_s.size());
            // std::ostringstream stream;

            for (auto& data: _cost_s) {
                auto& v = data.second;
                /* stream << data.first << " => ";
                for (uint64_t value: v) {
                    stream << value << " ";
                }
                stream << ": "; */
                // std::cout << v[0] << std::endl;
                // auto average = avg(v.data(), v.size());
                // std::cout << v[0] << std::endl;
                // cost_s.push_back(average);
                // stream << average << std::endl;

                std::vector<uint64_t> s;
                for (auto [lock, critical, unlock]: v) {
                    // averg += lock + unlock;
                    s.push_back(lock + unlock);
                }
                // cost_s.push_back(averg / v.size());
                cost_s.push_back(unsorted_median(s.data(), s.size()));
            }
            /* stream << std::endl;
            std::cout << stream.str(); */

            uint64_t avg_cost_s = avg(cost_s.data(), cost_s.size());
            unsigned int best_step = std::sqrt((_data._iter * avg_cost_s) / (_data._cost_p * _times.size() + _data._wi));
            // printf("Old = %d, new = %d\n", _best_step, best_step);
            _second_best_step = best_step;

            for (auto& [queue, _]: _times) {
#if RECONFIGURE == 1
                queue->prepare_reconfigure(SECOND_BEST_STEP);
#endif
            }
        }
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
            // data._sync_times = (uint64_t*)malloc(sizeof(uint64_t) * cost_p_cost_s_size);
            data._lock_times = (uint64_t*)malloc(sizeof(uint64_t) * cost_p_cost_s_size);
            data._unlock_times = (uint64_t*)malloc(sizeof(uint64_t) * cost_p_cost_s_size);
            data._copy_times = (uint64_t*)malloc(sizeof(uint64_t) * cost_p_cost_s_size);
        }
    }

    _cost_p_cost_s_size = cost_p_cost_s_size;
}

template<typename T>
void Observer<T>::add_cost_p_cost_s_time(NaiveQueueImpl<T>* producer, uint64_t push_time, uint64_t lock_time, uint64_t copy_time, uint64_t unlock_time) {
    // printf("Adding CostP = %llu\n", time);
    MapData& data = _times[producer];

    if (data._n_push < _cost_p_cost_s_size) {
        data._push_times[data._n_push++] = push_time;
    }

    if (copy_time != 0) {
        /* if (data._n_sync < _cost_p_cost_s_size) {
            data._sync_times[data._n_sync++] = sync_time;
        } */
        if (data._n_sync < _cost_p_cost_s_size) {
            data._lock_times[data._n_sync] = lock_time;
            data._copy_times[data._n_sync] = copy_time;
            data._unlock_times[data._n_sync] = unlock_time;
            data._n_sync++;
        }
    }
    if (data._n_push == _cost_p_cost_s_size && data._n_sync == _cost_p_cost_s_size) {
        trigger_reconfigure(true);
    }
}

template<typename T>
json Observer<T>::serialize() const {
    json result;
    result["best_step"] = _best_step;
    result["best_step_p2"] = _second_best_step;
    result["worst_avg"] = _worst_avg;
    result["cost_p"] = _data._cost_p;

    json extra;
    extra["cost_p"] = _cost_p;
    extra["average"] = _averages;

    json cs_data = json::array();
    for (auto const& [queue, data]: _times) {
        if (data._producer) {
            json this_producer = json::array();
            for (int i = int(data._n_sync * 0.1); i < int(data._n_sync * 0.9); ++i) {
                json cs;
                cs["lock"] = data._lock_times[i];
                cs["cs"] = data._copy_times[i];
                cs["unlock"] = data._unlock_times[i];
                this_producer.push_back(cs);
            }
            cs_data.push_back(this_producer);
        }
    }

    json cs_data_p2 = json::array();
    for (auto const& [queue, data]: _cost_s) {
        json this_producer = json::array();
        for (int i = 0; i < data.size(); ++i) {
            for (auto [lock, copy, unlock]: data) {
                json cs;
                cs["lock"] = lock;
                cs["cs"] = copy;
                cs["unlock"] = unlock;
                this_producer.push_back(cs);
            }
        }
        cs_data_p2.push_back(this_producer);
    }

    json consumer_cs_data = json::array();
    for (auto const& [queue, data]: _cs_data) {
        if (data._producer) {
            continue;
        }

        json this_consumer;
        this_consumer["content"] = json::array();
        // this_consumer["type"] = data._producer ? "producer":"consumer";
        for (auto [lock, cs, unlock]: data._data) {
            json content;
            content["lock"] = lock;
            content["cs"] = cs;
            content["unlock"] = unlock;
            this_consumer["content"].push_back(content);
        }

        consumer_cs_data.push_back(this_consumer);
    }

    extra["cs_data"] = cs_data;
    extra["cs_data_p2"] = cs_data_p2;
    extra["consumer_cs_daa"] = consumer_cs_data;
    
    result["extra"] = extra;
    return result;
}

template<typename T>
void Observer<T>::set_cost_s_size(size_t cost_s_size) {
    _cost_s_size = cost_s_size;
    for (auto& data: _cost_s) {
        auto& v = data.second;
        v.reserve(cost_s_size);
    }

    for (auto& data: _cs_data) {
        data.second._data.reserve(cost_s_size);
    }
}

template<typename T>
typename Observer<T>::CostSState Observer<T>::add_cost_s_time(NaiveQueueImpl<T>* producer, uint64_t lock, uint64_t critical, uint64_t unlock) {
    if (producer->was_reconfigured()) {
        if (_cost_s.find(producer) == _cost_s.end()) {
            throw std::runtime_error("Non mais là...");
        }
        // printf("%p %d %llu\n", producer, _cost_s[producer].size(), sync_time);
        /* if (sync_time < producer->get_step() * _data._cost_p) {
            // Because we cannot be sure that the value of sync_time is based on the new step
            // if it happens to be absurd, then use sync_time without trying to adjust it. This
            // should not happen a lot of times.
            time = sync_time;
        } */
        // printf("%p %llu %llu\n", producer, _cost_s[producer][0], time);
        _cost_s[producer].push_back({lock, critical, unlock});
        // printf("%p %llu\n", producer, _cost_s[producer][0]);
        if (_cost_s[producer].size() == _cost_s_size) {
            // printf("Producer P2 OK\n");
            trigger_reconfigure(false);
            return CostSState::TRIGGERED;
        }

        return CostSState::RECONFIGURED;
    }

    return CostSState::NOT_RECONFIGURED;
}

template<typename T>
void Observer<T>::add_critical_section_data(NaiveQueueImpl<T>* queue, uint64_t lock, uint64_t cs, uint64_t unlock) {
    _cs_data[queue]._data.push_back({lock, cs, unlock});
}
