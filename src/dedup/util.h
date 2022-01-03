#ifndef _UTIL_H_
#define _UTIL_H_

#include <cstdlib>

#include <functional>
#include <map>
#include <mutex>

#include <pthread.h>

#include "dedupdef.h"

/* File I/O with error checking */
int xread(int sd, void *buf, size_t len);
int xwrite(int sd, const void *buf, size_t len);

/* Process file header */
int read_header(int fd, byte *compress_type);
int write_header(int fd, byte compress_type);

template<typename T>
class TSLogger {
public:
    TSLogger() { }
    std::pair<size_t, T*>& register_thread(size_t n) {
        std::unique_lock<std::mutex> lck(_m);
        std::pair<size_t, T*>& res = _arrays[pthread_self()];
        res.first = 0;
        res.second = static_cast<T*>(malloc(sizeof(T) * n));
        return res;
    }

    void log(std::function<void(T const&)> const& f) {
        for (auto const& [_, p]: _arrays) {
            auto const& [size, value] = p;
            for (size_t i = 0; i < size; ++i) {
                f(value[i]);
            }
        }
    }

private:
    std::mutex _m;
    std::map<pthread_t, std::pair<size_t, T*>> _arrays;
};

/* struct DeduplicateData {
    unsigned int l1;
    unsigned int l2;
    unsigned long long arrived;
    unsigned long long push;
};

struct CompressData {
    unsigned int l1;
    unsigned int l2;
    unsigned long long in;
    unsigned long long out;
};

extern TSLogger<CompressData> compress_logger;
extern TSLogger<DeduplicateData> deduplicate_logger;
extern TSLogger<unsigned int> deduplicate_locks_logger; */

#endif //_UTIL_H_

