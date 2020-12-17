#include <sstream>
#include <stdexcept>
#include <utility>

template<typename T>
PassiveNaivePromise<T>::PassiveNaivePromise(int nb_values) :
    PromisePlus<T>(nb_values), _base(nb_values) {
    
}

template<typename T>
ActiveNaivePromise<T>::ActiveNaivePromise(int nb_values) :
    PromisePlus<T>(nb_values), _base(nb_values) {
    
}

template<typename T>
T& PassiveNaivePromise<T>::get(int index) {
    if (!_base.ready_index_strong(index)) {
        std::unique_lock<std::mutex> lck(_base._wait[index].first);
        while (!_base._ready[index])
            _base._wait[index].second.wait(lck);
    }
        
    return this->_values[index];
}

template<typename T>
T& ActiveNaivePromise<T>::get(int index) {
    if (!_base.ready_index_strong(index)) {
        while (!_base._ready[index].load(std::memory_order_acquire))
            ;
    }

    return this->_values[index];
}

template<typename T>
void PassiveNaivePromise<T>::set(int index, const T& value) {
    set_maybe_check(index, value, true);
}

template<typename T>
void PassiveNaivePromise<T>::set(int index, T&& value) {
    set_maybe_check(index, std::move(value), true);
}

template<typename T>
void PassiveNaivePromise<T>::set_immediate(int index, const T& value) {
    set_maybe_check(index, value, false);
}

template<typename T>
void PassiveNaivePromise<T>::set_immediate(int index, T&& value) {
    set_maybe_check(index, std::move(value), false);
}

template<typename T>
void ActiveNaivePromise<T>::set(int index, const T& value) {
    set_maybe_check(index, value, true);
}

template<typename T>
void ActiveNaivePromise<T>::set(int index, T&& value) {
    set_maybe_check(index, std::move(value), true);
}

template<typename T>
void ActiveNaivePromise<T>::set_immediate(int index, const T& value) {
    set_maybe_check(index, value, false);
}

template<typename T>
void ActiveNaivePromise<T>::set_immediate(int index, T&& value) {
    set_maybe_check(index, std::move(value), false);
}

template<typename T>
void PassiveNaivePromise<T>::set_maybe_check(int index, T const& value, bool check) {
    std::unique_lock<NaiveSetMutex> lock_s(_base._common._set_m[index]);

    if (check)
        _base.assert_free_index_strong(index);

    std::unique_lock<std::mutex> lck(_base._wait[index].first);
    this->_values[index] = value;
    _base._ready[index] = true;
    _base._wait[index].second.notify_all();
}

template<typename T>
void PassiveNaivePromise<T>::set_maybe_check(int index, T&& value, bool check) {
    std::unique_lock<NaiveSetMutex> lock_s(_base._common._set_m[index]);

    if (check)
        _base.assert_free_index_strong(index);

    std::unique_lock<std::mutex> lck(_base._wait[index].first);
    this->_values[index] = std::move(value);
    _base._ready[index] = true;
    _base._wait[index].second.notify_all();
}

template<typename T>
void ActiveNaivePromise<T>::set_maybe_check(int index, T const& value, bool check) {
    std::unique_lock<NaiveSetMutex> lock_s(_base._common._set_m[index]);

    if (check)
        _base.assert_free_index_strong(index);

    this->_values[index] = value;
    _base._ready[index].store(true, std::memory_order_release);
}

template<typename T>
void ActiveNaivePromise<T>::set_maybe_check(int index, T&& value, bool check) {
    std::unique_lock<NaiveSetMutex> lock_s(_base._common._set_m[index]);

    if (check)
        _base.assert_free_index_strong(index);

    this->_values[index] = std::move(value);
    _base._ready[index].store(true, std::memory_order_release);
}