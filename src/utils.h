#ifndef UTILS_H
#define UTILS_H

#include <atomic>
#include <array>
#include <initializer_list>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <tuple>
#include <utility>

#include "defines.h"

#define NO_COPY_CTR(CLASS) CLASS(CLASS const&) = delete
#define NO_COPY_OP(CLASS) CLASS& operator=(CLASS const&) = delete
#define NO_COPY(CLASS) NO_COPY_CTR(CLASS); \
                       NO_COPY_OP(CLASS)

#define NO_COPY_CTR_T(CLASS, T) CLASS(CLASS<T> const&) = delete
#define NO_COPY_OP_T(CLASS, T) NO_COPY_OP(CLASS<T>)
#define NO_COPY_T(CLASS, T) NO_COPY_CTR_T(CLASS, T); NO_COPY_OP_T(CLASS, T)

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

namespace notstd {
    // Do nothing mutex
    // Inspired by ACE_Null_Mutex
    class null_mutex {
    public:
        null_mutex() { }
        null_mutex(null_mutex const&) = delete;

        null_mutex& operator=(null_mutex const&) = delete;

        inline void lock() { }
        inline void unlock() { }
    };
}

namespace Globals {
    extern RandomGenerator<unsigned int> sleep_generator;
    extern RandomGenerator<unsigned char> binary_generator;
}

template<typename T, typename R>
auto count_duration_cast(std::chrono::duration<R> const& tp) {
    return std::chrono::duration_cast<T>(tp).count();
}

std::string get_time_fmt(const char* fmt);
const char* get_time_fmt_cstr(const char* fmt);
const char* get_time_default_fmt();

void omp_debug();

uint64 clock_diff(const struct timespec*, const struct timespec*);
uint64 clock_to_ns(struct timespec const&);
uint64 now_as_ns();
// Add the leading zeros to ns
std::string ns_with_leading_zeros(uint64 ns);

void assert_matrix_equals(Matrix const& lhs, Matrix const& rhs);

// void init_matrix(double* ptr);
void init_reordered_matrix(Matrix& matrix);

void assert_okay_init(Matrix const& matrix);

void init_from(Matrix&, const Matrix&);

void init_start_matrix_once();
void init_start_matrix_from_file(const std::string& filename);

void init_from_start_matrix(Matrix&);

void init_expected_matrix_once();
void init_expected_matrix_once_from_file(const std::string& filename);

void init_matrix_from_file(Matrix::element* ptr, const std::string& filename);

unsigned int omp_nb_threads();

#include "utils.tpp"

#endif /* UTILS_H */
