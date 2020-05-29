#include <cassert>

template<typename T>
PromisePlus<T>::PromisePlus() : PromisePlusBase() {
    
}

template<typename T>
PromisePlus<T>::PromisePlus(int nb_values, int max_index) : PromisePlusBase(max_index) {
    _values.resize(nb_values);
}

