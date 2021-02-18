#ifndef PROMISE_PLUS_H
#define PROMISE_PLUS_H

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "utils.h"

class PromisePlusBase {
public:
    PromisePlusBase();
    PromisePlusBase(int max_index);

    virtual ~PromisePlusBase() { }

    virtual void set_max_index(int max_index);

protected:
    int _max_index;
};

/**
 * Base for PromisePlus
 */
template<typename T>
class PromisePlus : public PromisePlusBase {
public:
    PromisePlus();
    PromisePlus(int nb_values, int max_index);

    NO_COPY_T(PromisePlus, T);

    virtual T& get(int index) = 0;
    virtual void set(int index, const T& value) = 0;
    virtual void set(int index, T&& value) = 0;
    virtual void set_immediate(int index, const T& value) = 0;
    virtual void set_immediate(int index, T&& value) = 0;

    inline void set_nb_values(int nb_values) {
        _values.resize(nb_values);
    }

protected:
    std::vector<T> _values;
};

template<>
class PromisePlus<void> : public PromisePlusBase {
public:
    PromisePlus();
    PromisePlus(int max_index);

    NO_COPY_T(PromisePlus, void);

    virtual void get(int index) = 0;
    virtual void set(int index) = 0;
    virtual void set_immediate(int index) = 0;
};

struct PromisePlusAbstractReadyCheck {
    virtual bool ready_index_strong(int index) = 0;
    virtual bool ready_index_weak(int index) = 0;

    virtual int index_strong() = 0;
    virtual int index_weak() = 0;

    void assert_free_index_strong(int index);
    void assert_free_index_weak(int index);
};

template<typename T>
class PromisePlusBuilder {
public:
    virtual PromisePlus<T>* new_promise() const = 0;
};

#include "promise_plus.tpp"

#endif // PROMISE_PLUS_H
