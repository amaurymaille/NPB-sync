#ifndef STATIC_STEP_PROMISE_H
#define STATIC_STEP_PROMISE_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#ifdef PROMISE_PLUS_DEBUG_COUNTERS
#  include <tuple>
#endif 
#include <vector>

#include "promise_plus.h"
#include "utils.h"

#ifndef NDEBUG
    using StaticStepSetMutex = notstd::null_mutex;
#else
    using StaticStepSetMutex = std::mutex;
#endif

template<typename T>
class StaticStepPromiseBuilder : public PromisePlusBuilder<T> {
public:
    StaticStepPromiseBuilder(int, unsigned int, unsigned int, PromisePlusWaitMode wait_mode = PromisePlusBase::DEFAULT_WAIT_MODE);
    PromisePlus<T>* new_promise() const;

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

#ifdef PROMISE_PLUS_DEBUG_COUNTERS
    uint64              _nb_wait_loops = 0;
    uint64              _nb_get_strong = 0;
    uint64              _nb_get_weak   = 0;
#endif
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

    friend PromisePlus<T>* StaticStepPromiseBuilder<T>::new_promise() const;

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

#ifdef PROMISE_PLUS_DEBUG_COUNTERS
    std::tuple<uint64, uint64, uint64> get_debug_data() const {
        auto& base = _base._common;
        return std::make_tuple(base._nb_wait_loops, base._nb_get_strong, base._nb_get_weak);
    }
#endif

    friend PromisePlus<void>* StaticStepPromiseBuilder<void>::new_promise() const;

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

    friend PromisePlus<T>* StaticStepPromiseBuilder<T>::new_promise() const;

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

    friend PromisePlus<void>* StaticStepPromiseBuilder<void>::new_promise() const;

private:
    PassiveStaticStepPromiseBase _base;
};

#include "static_step_promise/static_step_promise.tpp"

#endif // STATIC_STEP_PROMISE_H
