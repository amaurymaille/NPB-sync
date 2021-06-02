#ifndef FIFO_H
#define FIFO_H

#include <condition_variable>
#include <mutex>
#include <queue>

#include "tss.h"

enum class FIFOPlusPopPolicy {
    POP_NO_WAIT = 0, // Does not wait if not enough elements are available, return early.
    POP_WAIT = 1, // Wait until there are enough elements. May lead to deadlocks.
};

template<typename T>
class FIFOPlus {
public:
    FIFOPlus(FIFOPlusPopPolicy policy, ThreadIdentifier*, size_t);
    
    // Add the content of elements to the FIFO. It may not be available immediately.
    // template<template<typename> typename Container>
    // void push(Container<T>&& elements);
    // Add the element to the FIFO. It may not be available immediately.
    void push(const T& value);
    // Extract n elements from the FIFO according to the pop policy.
    template<template<typename> typename Container>
    void pop(Container<T>& target, size_t n);
    // Extract all elements from the FIFO without considering the policy.
    template<template<typename> typename Container>
    void empty(Container<T>& target);

    inline void set_pop_policy(FIFOPlusPopPolicy policy) {
        _pop_policy = policy;
    }
    
    inline FIFOPlusPopPolicy get_pop_policy() const {
        return _pop_policy;
    }

    // Return the amount of elements in the active buffer.
    inline size_t get_nb_elements() const {
        return _buffer.size();
    }

    inline unsigned int get_n() const {
        return _producers_data->_n;
    }

    inline void set_n(unsigned int n) {
        _producers_data->_n = n;
    }

private:
    struct ProducerData {
        // Work buffer that is merged into _buffer when required.
        std::queue<T> _inner_buffer;

        // Counters
        unsigned int _n_no_work = 0;
        unsigned int _n_with_work = 0;

        // Thresholds
        unsigned int _no_work_threshold;
        unsigned int _with_work_threshold;
        // Amount of work remaining in the active buffer before transfer
        unsigned int _work_amount_threshold;

        // Quantity of work to have in the inner buffer before transfer
        unsigned int _n;
    };

    TSS<ProducerData> _producers_data;

    // Policy when popping elements
    FIFOPlusPopPolicy _pop_policy;

    // Active buffer that consumers will pick from.
    std::queue<T> _buffer;

    // Mutual exclusion
    std::condition_variable _cv;
    std::mutex _m;

    // Send content of _inner_buffer in _buffer
    void _transfer(); 

    // Reconfigure everything
    void _reconfigure();
};

#include "fifo_plus.tpp"

#endif
