#ifndef TSS_H
#define TSS_H

#include <omp.h>
#include <pthread.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

class ThreadIdentifier {
public:
    virtual unsigned int thread_id() const = 0;
};

class PThreadThreadIdentifier : public ThreadIdentifier {
public:
    PThreadThreadIdentifier() { }

    void register_thread() {
        std::unique_lock<std::mutex> lck(_m);
        _ids[pthread_self()] = _thread_id++;
    }

    unsigned int thread_id() const override {
        // Fuck you...
        return const_cast<std::remove_const_t<decltype(_ids)>&>(_ids)[pthread_self()];
    }

    int pthread_create(pthread_t* thread, 
                       const pthread_attr_t *attr,
                       void* (*start_routine)(void*),
                       void* arg) {
        deferred_pthread_create* data = new deferred_pthread_create;
        data->_original_arg = arg;
        data->_start_routine = start_routine;
        data->_this = this;
        return ::pthread_create(thread, attr, PThreadThreadIdentifier::run_pthread, data);
    }

private:
    unsigned int _thread_id = 0;
    std::map<pthread_t, unsigned int> _ids;
    std::mutex _m;

    struct deferred_pthread_create {
        void* _original_arg;
        void* (*_start_routine)(void*);
        PThreadThreadIdentifier* _this;
    };

    static void* run_pthread(void* arg) {
        deferred_pthread_create* data = (deferred_pthread_create*)arg;
        data->_this->register_thread();
        void* res = data->_start_routine(data->_original_arg);
        delete data;
        return res;
    }
};

class OMPThreadIdentifier : public ThreadIdentifier {
public:
    OMPThreadIdentifier() { }

    unsigned int thread_id() const override {
        return omp_get_thread_num();
    }
};

template<typename T>
class TSS {
public:
    TSS(ThreadIdentifier* identifier, size_t n) : _identifier(identifier), _values(n) { }

    inline T& operator*() { return _values[_identifier->thread_id()]; }
    inline const T& operator*() const { return const_cast<TSS<T>*>(this)->operator*(); }

    inline T* operator->() { return _values.data() + _identifier->thread_id(); }
    inline const T* operator->() const { return const_cast<TSS<T>*>(this)->operator->(); }

    inline T& get() { return _values[_identifier->thread_id()]; }
    inline void set(const T& value) { _values[_identifier->thread_id()] = value; }
    inline void set(T&& value) { _values[_identifier->thread_id()] = std::move(value); }

private:
    std::unique_ptr<ThreadIdentifier> _identifier;
    std::vector<T> _values;
};

#endif // TSS_H
