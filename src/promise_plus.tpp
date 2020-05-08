#include <cassert>

template<typename T>
PromisePlus<T>::PromisePlus() : PromisePlusBase() {
    
}

template<typename T>
PromisePlus<T>::PromisePlus(int nb_values, int max_index, PromisePlusWaitMode wait_mode) : PromisePlusBase(max_index, wait_mode) {
    _values.resize(nb_values);
}

template<typename T>
T& PromisePlus<T>::get(int index) {
    if (_last_ready_index <= index)
        return _values[index];

    if (_wait_mode == PromisePlusWaitMode::ACTIVE) {
        while (!(_ready_index.load(std::memory_order_acquire) >= index))
            ;
    } else {
        std::unique_lock<std::mutex> lck(_locks[index].first);
        while (!(_ready_index.load(std::memory_order_acquire) >= index))
            _locks[index].second.wait(lck);
    }

    _last_ready_index = _ready_index.load(std::memory_order_acquire);

    return _values[index];
}

template<typename T>
std::unique_ptr<T[]> PromisePlus<T>::get_slice(int begin, int end, int step) {
    T* values = new T[(end - begin) / step + 1];

    if (_wait_mode == PromisePlusWaitMode::ACTIVE) {
        while (!(_ready_index.load(std::memory_order_acquire) >= end))
            ;
    } else {
        std::unique_lock<std::mutex> lck(_locks[end].first);
        while (!(_ready_index.load(std::memory_order_acquire) >= end))
            _locks[end].second.wait(lck);
    }

    for (int i = begin; i < end; i += step)
        values[i] = _values[i];

    return std::unique_ptr<T[]>(values);
}

template<typename T>
void PromisePlus<T>::set(int index, const T& value) {
    assert(_ready_index.load(std::memory_order_acquire) < index);

    _values[index] = value;
    std::unique_lock<std::mutex> lck(_locks[index].first, std::defer_lock);

    if (_wait_mode == PromisePlusWaitMode::PASSIVE)
        lck.lock();

    _ready_index.store(index, std::memory_order_release);

    if (_wait_mode == PromisePlusWaitMode::PASSIVE)
        _locks[index].second.notify_one();

}
