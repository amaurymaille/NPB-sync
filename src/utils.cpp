#include <cassert>
#include <cstring>
#include <ctime>

#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <tuple>

#include <sys/time.h>

#include <omp.h>

#include "functions.h"
#include "utils.h"

namespace Globals {
    RandomGenerator<unsigned int> sleep_generator(0, 100);
    RandomGenerator<unsigned char> binary_generator(0, 1);
}

DimensionConverter<4>::DimensionConverter(std::initializer_list<size_t> const& init) {
    assert(init.size() == 4);

    std::copy(init.begin(), init.end(), _dimensions_sizes.begin());
}

size_t DimensionConverter<4>::to_1d(std::initializer_list<size_t> const& values) {
    assert (values.size() == 4);

    auto iter = values.begin();
    size_t w = *iter, x = *(iter + 1), y = *(iter + 2), z = *(iter + 3);
    auto [_, dimx, dimy, dimz] = _dimensions_sizes;

    return w * dimx * dimy  * dimz +
           x        * dimy  * dimz + 
           y                * dimz +
           z;
}

std::array<size_t, 4> DimensionConverter<4>::from_1d(size_t pos) {
    auto [_, dimx, dimy, dimz] = _dimensions_sizes;

    size_t z = pos % dimz;
    size_t y = ((pos - z) / dimz) % dimy;
    size_t x = ((pos - z - y * dimz) / (dimy * dimz)) % dimx;
    size_t w = (pos - z - y * dimz - x * dimy * dimz) / (dimx * dimy * dimz);

    std::array<size_t, 4> result = {w, x, y ,z};
    return result;
}

size_t to1d(size_t w, size_t x, size_t y, size_t z) {
    namespace g = Globals;
    return w * g::DIM_X * g::DIM_Y  * g::DIM_Z +
           x            * g::DIM_Y  * g::DIM_Z + 
           y                        * g::DIM_Z +
           z;
}

std::tuple<size_t, size_t, size_t, size_t> to4d(size_t n) {
    namespace g = Globals;
    size_t z = n % g::DIM_Z;
    size_t y = ((n - z) / g::DIM_Z) % g::DIM_Y;
    size_t x = ((n - z - y * g::DIM_Z) / (g::DIM_Y * g::DIM_Z)) % g::DIM_X;
    size_t w = (n - z - y * g::DIM_Z - x * g::DIM_Y * g::DIM_Z) / (g::DIM_X * g::DIM_Y * g::DIM_Z);

    return std::make_tuple(w, x, y, z);
}

void init_matrix(int* ptr) {
    namespace g = Globals;
    for (int i = 0; i < g::NB_ELEMENTS; ++i) {
        ptr[i] = i % 10;
    }
}

void assert_okay_init(Matrix const& matrix) {
    namespace g = Globals;
    assert_matrix_equals(matrix, g_start_matrix);
}

std::string get_time_fmt(const char* fmt) {
    auto now = std::chrono::system_clock::now();
    std::time_t as_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* now_as_tm = std::gmtime(&as_time_t);

    std::unique_ptr<char[]> res(new char[100]);
    std::size_t size = std::strftime(res.get(), 100, fmt, now_as_tm);
    assert(size != 0);

    std::string result(res.get());
    return result;
}

const char* get_time_fmt_cstr(const char* fmt) {
    return get_time_fmt(fmt).c_str();
}

const char* get_time_default_fmt() {
    return get_time_fmt_cstr("%H:%M:%S");
}

void omp_debug() {
    std::cout << "Number of threads (out of parallel): " << omp_get_num_threads() << std::endl;

#pragma omp parallel
{
    #pragma omp master
    {
        std::cout << "Number of threads (in parallel) : " << omp_get_num_threads() << std::endl;
    }
}

    std::cout << "Number of threads (out of parallel again) : " << omp_get_num_threads() << std::endl;
}

// Computes a - b
uint64 clock_diff(const struct timespec* a, const struct timespec* b) {
    return (a->tv_sec - b->tv_sec) * TO_NANO + a->tv_nsec - b->tv_nsec;
}

void assert_matrix_equals(Matrix const& lhs, Matrix const& rhs) {
    assert(memcmp(lhs.data(), rhs.data(), Globals::NB_ELEMENTS * sizeof(MatrixValue)) == 0);
}

void init_start_matrix_once() {
    init_matrix(g_start_matrix.data());
}

void init_from_start_matrix(Matrix& matrix) {
    memcpy(matrix.data(), g_start_matrix.data(), Globals::NB_ELEMENTS * sizeof(MatrixValue));
}

void init_expected_matrix_once() {
    for (int i = 1; i < Globals::ITERATIONS; ++i) {
        heat_cpu(g_expected_matrix, i);
    }
}

uint64 clock_to_ns(const struct timespec& clk) {
    return clk.tv_sec * BILLION + clk.tv_nsec;
}

uint64 now_as_ns() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    return clock_to_ns(now);
}

DeadlockDetector::DeadlockDetector(uint64 limit) : _limit(limit) {
    _running.store(false, std::memory_order_relaxed);
    _reset_count.store(0, std::memory_order_relaxed);
}

void DeadlockDetector::reset() {
    _reset_count++;
}

void DeadlockDetector::stop() {
    _running.store(false, std::memory_order_release);
}

void DeadlockDetector::run() {
    if (_running.load(std::memory_order_relaxed)) {
        throw std::runtime_error("Called run() on already running DeadlockDetector");
    }

    _running.store(true, std::memory_order_relaxed);

    uint64 time_since_reset = 0;
    while (_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        if (_reset_count.load(std::memory_order_acquire) != 0) {
            _reset_count.store(0, std::memory_order_release);
            time_since_reset = 0;
        } else {
            time_since_reset += 5LL * TO_NANO;

            if (time_since_reset > _limit) {
                std::cerr << "Deadlock detected, aborting (time since last reset: " << time_since_reset << ", limit: " << _limit << ")"  << std::endl;
                std::terminate();
            }
        }
    }
}

std::string ns_with_leading_zeros(uint64 ns) {
    std::ostringstream str;
    str << ns;

    if (str.str().size() < 9) {
        std::ostringstream prepend;
        for (int i = 0; i < 9 - str.str().size(); ++i)
            prepend << "0";
        
        prepend << str.str();

        return prepend.str();
    } else {
        return str.str();
    }
}