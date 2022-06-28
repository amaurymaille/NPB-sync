#include <cmath>

#include <algorithm>
#include <iostream>
#include <iterator>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <tuple>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static void init_atomic(std::atomic<uint32_t>& a) {
    a.store(0, std::memory_order_relaxed);
}

static void init_atomic(std::atomic<bool>& a) {
    a.store(false, std::memory_order_relaxed);
}

template<typename T>
Observer<T>::Observer() { 
    init_atomic(_producer_count);
    init_atomic(_producer_count_second_phase);
    init_atomic(_consumer_count);
    init_atomic(_consumer_count_second_phase);
    init_atomic(_reconfigured);
    init_atomic(_reconfigured_twice);
}

template<typename T>
Observer<T>::Observer(std::string const& description, uint64_t iter, int choice_step, int dephase, int prod_step, int cons_step) : 
    _description(description), _choice_step(choice_step), _dephase(dephase), _prod_step(prod_step), _cons_step(cons_step) {
    _data._iter = iter;

    init_atomic(_producer_count);
    init_atomic(_producer_count_second_phase);
    init_atomic(_consumer_count);
    init_atomic(_consumer_count_second_phase);
    init_atomic(_reconfigured);
    init_atomic(_reconfigured_twice);
}

template<typename T>
Observer<T>::~Observer() {
    std::cout << "First prod = " << _data._first_prod_step << ", first cons = " << _data._first_cons_step << ", second prod " << _data._second_prod_step << ", second cons " << _data._second_cons_step << ", first prod effective " << _data._first_prod_step_eff << ", first cons effective " << _data._first_cons_step_eff << ", second prod effective " << _data._second_prod_step_eff << ", second cons effective " << _data._second_cons_step_eff << std::endl;

    std::cout << _producer_count.load(std::memory_order_acquire) << ", " << _consumer_count.load(std::memory_order_acquire) << ", " << _producer_count_second_phase.load(std::memory_order_acquire) << ", " << _consumer_count_second_phase.load(std::memory_order_acquire) << ", " << _n_first_reconf << ", " << _n_second_reconf << std::endl;
}

template<typename T>
void Observer<T>::delayed_init(std::string const& description, uint64_t iter, int choice_step, int dephase, int prod_step, int cons_step) {
    _data._iter = iter;
    _choice_step = choice_step;
    _dephase = dephase;
    _prod_step = prod_step;
    _cons_step = cons_step;
    _description = description;
}

template<typename T>
void Observer<T>::add_producer(NaiveQueueImpl<T>* producer) {
    FirstReconfigurationData& data = _times[producer];
    data._producer = true;

    ++_n_producers;
    _cost_s[producer];
    // _cs_data[producer]._producer = true;
}

template<typename T>
void Observer<T>::add_consumer(NaiveQueueImpl<T>* consumer) {
    FirstReconfigurationData& data = _times[consumer];
    data._producer = false;
    
    ++_n_consumers;
    // _cs_data[consumer]._producer = false;
}

template<typename T>
bool Observer<T>::add_work_time(NaiveQueueImpl<T>* client, uint64_t time) {
    if (_times.find(client) == _times.end()) {
        throw std::runtime_error("Client not registered\n");
    }

    assert (time != 0);

    uint32_t operations, max, other_operations, other_max;
    if (client->_producer) {
        operations = get_add_producers_operations_first_phase();
        max = get_max_producers_operations_first_phase();
        other_operations = get_consumers_operations_first_phase();
        other_max = get_max_consumers_operations_first_phase();
    } else {
        operations = get_add_consumers_operations_first_phase();
        max = get_max_consumers_operations_first_phase();
        other_operations = get_producers_operations_first_phase();
        other_max = get_max_producers_operations_first_phase();
    }

    if (operations > max) {
        return false;
    }

    FirstReconfigurationData& data = _times[client];
    data._m.lock();
    data._work_times.push_back(time);
    data._interactions++;
    data._m.unlock();

    if (operations == max && other_operations >= other_max) {
        trigger_reconfigure(true);
        return false;
    }

    return true;
}

