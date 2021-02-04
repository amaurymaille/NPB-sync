#include <utility>

#include <omp.h>

template<typename T, DynamicStepPromiseMode mode>
DynamicStepPromiseBuilder<T, mode>::DynamicStepPromiseBuilder(int nb_values,
    unsigned int start_step, unsigned int nb_threads) : _nb_values(nb_values),
    _start_step(start_step), _n_threads(nb_threads) {

}

template<typename T, DynamicStepPromiseMode mode>
PromisePlus<T>* DynamicStepPromiseBuilder<T, mode>::new_promise() const {
    DynamicStepPromise<T, mode>* ptr = new DynamicStepPromise<T, mode>(_nb_values, _start_step);
    ptr->_last_unblock_index_weak.resize(_n_threads, -1);
    return ptr;
}

template<typename T, DynamicStepPromiseMode mode>
DynamicStepPromise<T, mode>::DynamicStepPromise(int nb_values, unsigned int start_step) : 
    PromisePlus<T>(nb_values, -1),
    _last_unblock_index_strong(),
    _last_unblock_index_weak() {
    _step.store(start_step, std::memory_order_relaxed);
    _last_unblock_index_strong.store(-1, std::memory_order_relaxed);
    if constexpr (UnblocksV<mode>) 
        _current_index.store(-1, std::memory_order_relaxed);
}

template<typename T, DynamicStepPromiseMode mode>
T& DynamicStepPromise<T, mode>::get(int index) {
    if (_last_unblock_index_weak[omp_get_thread_num()] < index) {
        while ((_last_unblock_index_weak[omp_get_thread_num()] = _last_unblock_index_strong.load(std::memory_order_acquire)) < index)
            ;
    }

    return this->_values[index];
}

template<typename T, DynamicStepPromiseMode mode>
void DynamicStepPromise<T, mode>::set(int index, const T& value) {
    if constexpr (IsTimerV<mode>)
        _sets_times.push_back(std::chrono::steady_clock::now());

    set_no_timer(index, value);
}

template<typename T, DynamicStepPromiseMode mode>
void DynamicStepPromise<T, mode>::set_no_timer(int index, const T& value) {
    this->_values[index] = value;

    set_current_index(index);
    
    {
        std::unique_lock<std::mutex> lck;
        if constexpr (RequiresLockV<mode>)
            lck = std::move(std::unique_lock<std::mutex>(_step_m));

        if (index - _last_unblock_index_strong.load(std::memory_order_acquire) >= get_step())
            _last_unblock_index_strong.store(index, std::memory_order_release);
    }
}

template<typename T, DynamicStepPromiseMode mode>
void DynamicStepPromise<T, mode>::set(int index, T&& value) {
    if constexpr (IsTimerV<mode>)
        _sets_times.push_back(std::chrono::steady_clock::now());

    set_no_timer(index, std::move(value));
}

template<typename T, DynamicStepPromiseMode mode>
void DynamicStepPromise<T, mode>::set_no_timer(int index, T&& value) {
    this->_values[index] = std::move(value);

    set_current_index(index);

    {
        std::unique_lock<std::mutex> lck;
        if constexpr (RequiresLockV<mode>)
            lck = std::move(std::unique_lock<std::mutex>(_step_m));

        if (index - _last_unblock_index_strong.load(std::memory_order_acquire) >= get_step())
            _last_unblock_index_strong.store(index, std::memory_order_release);
    }

}

template<typename T, DynamicStepPromiseMode mode>
void DynamicStepPromise<T, mode>::set_immediate(int index, const T& value) {
    if constexpr (IsTimerV<mode>)
        _sets_times.push_back(std::chrono::steady_clock::now());
    
    set_immediate_no_timer(index, value);
}

template<typename T, DynamicStepPromiseMode mode>
void DynamicStepPromise<T, mode>::set_immediate_no_timer(int index, const T& value) {
    this->_values[index] = value;

    {
        std::unique_lock<std::mutex> lck;
        if constexpr (RequiresLockV<mode>)
            lck = std::move(std::unique_lock<std::mutex>(_step_m));

        set_current_index(index);
        _last_unblock_index_strong.store(index, std::memory_order_release);
    }
}

template<typename T, DynamicStepPromiseMode mode>
void DynamicStepPromise<T, mode>::set_immediate(int index, T&& value) {
    if constexpr (IsTimerV<mode>)
        _sets_times.push_back(std::chrono::steady_clock::now());

    set_immediate_no_timer(index, std::move(value));
}

template<typename T, DynamicStepPromiseMode mode>
void DynamicStepPromise<T, mode>::set_immediate_no_timer(int index, T&& value) {
    this->_values[index] = std::move(value);

    {
        std::unique_lock<std::mutex> lck;
        if constexpr (RequiresLockV<mode>)
            lck = std::move(std::unique_lock<std::mutex>(_step_m));

        set_current_index(index);
        _last_unblock_index_strong.store(index, std::memory_order_release);
    }
}

template<typename T, DynamicStepPromiseMode mode>
void DynamicStepPromise<T, mode>::set_step(unsigned int new_step) {
    /* if constexpr (!CanSetStepV<mode>) {
        return;
    } */

    if (new_step == get_step()) {
        return;
    }

    if constexpr (UnblocksV<mode>) {
        if constexpr (RequiresLockV<mode>) {
            std::unique_lock<std::mutex> lck(_step_m);

            if (new_step < _step.load(std::memory_order_relaxed)) {
                if (_current_index.load(std::memory_order_acquire) - _last_unblock_index_strong.load(std::memory_order_acquire) >= new_step) {
                    _last_unblock_index_strong.store(_current_index.load(std::memory_order_acquire), std::memory_order_release);
                }
            }

            _step.store(new_step, std::memory_order_relaxed); // Mutex unblock at end of CS voids the need of release
        } else {
            if (new_step < _step.load(std::memory_order_relaxed)) {
                int current_index = _current_index.load(std::memory_order_relaxed);
                if (current_index - _last_unblock_index_strong.load(std::memory_order_relaxed) >= new_step) {
                    _last_unblock_index_strong.store(current_index, std::memory_order_relaxed);
                }
            }

            _step.store(new_step, std::memory_order_relaxed);
        }
    } else {
        if constexpr (RequiresStrongSyncV<mode>) {
            _step.store(new_step, std::memory_order_release);
        } else {
            _step.store(new_step, std::memory_order_relaxed);
        }
    }
}
