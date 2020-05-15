#include <cassert>

#include "promise_plus.h"

PromisePlusBase::PromisePlusBase() {
    _max_index = -1;
    _wait_mode = DEFAULT_WAIT_MODE;
}

PromisePlusBase::PromisePlusBase(int max_index, PromisePlusWaitMode wait_mode) {
    _max_index = max_index;
    _wait_mode = wait_mode;
}

void PromisePlusBase::set_max_index(int max_index) {
    _max_index = max_index;
}

PromisePlus<void>::PromisePlus() : PromisePlusBase() {
    
}

PromisePlus<void>::PromisePlus(int max_index, PromisePlusWaitMode wait_mode) : PromisePlusBase(max_index, wait_mode) {
    
}