#ifndef FIFO_H
#define FIFO_H

#include <cstdio>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <omp.h>
#include <queue>
#include <utility>

template<typename T>
class fifo {
public:
    fifo() { }

    void push(T const& t) {
        std::lock_guard<std::mutex> lck(_m);
        _queue.push(t);
        _empty.notify_all();
    }

    T pop() {
        std::unique_lock<std::mutex> lck(_m);
        while (_queue.empty())
            _empty.wait(lck);

        T v(std::move(_queue.front()));
        _queue.pop();
        return v;
    }

private:
    std::mutex _m;
    std::condition_variable _empty;
    std::queue<T> _queue;
};

template<typename T>
class omp_fifo {
public:
    omp_fifo() { _n.store(0, std::memory_order_relaxed); }

    void push(T const& t) {
        #pragma omp critical(queue)
        {
            _queue.push(t);
        }

        _n++;
    }

    T pop() {
        while (_n.load(std::memory_order_acquire) == 0)
            ;

        T t;
        #pragma omp critical(queue)
        {
            t = _queue.front();
            _queue.pop();
        }

        _n--;
        return t;
    }

private:
    std::queue<T> _queue;
    std::atomic<int> _n;
};

#endif // FIFO_JH
