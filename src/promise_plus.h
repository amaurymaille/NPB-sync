#ifndef PROMISE_PLUS_H
#define PROMISE_PLUS_H

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

enum class PromisePlusWaitMode : unsigned char {
    PASSIVE,
    ACTIVE,
};

class PromisePlusBase {
public:
#ifdef ACTIVE_PROMISES
    static constexpr PromisePlusWaitMode DEFAULT_WAIT_MODE = PromisePlusWaitMode::ACTIVE;
#else
    static constexpr PromisePlusWaitMode DEFAULT_WAIT_MODE = PromisePlusWaitMode::PASSIVE;
#endif

    PromisePlusBase();
    PromisePlusBase(int max_index, PromisePlusWaitMode wait_mode);

    virtual ~PromisePlusBase() { }

    inline void set_wait_mode(PromisePlusWaitMode mode) {
        _wait_mode = mode;
    }

    inline bool passive() const {
        return _wait_mode == PromisePlusWaitMode::PASSIVE;
    }

    inline bool active() const {
        return _wait_mode == PromisePlusWaitMode::ACTIVE;
    }

    virtual void set_max_index(int max_index);

protected:
    int _max_index;
    /* Index of the last get() we received.
     * If a get asks for a lower index, get() returns immediately without having
     * to wait.
     */
    int _last_ready_index;

    /// Wait on atomic if active, on condition variable if passive
    PromisePlusWaitMode _wait_mode;
};

/**
 * Base for PromisePlus
 */
template<typename T>
class PromisePlus : public PromisePlusBase {
public:
    PromisePlus();
    PromisePlus(int nb_values, int max_index, PromisePlusWaitMode wait_mode = DEFAULT_WAIT_MODE);

    PromisePlus(PromisePlus<T> const&) = delete;
    PromisePlus<T>& operator=(PromisePlus<T> const&) = delete;

    virtual T& get(int index) = 0;
    virtual void set(int index, const T& value) = 0;
    virtual void set(int index, T&& value) = 0;
    virtual void set_final(int index, const T& value) = 0;
    virtual void set_final(int index, T&& value) = 0;

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
    PromisePlus(int max_index, PromisePlusWaitMode wait_mode = DEFAULT_WAIT_MODE);

    PromisePlus(PromisePlus<void> const&) = delete;
    PromisePlus<void>& operator=(PromisePlus<void> const&) = delete;

    virtual void get(int index) = 0;
    virtual void set(int index) = 0;
    virtual void set_final(int index) = 0;
};

template<typename T>
class PromisePlusBuilder {
public:
    virtual PromisePlus<T>* new_promise() const = 0;
};

#include "promise_plus.tpp"

#endif // PROMISE_PLUS_H
