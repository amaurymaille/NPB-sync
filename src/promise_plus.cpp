#include <cassert>

#include <omp.h>

#include <sstream>
#include <stdexcept>

#include "promise_plus.h"

static void assert_free_index_throw(int index, std::string const& mode, int found);

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

void PromisePlusAbstractReadyCheck::assert_free_index_strong(int index) {
#ifndef NDEBUG
    if (ready_index_strong(index)) {
        assert_free_index_throw(index, "strong", index_strong());
    }
#endif
}

void PromisePlusAbstractReadyCheck::assert_free_index_weak(int index) {
#ifndef NDEBUG
    if (ready_index_weak(index)) {
        assert_free_index_throw(index, "weak", index_weak());
    }
#endif
}

void assert_free_index_throw(int index, std::string const& mode, int found) {
    std::ostringstream str;
    str << "Thread " << omp_get_thread_num() << ", PromisePlus: index " << index << " already fulfilled (" << mode << ": " << found << ")" << std::endl;
    fprintf(stderr, "%s\n", str.str().c_str());
    fflush(stdout);
    // std::cerr << str.str() << std::endl;
    // throw std::runtime_error(str.str());
    assert(false);
}
