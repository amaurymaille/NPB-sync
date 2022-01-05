#pragma once

#include <cstdlib>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>

template<typename T>
class Ringbuffer {
    public:
        Ringbuffer(size_t size) {
            init(size);
        }

        ~Ringbuffer() {
            free(_data);
        }

        bool empty() const {
            return _tail == _head;
        }

        bool full() const {
            return _head == (_tail - 1 + _size) % _size;
        }

        std::optional<T> pop() {
            std::optional<T> result;
            if (empty()) {
                return std::nullopt;
            } else {
                result = _data[_tail];
                ++_tail;
                if (_tail >= _size) {
                    _tail = 0;
                }
            }

            --_n_elements;
            return result;
        }

        int push(T const& data) {
            if (full()) {
                return -1;
            }

            _data[_head] = data;
            _head++;
            if (_head == _size) {
                _head = 0;
            }

            ++_n_elements;
            return 0;
        }

        size_t n_elements() const {
            return _n_elements;
        }

        void reinit(size_t size) {
            free(_data);
            init(size);
        }

        size_t size() const {
            return _size;
        }
    private:
        T* _data;
        size_t _n_elements;
        size_t _size;
        int _head, _tail;

        void init(size_t size) {
            _data = static_cast<T*>(malloc(sizeof(T) * (size + 1)));
            _size = size + 1;
            _head = _tail = _n_elements = 0;

            if (_data == nullptr) {
                throw std::runtime_error("Not enough memory");
            }

        }
};

template<typename T>
class NaiveQueue {
    public:
        NaiveQueue(size_t size, int n_producers) : _buf(size) {
            _n_producers = n_producers;
            _n_terminated = 0;
        }

        ~NaiveQueue() {

        }

        void terminate() {
            std::unique_lock<std::mutex> lck(_mutex);
            ++_n_terminated; 

            if (terminated()) {
                _not_empty.notify_all();
            }
        }

        int dequeue(Ringbuffer<T>* buf, int limit) {
            std::unique_lock<std::mutex> lck(_mutex);
            while (_buf.empty() && !terminated()) {
                _not_empty.wait(lck);
            }

            if (_buf.empty() && terminated()) {
                return -1;
            }

            int i = 0;
            for (; i < limit && !_buf.empty() && !buf->full(); ++i) {
                std::optional<T> temp = _buf.pop();
                buf->push(*temp);
            }

            if (i > 0) {
                _not_full.notify_all();
            }

            return i;
        }

        int enqueue(Ringbuffer<T>* buf, int limit) {
            std::unique_lock<std::mutex> lck(_mutex);
            while (_buf.full()) {
                _not_full.wait(lck);
            }

            int i = 0;
            for (; i < limit && !_buf.full() && !buf->empty(); ++i) {
                std::optional<T> temp = buf->pop();
                _buf.push(*temp);
            }

            if (i > 0) {
                _not_empty.notify_all();
            }

            return i;
        }

        unsigned int size() {
            return _buf._n_elements();
        }

    private:
        Ringbuffer<T> _buf;
        int _n_producers;
        int _n_terminated;
        std::mutex _mutex;
        std::condition_variable _not_empty, _not_full;


        bool terminated() const {
            return _n_producers == _n_terminated;
        }
};