template<typename T>
void Observer<T>::trigger_reconfigure(bool first) {
    auto avg = [](uint64_t* arr, size_t s, float ignore = 0) {
        if (s == 0) {
            return 0;
        }

        if (ignore != 0) {
            std::sort(arr, arr + s);
        }
        return std::accumulate(arr + int(s * ignore), arr + int(s * (1 - ignore)), 0) / int(s * (1 - 2 * ignore));
    };

    auto sorted_median = [](uint64_t* arr, size_t s) {
        if (s == 0) {
            return 0UL;
        }

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
        std::sort(arr, arr + s);
        return sorted_median(arr, s / 2);
    };

    bool expected = false;
    if (first) {
        if (!_reconfigured.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return;
        }

        /* if (!_reconfigured && std::all_of(_times.begin(), _times.end(), [this](auto const& value) {
            auto const& [_, data] = value;
            if (data._producer) {
                bool result = data._n_work == this->_prod_size && data._n_push == this->_cost_p_cost_s_size;
                return result;
            } else {
                bool result = data._n_work == this->_cons_size;
                return result;
            }
        })) { */
        std::vector<uint64_t> cost_p;
        cost_p.reserve(_times.size());
        
        std::vector<uint64_t> producers, consumers;
        producers.reserve(_n_producers);
        consumers.reserve(_n_consumers);

        std::vector<uint64_t> locks, unlocks, copies;
        locks.reserve(_n_producers);
        unlocks.reserve(_n_producers);
        copies.reserve(_n_producers);
        
        uint32_t producers_zero = 0;
        for (auto& [queue, data]: _times) {
            data._m.lock();
            unsigned int average = avg(data._work_times.data(), data._work_times.size());
            if (data._producer) {
                cost_p.push_back(avg(data._push_times.data(), data._push_times.size()));
                data._m.unlock();
                if (average == 0) {
                    ++producers_zero;
                }
                producers.push_back(average);
            } else {
                data._m.unlock();
                consumers.push_back(average);
            }
        }

        if (producers_zero == _n_producers) {
            throw std::runtime_error("Okay like WTF bro\n");
        }

        auto consumer_avg = avg(consumers.data(), consumers.size());
        auto producer_avg = avg(producers.data(), producers.size());
        _data._cost_p = *std::min_element(cost_p.begin(), cost_p.end());

        for (auto& [queue, data]: _times) {
            if (data._producer) {
                locks.push_back(quart(data._lock_times.data(), data._lock_times.size()));
                unlocks.push_back(quart(data._unlock_times.data(), data._unlock_times.size()));
                copies.push_back(quart(data._copy_times.data(), data._copy_times.size()));
            }
        }

        _data._cost_wl = avg(locks.data(), locks.size());
        _data._cost_cc = avg(copies.data(), copies.size());
        _data._cost_u = avg(unlocks.data(), unlocks.size());

        auto [prod_step, cons_step] = compute_steps(producer_avg, consumer_avg, _data._cost_wl + _data._cost_u);
        if (prod_step == 0) {
            prod_step = 1;
        }

        if (cons_step == 0) {
            cons_step = 1;
        }

        // int dephase_i = 0;
        for (auto& [queue, map_data]: _times) {
#if RECONFIGURE == 1
            /* if (_choice_step == 0 || map_data._producer) {
                queue->prepare_reconfigure(BEST_STEP);
            } else {
                queue->prepare_reconfigure(_choice_step + _dephase * dephase_i);
                ++dephase_i;
            } */
            if (map_data._producer) {
                queue->prepare_reconfigure(BEST_PROD_STEP);
            } else {
                queue->prepare_reconfigure(BEST_CONS_STEP);
            }
#endif
        }

        _data._producers_avg = producer_avg;
        _data._consumers_avg = consumer_avg;
        _data._prod_cons = _n_producers * consumer_avg;
        _data._prod_prod = _n_consumers * producer_avg;
        _data._first_prod_step = prod_step;
        _data._first_cons_step = cons_step;

        _data._first_prod_step_eff = BEST_PROD_STEP;
        _data._first_cons_step_eff = BEST_CONS_STEP;


        // }
    } else {
        /* if (!_reconfigured_twice && std::all_of(_cost_s.begin(), _cost_s.end(), [this](auto const& value) {
            auto const& [_, costs] = value;
            if (costs.size() != this->_cost_s_size) {
                return false;
            }

            return true;
        })) { */
        if (!_reconfigured_twice.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return;
        }
        std::vector<uint64_t> cost_s(_cost_s.size());

        for (auto& data: _cost_s) {
            auto& v = data.second;

            std::vector<uint64_t> s;
            v._m.lock();
            for (auto [lock, critical, unlock]: v._cost_s) {
                // averg += lock + unlock;
                s.push_back(lock + unlock);
            }
            v._m.unlock();
            // cost_s.push_back(averg / v.size());
            cost_s.push_back(unsorted_median(s.data(), s.size()));
        }

        uint64_t avg_cost_s = avg(cost_s.data(), cost_s.size());
        // unsigned int prod_step = 0, cons_step = 0;
        auto [prod_step, cons_step] = compute_steps(_data._producers_avg, _data._consumers_avg, avg_cost_s);

        // int dephase_i = 0;
        for (auto& [queue, map_data]: _times) {
#if RECONFIGURE == 1
            /* if (_choice_step == 0 || map_data._producer) {
                queue->prepare_reconfigure(SECOND_BEST_STEP);
            } else {
                queue->prepare_reconfigure(_choice_step + _dephase * dephase_i);
                ++dephase_i;
            } */
            if (map_data._producer) {
                queue->prepare_reconfigure(SECOND_BEST_PROD_STEP);
            } else {
                queue->prepare_reconfigure(SECOND_BEST_CONS_STEP);
            }
#endif
        }

        _data._second_prod_step = prod_step;
        _data._second_cons_step = cons_step;

        _data._second_prod_step_eff = SECOND_BEST_PROD_STEP;
        _data._second_cons_step_eff = SECOND_BEST_CONS_STEP;
    }
}

