#include <atomic>


template<typename T>
class NaivePromise {
public:
    NaivePromise() {
        _ready_strong.store(false, std::memory_order_relaxed);
    }

    T& get() {
        while (!_ready_strong.load(std::memory_order_acquire))
            ;

        return _value;
    }

    void set(const T& value) {
        _value = value;
        _ready_strong.store(true, std::memory_order_release);
    }

    void set(T&& value) {
        _value = std::move(value);
        _ready_strong.store(true, std::memory_order_release);
    }

private:
    std::atomic<bool> _ready_strong;
    T _value;
};

template<>
class NaivePromise<void> {
public:
    NaivePromise() {
        _ready_strong.store(false, std::memory_order_relaxed);        
    }

    void set() {
        _ready_strong.store(true, std::memory_order_release);
    }

    void get() {
        while (!_ready_strong.load(std::memory_order_acquire))
            ;
    }

private:
    bool _ready;
    std::atomic<bool> _ready_strong;
};
