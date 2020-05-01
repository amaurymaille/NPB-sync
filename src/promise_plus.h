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

    inline void set_wait_mode(PromisePlusWaitMode mode) {
        _wait_mode = mode;
    }

    void set_max_index(int max_index);

protected:
    int _max_index;
    /* Index of the last get() we received.
     * If a get asks for a lower index, get() returns immediately.
     */
    int _last_ready_index;
    PromisePlusWaitMode _wait_mode;

    std::atomic<int> _ready_index;
    std::map<int, std::pair<std::mutex, std::condition_variable>> _locks;

private:
    void init_locks();
};

template<typename T>
class PromisePlus : public PromisePlusBase {
public:
    PromisePlus();
    PromisePlus(int nb_values, int max_index, PromisePlusWaitMode wait_mode = DEFAULT_WAIT_MODE);

    PromisePlus(PromisePlus<T> const&) = delete;
    PromisePlus<T>& operator=(PromisePlus<T> const&) = delete;

    T& get(int index);
    std::unique_ptr<T[]> get_slice(int begin, int end, int step = 1);

    void set(int index, const T& value);

    inline void set_nb_values(int nb_values) {
        _values.resize(nb_values);
    }

private:
    std::vector<T> _values;
};

template<>
class PromisePlus<void> : public PromisePlusBase {
public:
    PromisePlus();
    PromisePlus(int max_index, PromisePlusWaitMode wait_mode = DEFAULT_WAIT_MODE);

    PromisePlus(PromisePlus<void> const&) = delete;
    PromisePlus<void>& operator=(PromisePlus<void> const&) = delete;

    void get(int index);
    void get_slice(int begin, int end, int step = 1);

    void set(int index);
};

#include "promise_plus.tpp"

#endif // PROMISE_PLUS_H