template<typename T>
std::tuple<uint32_t, uint32_t> Observer<T>::compute_steps(uint64_t producer_avg, uint64_t consumer_avg, uint64_t cost_s) {
    uint64_t pc_ratio = _n_consumers * producer_avg;
    uint64_t cp_ratio = _n_producers * consumer_avg;

    uint64_t up = _data._iter * cost_s;
    if (pc_ratio > cp_ratio) {
        uint64_t down_left = (_n_producers * consumer_avg + _n_producers * _data._cost_p) / _n_consumers;
        uint64_t down_right = 2 * (_n_producers * _data._cost_p - _data._cost_p + _n_producers * _data._cost_p);

        uint32_t step = std::sqrt(up / (down_left + down_right));
        return { step, step * _n_producers / _n_consumers };
    } else {
        uint64_t down_left = (_n_consumers * producer_avg + _n_consumers * _data._cost_p) / _n_producers;
        uint64_t down_right = 2 * (_n_consumers * _data._cost_p - _data._cost_p + _n_consumers * _data._cost_p);

        uint32_t step = std::sqrt(up / (down_left + down_right));
        return { step * _n_consumers / _n_producers, step };
    }
}

template<typename T>
void Observer<T>::set_first_reconfiguration_n(uint32_t n) {
    for (auto& [_, data]: _times) {
        if (data._producer) {
            if (data._push_times.capacity() != 0) {
                throw std::runtime_error("You cannot change the size of the time");
            }

            data._work_times.reserve(n);
            data._push_times.reserve(n);
            data._lock_times.reserve(n);
            data._copy_times.reserve(n);
            data._unlock_times.reserve(n);
        }
    }

    _n_first_reconf = n;
}

