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

class NaivePromiseBase {
public:
    NaivePromiseBase(int nb_values, PromisePlusWaitMode wait_mode = PromisePlusBase::DEFAULT_WAIT_MODE);

    std::unique_ptr<std::atomic<bool>[]> _ready_strong;
    std::unique_ptr<bool[]> _ready_weak;
    std::unique_ptr<std::pair<std::mutex, std::condition_variable>[]> _wait_m;
    std::unique_ptr<NaiveSetMutex[]> _set_m;
};

template<typename T>
class NaivePromise : public PromisePlus<T> {
public:
    NaivePromise(int nb_values, PromisePlusWaitMode wait_mode = PromisePlusBase::DEFAULT_WAIT_MODE);

    NO_COPY_T(NaivePromise, T);

    T& get(int index);
    void set(int index, const T& value);
    void set(int index, T&& value);
    void set_final(int index, const T& value);
    void set_final(int index, T&& value);

private:
    bool ready_index(int index) const;
    void assert_free_index(int index) const;

    NaivePromiseBase _base;
};

template<>
class NaivePromise<void> : public PromisePlus<void> {
public:
    NaivePromise(int nb_values, PromisePlusWaitMode wait_mode = PromisePlusBase::DEFAULT_WAIT_MODE);

    NO_COPY_T(NaivePromise, void);

    void get(int index);
    void set(int index);
    void set_final(int index);

private:
    bool ready_index(int index) const;
    void assert_free_index(int index) const;

    NaivePromiseBase _base;
};

template<typename T>
class NaivePromiseBuilder : public PromisePlusBuilder<T> {
public:
    NaivePromiseBuilder(int nb_values, PromisePlusWaitMode wait_mode = PromisePlusBase::DEFAULT_WAIT_MODE);

    PromisePlus<T>* new_promise() const {
        return new NaivePromise<T>(_nb_values, _wait_mode);
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
