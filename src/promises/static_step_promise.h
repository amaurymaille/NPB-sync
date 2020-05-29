#ifndef STATIC_STEP_PROMISE_H
#define STATIC_STEP_PROMISE_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

#include "promise_plus.h"
#include "utils.h"

#ifndef NDEBUG
    using StaticStepSetMutex = notstd::null_mutex;
#else
    using StaticStepSetMutex = std::mutex;
#endif

class StaticStepPromiseBase {
public:
    StaticStepPromiseBase(int nb_values, unsigned int step);

    unsigned int _step;
    // Will increase when enough set()s are performed
    std::atomic<int> _current_index_strong;

#ifndef NDEBUG
    // Will increase as set()s are performed
    std::atomic<int> _current_index_internal_strong;
#endif

    int _current_index_weak;

#ifndef NDEBUG
    int _current_index_internal_weak;
#endif

    // std::unique_ptr<std::pair<std::mutex, std::condition_variable>[]> _wait_m;
    StaticStepSetMutex _set_m;
    std::atomic<bool> _finalized;

    void assert_okay_index(int index, bool passive);
    bool ready_index(int index, bool passive);
    std::mutex _index_m;
    std::condition_variable _cond_m;
};

/**
 * A PromisePlus that works by receiving increasing index values.
 * Given an increment INC, get(0), ..., get(INC - 1) will unlock when set(INC, value)
 * is performed.
 * 
 * Debug mode ensures that index are indeed received in increasing order. In release
 * mode not performing set()s in the right order will result in undefined behaviour.
 */
template<typename T>
class StaticStepPromise : public PromisePlus<T> {
public:
    StaticStepPromise(int nb_values, unsigned int step, 
                      PromisePlusWaitMode wait_mode = PromisePlusBase::DEFAULT_WAIT_MODE);
    
    NO_COPY_T(StaticStepPromise, T);

    T& get(int index);
    void set(int index, const T& value);
    void set(int index, T&& value);
    void set_final(int index, const T& value);
    void set_final(int index, T&& value);

    bool passive() const { return true; }

private:
    StaticStepPromiseBase _base;
};

template<>
class StaticStepPromise<void> : public PromisePlus<void> {
public:
    StaticStepPromise(int nb_values, unsigned int step, 
                      PromisePlusWaitMode wait_mode = PromisePlusBase::DEFAULT_WAIT_MODE);
    
    NO_COPY_T(StaticStepPromise, void);

    void get(int index);
    void set(int index);
    void set_final(int index);

    bool passive() const { return true; }

private:
    StaticStepPromiseBase _base;
};

template<typename T>
class StaticStepPromiseBuilder : public PromisePlusBuilder<T> {
public:
    StaticStepPromiseBuilder(int nb_values, unsigned int step, 
                             PromisePlusWaitMode wait_mode = PromisePlusBase::DEFAULT_WAIT_MODE) {
        _nb_values = nb_values;
        _step = step;
        _wait_mode = wait_mode;
    }

    PromisePlus<T>* new_promise() const {
        return new StaticStepPromise<T>(_nb_values, _step, _wait_mode);
    }

private:
    int _nb_values;
    PromisePlusWaitMode _wait_mode;
    unsigned int _step;
};

#include "static_step_promise/static_step_promise.tpp"

#endif // STATIC_STEP_PROMISE_H
