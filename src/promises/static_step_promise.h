#ifndef STATIC_STEP_PROMISE_H
#define STATIC_STEP_PROMISE_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

#include <boost/thread/tss.hpp>

#include "promise_plus.h"
#include "utils.h"

#ifndef NDEBUG
    using StaticStepSetMutex = notstd::null_mutex;
#else
    using StaticStepSetMutex = std::mutex;
#endif

template<typename T>
using tss = boost::thread_specific_ptr<T>;

struct StaticStepPromiseCommonBase {
    StaticStepPromiseCommonBase(unsigned int step);

    StaticStepSetMutex  _set_m;
    const unsigned int  _step;

    tss<int>            _current_index_weak;
};

struct ActiveStaticStepPromiseBase : public PromisePlusAbstractReadyCheck {
    

    ActiveStaticStepPromiseBase(unsigned int step);

    StaticStepPromiseCommonBase _common;
    std::atomic<int>            _current_index_strong;

    bool ready_index_strong(int index) const final;
    bool ready_index_weak(int index) const final;
};

struct PassiveStaticStepPromiseBase : public PromisePlusAbstractReadyCheck {
    PassiveStaticStepPromiseBase(unsigned int step);

    StaticStepPromiseCommonBase _common;
    int                         _current_index_strong;
    // Yes, so, locking a mutex is modifying it, even though ready_index_strong 
    // is const. Thanks, I hate it.
    mutable std::mutex          _index_m;
    std::condition_variable     _index_c;

    bool ready_index_strong(int index) const final;
    bool ready_index_weak(int index) const final;
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
class ActiveStaticStepPromise : public PromisePlus<T> {
public:
    ActiveStaticStepPromise(int nb_values, unsigned int step);
    
    NO_COPY_T(ActiveStaticStepPromise, T);

    T& get(int index);
    void set(int index, const T& value);
    void set(int index, T&& value);
    void set_final(int index, const T& value);
    void set_final(int index, T&& value);

private:
    ActiveStaticStepPromiseBase _base;
};

template<>
class ActiveStaticStepPromise<void> : public PromisePlus<void> {
public:
    ActiveStaticStepPromise(int nb_values, unsigned int step);
    
    NO_COPY_T(ActiveStaticStepPromise, void);

    void get(int index);
    void set(int index);
    void set_final(int index);

private:
    ActiveStaticStepPromiseBase _base;
};

template<typename T>
class PassiveStaticStepPromise : public PromisePlus<T> {
public:
    PassiveStaticStepPromise(int nb_values, unsigned int step);
    
    NO_COPY_T(PassiveStaticStepPromise, T);

    T& get(int index);
    void set(int index, const T& value);
    void set(int index, T&& value);
    void set_final(int index, const T& value);
    void set_final(int index, T&& value);

private:
    PassiveStaticStepPromiseBase _base;
};

template<>
class PassiveStaticStepPromise<void> : public PromisePlus<void> {
public:
    PassiveStaticStepPromise(int nb_values, unsigned int step);
    
    NO_COPY_T(PassiveStaticStepPromise, void);

    void get(int index);
    void set(int index);
    void set_final(int index);

private:
    PassiveStaticStepPromiseBase _base;
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
        if (_wait_mode == PromisePlusWaitMode::ACTIVE) {
            return new ActiveStaticStepPromise<T>(_nb_values, _step);
        } else {
            return new PassiveStaticStepPromise<T>(_nb_values, _step);
        }
    }

private:
    int _nb_values;
    PromisePlusWaitMode _wait_mode;
    unsigned int _step;
};

#include "static_step_promise/static_step_promise.tpp"

#endif // STATIC_STEP_PROMISE_H
