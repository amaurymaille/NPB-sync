#include <sstream>
#include <stdexcept>
#include <utility>

template<typename T>
NaivePromise<T>::NaivePromise(int nb_values, PromisePlusWaitMode wait_mode) :
    _base(nb_values, wait_mode) {
    
}

template<typename T>
T& NaivePromise<T>::get(int index) {
    if (ready_index(index)) {
        return this->_values[index];
    } else {
        if (this->passive()) {
            std::unique_lock<std::mutex> lck(_base._wait_m[index].first);
            while (!_base._ready_weak[index])
                _base._wait_m[index].second.wait(lck);
        } else {
            while (!_base._ready_strong[index].load(std::memory_order_acquire))
                ;
        }

        return this->_values[index];
    }
}

template<typename T>
void NaivePromise<T>::assert_free_index(int index) const {
#ifndef NDEBUG
    if (ready_index(index)) {
        std::stringstream str;
        str << "NaivePromise: index " << index << " already fulfiled" << std::endl;
        throw std::runtime_error(str.str());
    }
#endif
}

template<typename T>
void NaivePromise<T>::set(int index, const T& value) {
    set_maybe_check(index, value, true);
}

template<typename T>
void NaivePromise<T>::set(int index, T&& value) {
    set_maybe_check(index, std::move(value), true);
}

template<typename T>
void NaivePromise<T>::set_final(int index, const T& value) {
    set_maybe_check(index, value, false);
}

template<typename T>
void NaivePromise<T>::set_final(int index, T&& value) {
    set_maybe_check(index, std::move(value), false);
}

template<typename T>
void NaivePromise<T>::set_maybe_check(int index, T&& value, bool check) {
    std::unique_lock<NaiveSetMutex> lock_s(_base._set_m[index]);

    if (check)
        assert_free_index(index);

    if (this->passive()) {
        std::unique_lock<std::mutex> lck(_base._wait_m[index].first);
        this->_values[index] = std::move(value);
        _base._ready_weak[index] = true;
        _base._wait_m[index].second.notify_all();
    } else if (this->active()) {
        this->_values[index] = std::move(value);
        _base._ready_strong[index].store(true, std::memory_order_consume);
    }
}

template<typename T>
bool NaivePromise<T>::ready_index(int index) const {
    return ((this->passive() && _base._ready_weak[index]) ||
            (this->active() && _base._ready_strong[index].load(std::memory_order_acquire)));
}