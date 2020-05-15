#include <cassert>

template<typename T>
PromisePlus<T>::PromisePlus() : PromisePlusBase() {
    
}

template<typename T>
PromisePlus<T>::PromisePlus(int nb_values, int max_index, PromisePlusWaitMode wait_mode) : PromisePlusBase(max_index, wait_mode) {
    _values.resize(nb_values);
}

