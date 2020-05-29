#include <cassert>

#include "promise_plus.h"

PromisePlusBase::PromisePlusBase() {
    _max_index = -1;
}

PromisePlusBase::PromisePlusBase(int max_index) {
    _max_index = max_index;
}

void PromisePlusBase::set_max_index(int max_index) {
    _max_index = max_index;
}

PromisePlus<void>::PromisePlus() : PromisePlusBase() {
    
}

PromisePlus<void>::PromisePlus(int max_index) : PromisePlusBase(max_index) {
    
}