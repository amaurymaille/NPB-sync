#include <cassert>

#include <sstream>
#include <stdexcept>

#include "promise_plus.h"

static void assert_free_index_throw(int index, std::string const& mode);

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

void PromisePlusAbstractReadyCheck::assert_free_index_strong(int index) const {
#ifndef NDEBUG
    if (ready_index_strong(index)) {
        assert_free_index_throw(index, "strong");
    }
#endif
}

void PromisePlusAbstractReadyCheck::assert_free_index_weak(int index) const {
#ifndef NDEBUG
    if (ready_index_weak(index)) {
        assert_free_index_throw(index, "weak");
    }
#endif
}

void assert_free_index_throw(int index, std::string const& mode) {
    std::ostringstream str;
    str << "PromisePlus: index " << index << " already fulfilled (" << mode << ")" << std::endl;
    throw std::runtime_error(str.str());
}