template<typename T>
bool Observer<T>::add_producer_synchronization_time_first(NaiveQueueImpl<T>* producer, uint64_t push_time, uint64_t lock_time, uint64_t copy_time, uint64_t unlock_time, uint64_t /* items */) {
    assert (push_time != 0);
    assert (lock_time != 0);
    assert (copy_time != 0);
    assert (unlock_time != 0);

    uint32_t observations = get_add_producers_operations_first_phase();
    uint32_t max = get_max_producers_operations_first_phase();

    if (observations > max) {
        return false;
    }

    if (_times.find(producer) == _times.end()) {
        throw std::runtime_error("Adding sync time for non registered producer\n");
    }

    FirstReconfigurationData& data = _times[producer];
    data._m.lock();
    data._push_times.push_back(push_time);

    if (copy_time != 0) {
        data._lock_times.push_back(lock_time);
        data._copy_times.push_back(copy_time);
        data._unlock_times.push_back(unlock_time);
        data._interactions++;
        // data._items.push_back(items);
    }
    data._m.unlock();

    uint32_t other_observations = get_consumers_operations_first_phase();
    uint32_t other_max = get_max_consumers_operations_first_phase();
    if (observations == max && other_observations >= other_max) {
        trigger_reconfigure(true);
        return false;
    }

    return true;
}

template<typename T>
void Observer<T>::set_second_reconfiguration_n(uint32_t n) {
    _n_second_reconf = n;
    for (auto& data: _cost_s) {
        auto& v = data.second;
        v._cost_s.reserve(n);
    }

    /* for (auto& data: _cs_data) {
        data.second._data.reserve(cost_s_size);
    } */
}

template<typename T>
typename Observer<T>::CostSState Observer<T>::add_producer_synchronization_time_second(NaiveQueueImpl<T>* producer, uint64_t lock, uint64_t critical, uint64_t unlock) {
    if (producer->was_reconfigured()) {
        uint32_t observations = get_add_producers_operations_second_phase();
        uint32_t max = get_max_producers_operations_second_phase();

        if (observations > max) {
            return CostSState::RECONFIGURED;
        }

        if (_cost_s.find(producer) == _cost_s.end()) {
            throw std::runtime_error("Adding second reconfiguration time for not registered producer");
        }

        SecondReconfigurationData& data = _cost_s[producer];
        data._m.lock();
        data._cost_s.push_back({lock, critical, unlock});
        data._m.unlock();

        if (observations == max) {
            trigger_reconfigure(false);
            return CostSState::TRIGGERED;
        }

        return CostSState::NOT_RECONFIGURED;
    }

    return CostSState::NOT_RECONFIGURED;
}

