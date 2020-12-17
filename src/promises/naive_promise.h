#ifndef NAIVE_PROMISE_H
#define NAIVE_PROMISE_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

#include "promise_plus.h"
#include "utils.h"

#ifndef NDEBUG
    using NaiveSetMutex = notstd::null_mutex;
#else
    using NaiveSetMutex = std::mutex;
#endif

struct NaivePromiseCommonBase {
    NaivePromiseCommonBase(int nb_values);
    std::unique_ptr<NaiveSetMutex[]> _set_m;
};

struct ActiveNaivePromiseBase : public PromisePlusAbstractReadyCheck {
    ActiveNaivePromiseBase(int nb_values);

    std::unique_ptr<std::atomic<bool>[]> _ready;
    NaivePromiseCommonBase _common;

    bool ready_index_strong(int index) final;
    bool ready_index_weak(int index) final;

    int index_strong() final { return -1; }
    int index_weak() final { return -1; }
};

struct PassiveNaivePromiseBase : public PromisePlusAbstractReadyCheck {
    PassiveNaivePromiseBase(int nb_values);

    std::unique_ptr<bool[]> _ready;
    std::unique_ptr<std::pair<std::mutex, std::condition_variable>[]> _wait;
    NaivePromiseCommonBase _common;

    bool ready_index_strong(int index) final;
    bool ready_index_weak(int index) final;

    int index_strong() final { return -1; }
    int index_weak() final { return -1; }
};

template<typename T>
class ActiveNaivePromise : public PromisePlus<T> {
public:
    ActiveNaivePromise(int nb_values);

    NO_COPY_T(ActiveNaivePromise, T);

    T& get(int index);
    void set(int index, const T& value);
    void set(int index, T&& value);
    void set_immediate(int index, const T& value);
    void set_immediate(int index, T&& value);

private:
    void set_maybe_check(int index, const T& value, bool check);
    void set_maybe_check(int index, T&& value, bool check);

    ActiveNaivePromiseBase _base;
};

template<typename T>
class PassiveNaivePromise : public PromisePlus<T> {
public:
    PassiveNaivePromise(int nb_values);

    NO_COPY_T(PassiveNaivePromise, T);

    T& get(int index);
    void set(int index, const T& value);
    void set(int index, T&& value);
    void set_immediate(int index, const T& value);
    void set_immediate(int index, T&& value);

private:
    void set_maybe_check(int index, const T& value, bool check);
    void set_maybe_check(int index, T&& value, bool check);

    PassiveNaivePromiseBase _base;
};

template<>
class ActiveNaivePromise<void> : public PromisePlus<void> {
public:
    ActiveNaivePromise(int nb_values);

    NO_COPY_T(ActiveNaivePromise, void);

    void get(int index);
    void set(int index);
    void set_immediate(int index);

private:
    void set_maybe_check(int index, bool check);

    ActiveNaivePromiseBase _base;
};

template<>
class PassiveNaivePromise<void> : public PromisePlus<void> {
public:
    PassiveNaivePromise(int nb_values);

    NO_COPY_T(PassiveNaivePromise, void);

    void get(int index);
    void set(int index);
    void set_immediate(int index);

private:
    void set_maybe_check(int index, bool check);

    PassiveNaivePromiseBase _base;
};

template<typename T>
class NaivePromiseBuilder : public PromisePlusBuilder<T> {
public:
    NaivePromiseBuilder(int nb_values, PromisePlusWaitMode wait_mode = PromisePlusBase::DEFAULT_WAIT_MODE);

    PromisePlus<T>* new_promise() const {
        if (_wait_mode == PromisePlusWaitMode::ACTIVE)
            return new ActiveNaivePromise<T>(_nb_values);
        else
            return new PassiveNaivePromise<T>(_nb_values);
    }

private:
    int _nb_values;
    PromisePlusWaitMode _wait_mode;
};

template<typename T>
NaivePromiseBuilder<T>::NaivePromiseBuilder(int nb_values, PromisePlusWaitMode wait_mode) : 
_nb_values(nb_values), _wait_mode(wait_mode) {

}

#include "naive_promise/naive_promise.tpp"

#endif // NAIVE_PROMISE_H
