#ifndef SSPP_H
#define SSPP_H

#include <atomic>
#include <mutex>
#include <vector>

class StaticStepPromisePlus {
public:
    StaticStepPromisePlus(unsigned int step) : _step(step) {
        _current_index_strong.store(-1, std::memory_order_relaxed);
        _current_index_weak.resize(100);
        std::fill(_current_index_weak.begin(), _current_index_weak.end(), -1);
    }

    inline bool ready_index_strong(int index) {
        return _current_index_strong.load(std::memory_order_release) >= index;
    }

    inline bool ready_index_weak(int index) {
        return _current_index_weak[omp_get_thread_num()] >= index;
    }

    inline void get(int index) {
        if (!ready_index_weak(index)) {
            int ready_index = _current_index_strong.load(std::memory_order_acquire);
            while (ready_index < index)
                ready_index = _current_index_strong.load(std::memory_order_acquire);

            // Is load necessary ?
            _current_index_weak[omp_get_thread_num()] = _current_index_strong.load(std::memory_order_acquire);
        }
    }

    inline void set(int index) {
        std::unique_lock<std::mutex> lck(_set_m);

        if (index - _current_index_strong.load(std::memory_order_acquire) >= _step) {
            _current_index_strong.store(index, std::memory_order_release);
        }
    }

    inline void set_final(int index) {
        std::unique_lock<std::mutex> lck(_set_m);
        _current_index_strong.store(index, std::memory_order_release);
    }

private:
    unsigned int _step;
    std::atomic<int> _current_index_strong;
    std::vector<int> _current_index_weak;
    std::mutex _set_m;
};

#endif // SSPP_H
