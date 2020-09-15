#ifndef STATIC_STEP_PROMISE_H
#define STATIC_STEP_PROMISE_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>


#include "promise_plus.h"
#include "utils.h"

#ifndef NDEBUG
    using StaticStepSetMutex = notstd::null_mutex;
#else
    using StaticStepSetMutex = std::mutex;
#endif

class StaticStepPromiseBuilder {
public:
    StaticStepPromiseBuilder(int, unsigned int, unsigned int, PromisePlusWaitMode wait_mode = PromisePlusBase::DEFAULT_WAIT_MODE);
    ActiveStaticStepPromise* new_promise() const;

private:
    int _nb_values;
    PromisePlusWaitMode _wait_mode;
    unsigned int _step;
    unsigned int _n_threads;
};

struct StaticStepPromiseCommonBase {
    StaticStepPromiseCommonBase(unsigned int step);

    StaticStepSetMutex  _set_m;
    const unsigned int  _step;

    std::vector<int>    _current_index_weak;
};

struct ActiveStaticStepPromiseBase : public PromisePlusAbstractReadyCheck {
    ActiveStaticStepPromiseBase(unsigned int step);

    StaticStepPromiseCommonBase _common;
    std::atomic<int>            _current_index_strong;

    bool ready_index_strong(int index) final;
    bool ready_index_weak(int index) final;

    int index_strong() final { return _current_index_strong.load(std::memory_order_acquire); }
    int index_weak() final { return _common._current_index_weak[omp_get_thread_num()]; }
};

struct PassiveStaticStepPromiseBase : public PromisePlusAbstractReadyCheck {
    PassiveStaticStepPromiseBase(unsigned int step);

    StaticStepPromiseCommonBase _common;
    int                         _current_index_strong;
    std::mutex                  _index_m;
    std::condition_variable     _index_c;

    bool ready_index_strong(int index) final;
    bool ready_index_weak(int index) final;

    int index_strong() final { std::unique_lock<std::mutex> lck(_index_m); return _current_index_strong; }
    int index_weak() final { return _common._current_index_weak[omp_get_thread_num()]; }
};

/**
 * A PromisePlus that works by receiving increasing index values.
 * Given an increment INC, get(0), ..., get(INC - 1) will unlock when set(INC, value)
 * is performed.
 * 
 * Debug mode ensures that index are indeed received in increasing order. In release
 * mode not performing set()s in the right order will result in undefined behaviour.
 */
/* template<typename T>
class ActiveStaticStepPromise : public PromisePlus<T> {
public:
    ActiveStaticStepPromise(int nb_values, unsigned int step);
    
    NO_COPY_T(ActiveStaticStepPromise, T);

    T& get(int index);
    void set(int index, const T& value);
    void set(int index, T&& value);
    void set_final(int index, const T& value);
    void set_final(int index, T&& value);

    friend ActiveStaticStepPromise<T>* StaticStepPromiseBuilder<T>::new_promise() const;

private:
    ActiveStaticStepPromiseBase _base;
}; */

class ActiveStaticStepPromise {
public:
    ActiveStaticStepPromise(int nb_values, unsigned int step);
    
    // NO_COPY_T(ActiveStaticStepPromise, void);

    inline void get(int index) __attribute__((always_inline)) {
        int ready_index = _base._current_index_strong.load(std::memory_order_acquire);
        while (ready_index < index)
            ready_index = _base._current_index_strong.load(std::memory_order_acquire);
    }

    inline void set(int index) __attribute__((always_inline)) {
        _base._current_index_strong.store(index, std::memory_order_release);
    }

    inline void set_final(int index) __attribute__((always_inline)) {
        _base._current_index_strong.store(index, std::memory_order_release);
    }

    friend ActiveStaticStepPromise* StaticStepPromiseBuilder::new_promise() const;

private:
    ActiveStaticStepPromiseBase _base;
};

template<typename T>
class PassiveStaticStepPromise {
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
class PassiveStaticStepPromise<void> {
public:
    PassiveStaticStepPromise(int nb_values, unsigned int step);
    
    NO_COPY_T(PassiveStaticStepPromise, void);

    void get(int index);
    void set(int index);
    void set_final(int index);

private:
    PassiveStaticStepPromiseBase _base;
};

#include "static_step_promise/static_step_promise.tpp"

#endif // STATIC_STEP_PROMISE_H
