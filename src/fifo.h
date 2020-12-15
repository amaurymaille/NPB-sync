#ifndef FIFO_H
#define FIFO_H

#include <condition_variable>
#include <mutex>
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

    void pop(T&& t) {
        std::unique_lock<std::mutex> lck(_m);
        while (_queue.empty())
            _empty.wait(lck);

        t = std::move(_queue.front());
        _queue.pop();
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

#endif // FIFO_JH
