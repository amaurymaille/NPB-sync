#include <cassert>
#include <ctime>

#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <tuple>

#include <omp.h>

#include "utils.h"

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

    for (int i = 0; i < g::DIM_W; ++i) {
        for (int j = 0; j < g::DIM_X; ++j) {
            for (int k = 0; k < g::DIM_Y; k++) {
                for (int l = 0; l < g::DIM_Z; l++) {
                    size_t as1d = to1d(i, j, k, l);
                    auto [ci, cj, ck, cl] = to4d(as1d);

                    assert(matrix[i][j][k][l] == to1d(i, j, k, l) % 10);
                    assert(ci == i && cj == j && ck == k && cl == l);
                }
            }
        }
    }
}

std::string get_time_fmt(const char* fmt) {
    auto now = std::chrono::system_clock::now();
    std::time_t as_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* now_as_tm = std::gmtime(&as_time_t);

    std::unique_ptr<char[]> res(new char[100]);
    std::size_t size = std::strftime(res.get(), 100, fmt, now_as_tm);

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

void heat_cpu(Matrix, size_t);
void heat_cpu_switch_loops(Matrix, size_t);

void assert_switch_and_no_switch_are_identical() {
    namespace g = Globals;

    Matrix no_switch;
    Matrix with_switch;

    int* no_switch_ptr = reinterpret_cast<int*>(no_switch);
    int* with_switch_ptr = reinterpret_cast<int*>(with_switch);

    init_matrix(no_switch_ptr);
    init_matrix(with_switch_ptr);

    for (int i = 0; i < g::DIM_W; ++i) {
        heat_cpu(no_switch, i);
        heat_cpu_switch_loops(with_switch, i);
    }

    for (int i = 0; i < g::NB_ELEMENTS; ++i) {
        assert(no_switch_ptr[i] == with_switch_ptr[i]);
    }
}