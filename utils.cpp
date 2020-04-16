#include <cassert>
#include <cstring>
#include <ctime>

#include <chrono>
#include <iostream>
#include <memory>
#include <random>
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

void assert_okay_init(Matrix matrix) {
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
    return (a->tv_sec - b->tv_sec) * NANO + a->tv_nsec - b->tv_nsec;
}

void assert_matrix_equals(Matrix lhs, Matrix rhs) {
    assert(memcmp(lhs, rhs, Globals::NB_ELEMENTS) == 0);
}

void init_start_matrix_once() {
    init_matrix(reinterpret_cast<int*>(g_start_matrix));
}

void init_from_start_matrix(Matrix matrix) {
    memcpy(matrix, g_start_matrix, Globals::NB_ELEMENTS * sizeof(MatrixValue));
}

void init_expected_matrix_once() {
    for (int i = 1; i < Globals::ITERATIONS; ++i) {
        heat_cpu(g_expected_matrix, i);
    }
}
