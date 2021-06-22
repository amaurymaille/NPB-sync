#ifndef FIFO_H
#define FIFO_H

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

#include "tss.h"

enum class FIFOPlusPopPolicy {
    POP_NO_WAIT = 0, // Does not wait if not enough elements are available, return early.
    POP_WAIT = 1, // Wait until there are enough elements. May lead to deadlocks.
};

enum class FIFORole {
    NONE = 0,
    PRODUCER = 1,
    CONSUMER = 2
};

template<typename T>
class FIFOPlus {
public:
    FIFOPlus(FIFOPlusPopPolicy policy, ThreadIdentifier*, size_t n_producers, size_t n_consumers);
    
    // Add the content of elements to the FIFO. It may not be available immediately.
    // template<template<typename> typename Container>
    // void push(Container<T>&& elements);
    // Add the element to the FIFO. It may not be available immediately.
    void push(const T& value, bool reconfigure = true);

    // Add the element to the FIFO and make it available immediately (along the
    // previous one).
    void push_immediate(const T& value, bool reconfigure = true);

    // Extract an element from the FIFO.
    void pop(std::optional<T>& value, bool reconfigure = true);
    // Extract n elements from the FIFO according to the pop policy.
    // template<template<typename> typename Container>
    // void pop(Container<T>& target, size_t n);
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
        return _data->_n;
    }

    inline void set_n(unsigned int n) {
        _data->_n = n;
    }

    inline void set_role(FIFORole role) {
        if (role == FIFORole::NONE) {
            throw std::runtime_error("Cannot set the role of a thread to NONE");
        } else if (_data->_role != FIFORole::NONE) {
            throw std::runtime_error("Cannot change the role of a thread that has already selected one");
        }

        _data->_role = role;
    }

    inline void terminate() {
        if (_data->role == FIFORole::PRODUCER) {
            std::unique_lock<std::mutex> lck(_prod_mutex);
            ++_n_producers_done;
        }
    }

    inline void set_thresholds(unsigned int no_work, unsigned int with_work, unsigned int work_amount) {
        _data->_no_work_threshold = no_work;
        _data->_with_work_threshold = with_work;
        _data->_work_amount_threshold = work_amount;
    }

    inline void set_multipliers(float increase, float decrease) {
        _data->_increase_mult = increase;
        _data->_decrease_mult = decrease;
    }

    // Terminated once every producer has called terminate()
    inline bool terminated() const {
        std::unique_lock<std::mutex> lck(_prod_mutex);
        return _n_producers == _n_producers_done;
    }

private:
    enum class ReconfigureReason {
        /* There was work to do, and we went over the threshold */
        WORK = 0,
        /* There was no work to do, and we went over the threshold */
        NO_WORK = 1
    };

    struct ProdConsData {
        // Work buffer. Merged into the active buffer for producers. Inner buffer
        // merged into it for consumers.
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

        // Role
        FIFORole _role = FIFORole::NONE; 

        // Multipliers for increase / decrease of _n
        float _increase_mult;
        float _decrease_mult;

        void transfer();
    };

    TSS<ProdConsData> _data;

    // Policy when popping elements
    FIFOPlusPopPolicy _pop_policy;

    // Active buffer that consumers will pick from.
    std::queue<T> _buffer;

    // Mutual exclusion
    std::condition_variable _cv;
    std::mutex _m;

    // Number of producers
    unsigned int _n_producers = 0;
    // Number of producers that are done
    unsigned int _n_producers_done = 0;
    // Producer count mutex
    std::mutex _prod_mutex;

    /* check_empty parameter could be related to the comment in pop that talks
     * about checking how many times in a row the buffer was empty when a thread
     * tried to pop. I don't see why it would appear in _transfer though.
     */

    // Send content of _inner_buffer in _buffer
    void _transfer(bool empty_check = false); 

    // Reconfigure everything
    void _reconfigure(ReconfigureReason reason);

    // Send content of _buffer to _inner_buffer
    void _reverse_transfer(bool empty_check = false);
};

#include "fifo_plus.tpp"

#endif
