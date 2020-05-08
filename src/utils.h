#ifndef UTILS_H
#define UTILS_H

#include <atomic>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <tuple>
#include <utility>

#include "defines.h"

struct timespec;

template<typename IntType>
class RandomGenerator {
public:
    template<typename... Args>
    RandomGenerator(Args&&... args) : _generator(std::random_device()()), _distribution(std::forward<Args>(args)...) {

    }

    IntType operator()() {
        return _distribution(_generator);
    }

private:
    std::mt19937 _generator;
    std::uniform_int_distribution<IntType> _distribution;
};

class DeadlockDetector {
public:
    DeadlockDetector(uint64 limit);
    void reset();
    void run();
    void stop();

private:
    uint64 _limit;
    std::atomic<unsigned int> _reset_count;
    std::atomic<bool> _running;
};

namespace Globals {
    extern RandomGenerator<unsigned int> sleep_generator;
    extern RandomGenerator<unsigned char> binary_generator;

    extern DeadlockDetector deadlock_detector;
    extern std::thread deadlock_detector_thread;
}

template<typename T, typename R>
auto count_duration_cast(std::chrono::duration<R> const& tp) {
    return std::chrono::duration_cast<T>(tp).count();
}

size_t to1d(size_t w, size_t x, size_t y, size_t z);
std::tuple<size_t, size_t, size_t, size_t> to4d(size_t n);
void init_matrix(int* ptr);
void assert_okay_init(Matrix const& matrix);
std::string get_time_fmt(const char* fmt);
const char* get_time_fmt_cstr(const char* fmt);
const char* get_time_default_fmt();
void omp_debug();
uint64 clock_diff(const struct timespec*, const struct timespec*);
uint64 clock_to_ns(struct timespec const&);
uint64 now_as_ns();
// Add the leading zeros to ns
std::string ns_with_leading_zeros(uint64 ns);

template<typename T, typename F>
std::optional<typename std::result_of<F(T const&)>::type> operator>>=(std::optional<T> const& lhs, F const& fn) {
    if (!lhs.has_value()) {
        return std::nullopt;
    } else {
        return std::make_optional(fn(*lhs));
    }
}

void assert_matrix_equals(Matrix const& lhs, Matrix const& rhs);

void init_start_matrix_once();
void init_from_start_matrix(Matrix&);

void init_expected_matrix_once();

#endif /* UTILS_H */
