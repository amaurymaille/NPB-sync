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

template<size_t N>
class DimensionConverter {
public:
    DimensionConverter(std::initializer_list<size_t> const& dimensions);
    size_t to_1d(std::initializer_list<size_t> const& values);
    std::array<size_t, N> from_1d(size_t pos);

private:
    size_t _dimw, _dimx, _dimy, _dimz;
};

template<>
class DimensionConverter<4> {
public:
    DimensionConverter(std::initializer_list<size_t> const& dimensions);
    size_t to_1d(std::initializer_list<size_t> const& values);
    std::array<size_t, 4> from_1d(size_t pos);

private:
    std::array<size_t, 4> _dimensions_sizes;
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
void init_reordered_matrix(Matrix& matrix);

void assert_okay_init(Matrix const& matrix);
void assert_okay_reordered_init(Matrix const& matrix);

std::string get_time_fmt(const char* fmt);
const char* get_time_fmt_cstr(const char* fmt);
const char* get_time_default_fmt();

void omp_debug();

uint64 clock_diff(const struct timespec*, const struct timespec*);
uint64 clock_to_ns(struct timespec const&);
uint64 now_as_ns();
// Add the leading zeros to ns
std::string ns_with_leading_zeros(uint64 ns);

// Monad like operator >>= for std::optional
template<typename T, typename F>
std::optional<typename std::result_of<F(T const&)>::type> operator>>=(std::optional<T> const& lhs, F const& fn) {
    if (!lhs.has_value()) {
        return std::nullopt;
    } else {
        return std::make_optional(fn(*lhs));
    }
}

void assert_matrix_equals(Matrix const& lhs, Matrix const& rhs);

void init_from(Matrix&, const Matrix&);

void init_start_matrix_once();
void init_reordered_start_matrix_once();

void init_from_start_matrix(Matrix&);
void init_from_reordered_start_matrix(Matrix&);

void init_expected_matrix_once();
void init_expected_reordered_matrix_once();

class MatrixReorderer {
public:
    virtual void init() = 0;
    virtual MatrixValue operator()(size_t, size_t, size_t, size_t) = 0;
    
protected:
    Matrix _matrix;
};

class StandardMatrixReorderer : public MatrixReorderer {
public:
    StandardMatrixReorderer() : _matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z])) {

    }

    void init() {
        init_from_start_matrix(_matrix);
    }

    MatrixValue operator()(size_t i, size_t j, size_t k, size_t l) {
        return _matrix[i][l][k][j];
    }
};

class JLinePromiseMatrixReorderer : public MatrixReorderer {
public:
    JLinePromiseMatrixReorderer() : _matrix(boost::extents[g::DIM_W][g::DIM_Z][g::DIM_Y][g::DIM_X]) {

    }

    void init() {
        init_from_reordered_start_matrix(_matrix);
    }
};

#include "utils.tpp"

#endif /* UTILS_H */