template<typename T>
json Observer<T>::serialize() const {
    json fifos = json::array();

    json steps;
    steps["first_prod_step"] = _data._first_prod_step;
    steps["first_cons_step"] = _data._first_cons_step;
    steps["first_prod_step_effective"] = _data._first_prod_step_eff;
    steps["first_cons_step_effective"] = _data._first_cons_step_eff;
    steps["second_prod_step"] = _data._second_prod_step;
    steps["second_cons_step"] = _data._second_cons_step;
    steps["second_prod_step_effective"] = _data._second_prod_step_eff;
    steps["second_cons_step_effective"] = _data._second_cons_step_eff;

    json compute;
    compute["iter"] = _data._iter;
    compute["n_consumers"] = _n_consumers;
    compute["n_producers"] = _n_producers;
    compute["prod_cons"] = _data._prod_cons;
    compute["prod_prod"] = _data._prod_prod;
    compute["producers_avg"] = _data._producers_avg;
    compute["consumers_avg"] = _data._consumers_avg;
    compute["cost_wl"] = _data._cost_wl;
    compute["cost_u"] = _data._cost_u;
    compute["cost_p"] = _data._cost_p;

    auto copy = [](json& arr, auto src, int n) {
        for (int i = 0; i < n; ++i) {
            arr.push_back(src[i]);
        }
    };

    for (auto const& [queue, data]: _times) {
        json fifo;
        fifo["type"] = data._producer ? "producer" : "consumer";
        json locks = json::array(), transfers = json::array(), unlocks = json::array(), locals = json::array(), work = json::array();

        copy(work, data._work_times.data(), data._work_times.size());
    
        fifo["work"] = work;

        if (! data._producer) {
            fifos.push_back(fifo);
            continue;
        }

        copy(locals, data._push_times.data(), data._push_times.size());
        copy(locks, data._lock_times.data(), data._lock_times.size());
        copy(transfers, data._copy_times.data(), data._copy_times.size());
        copy(unlocks, data._unlock_times.data(), data._unlock_times.size());

        fifo["lock"] = locks;
        fifo["transfer"] = transfers;
        fifo["unlock"] = unlocks;
        fifo["local"] = locals;

        fifos.push_back(fifo);
    }

    json p2 = json::array();
    for (auto const& [queue, data]: _cost_s) {
        json fifo;
        json locks = json::array(), unlocks = json::array(), transfers = json::array();

        fifo["type"] = queue->_producer ? "producer" : "consumer";
        for (auto [lock, transfer, unlock]: data._cost_s) {
            locks.push_back(lock);
            unlocks.push_back(unlock);
            transfers.push_back(transfer);
        }

        fifo["lock"] = locks;
        fifo["unlock"] = unlocks;
        fifo["transfer"] = transfers;
        
        p2.push_back(fifo);
    }

    json result;
    result["description"] = _description;
    result["compute"] = compute;
    result["steps"] = steps;
    result["fifos"] = fifos;
    result["p2"] = p2;

    return result;
}

template<typename T>
uint32_t Observer<T>::get_producers_operations_first_phase() const {
    return _producer_count.load(std::memory_order_acquire);
}

template<typename T>
uint32_t Observer<T>::get_consumers_operations_first_phase() const {
    return _consumer_count.load(std::memory_order_acquire);
}

template<typename T>
uint32_t Observer<T>::get_add_producers_operations_first_phase() {
    return _producer_count.fetch_add(1, std::memory_order_acq_rel);
}

template<typename T>
uint32_t Observer<T>::get_add_producers_operations_second_phase() {
    return _producer_count_second_phase.fetch_add(1, std::memory_order_acq_rel);
}

template<typename T>
uint32_t Observer<T>::get_add_consumers_operations_first_phase() {
    return _consumer_count.fetch_add(1, std::memory_order_acq_rel);
}

template<typename T>
uint32_t Observer<T>::get_add_consumers_operations_second_phase() {
    return _consumer_count_second_phase.fetch_add(1, std::memory_order_acq_rel);
}

template<typename T>
uint32_t Observer<T>::get_max_producers_operations_first_phase() {
    return _n_first_reconf * 2 * _n_producers;
}

template<typename T>
uint32_t Observer<T>::get_max_producers_operations_second_phase() {
    return _n_second_reconf * 2 * _n_producers;
}

template<typename T>
uint32_t Observer<T>::get_max_consumers_operations_first_phase() {
    return _n_first_reconf * _n_consumers;
}

template<typename T>
uint32_t Observer<T>::get_max_consumers_operations_second_phase() {
    return _n_second_reconf * _n_consumers;
}

/* template<typename T>
void Observer<T>::add_critical_section_data(NaiveQueueImpl<T>* queue, uint64_t lock, uint64_t cs, uint64_t unlock, uint64_t items) {
    _cs_data[queue]._data.push_back({lock, cs, unlock, items});
} */
