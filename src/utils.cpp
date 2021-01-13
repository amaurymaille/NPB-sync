#include <cassert>
#include <cstring>
#include <ctime>

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <tuple>

#include <sys/time.h>

#include <omp.h>

#include "dynamic_config.h"
#include "functions.h"
#include "matrix_core.h"
#include "utils.h"

namespace Globals {
    RandomGenerator<unsigned int> sleep_generator(0, 100);
    RandomGenerator<unsigned char> binary_generator(0, 1);
}

/* void init_matrix(double* ptr) {
    namespace g = Globals;
    for (size_t i = 0; i < g::HeatCPU::NB_ELEMENTS; ++i) {
        ptr[i] = double(i % 10);
    }
} */

void init_reordered_matrix(Matrix& matrix) {
    namespace g = Globals;

    size_t value = 0;
    for (int i = 0; i < g::HeatCPU::DIM_W; ++i) {
        for (int j = 0; j < g::HeatCPU::DIM_X; ++j) {
            for (int k = 0; k < g::HeatCPU::DIM_Y; ++k) {
                for (int l = 0; l < g::HeatCPU::DIM_Z; ++l) {
                    matrix[i][l][k][j] = value % 10;
                    value++;
                }
            }
        }
    }
}

void assert_okay_init(Matrix const& matrix) {
    assert_matrix_equals(matrix, g_start_matrix);
}

/* void assert_okay_reordered_init(Matrix const& matrix) {
    assert_matrix_equals(matrix, g_reordered_start_matrix);
} */

void assert_okay_reordered_compute() {
    for (int i = 0; i < g::HeatCPU::DIM_W; ++i) {
        for (int j = 0; j < g::HeatCPU::DIM_X; ++j) {
            for (int k = 0; k < g::HeatCPU::DIM_Y; ++k) {
                for (int l = 0; l < g::HeatCPU::DIM_Z; ++l) {
                    assert((*g_expected_matrix)(i, j, k, l) == (*g_expected_reordered_matrix)(i, j, k, l));
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
    if (memcmp(lhs.data(), rhs.data(), Globals::HeatCPU::NB_ELEMENTS * sizeof(MatrixValue)) != 0) {
        throw std::runtime_error("Matrix not equals");
    }
}

void init_matrix_from(Matrix& matrix, Matrix const& src) {
    memcpy(matrix.data(), src.data(), Globals::HeatCPU::NB_ELEMENTS * sizeof(MatrixValue));
}

void init_start_matrix_once() {
    auto start_matrix_filename_opt = sDynamicConfigFiles.get_start_matrix_filename();
    if (start_matrix_filename_opt) {
        init_start_matrix_from_file(start_matrix_filename_opt.value());
    } else {
        init_matrix(g_start_matrix, Globals::HeatCPU::NB_ELEMENTS);
    }
}

void init_start_matrix_from_file(const std::string& filename) {
    init_matrix_from_file(g_start_matrix.data(), filename);
}

/* void init_reordered_start_matrix_once() {
    init_reordered_matrix(g_reordered_start_matrix);
} */

void init_from_start_matrix(Matrix& matrix) {
    init_matrix_from(matrix, g_start_matrix);
}

/* void init_from_reordered_start_matrix(Matrix& matrix) {
    init_matrix_from(matrix, g_reordered_start_matrix);
} */

void init_expected_matrix_once() {
    namespace g = Globals;

    auto input_matrix_filename_opt = sDynamicConfigFiles.get_input_matrix_filename();
    if (input_matrix_filename_opt) {
        const std::string& filename = input_matrix_filename_opt.value();
        init_expected_matrix_once_from_file(filename);
    } else {
        compute_matrix(g_expected_matrix, g::HeatCPU::DIM_W, g::HeatCPU::DIM_X, g::HeatCPU::DIM_Y, g::HeatCPU::DIM_Z);
        /* for (int i = 1; i < Globals::HeatCPU::ITERATIONS; ++i) {
            heat_cpu_naive(g_expected_matrix, i);
        } */
    }
}

void init_expected_matrix_once_from_file(const std::string& filename) {
    init_matrix_from_file(g_expected_matrix.data(), filename);
}

void init_matrix_from_file(Matrix::element* ptr, const std::string& filename) {
    std::ifstream stream(filename);
    if (!stream.good()) {
        std::ostringstream err;
        err << "Unable to open input matrix file " << filename << std::endl;
        throw std::runtime_error(err.str());
    }

    std::copy(std::istream_iterator<Matrix::element>(stream), std::istream_iterator<Matrix::element>(), ptr);
}

/*void init_expected_reordered_matrix_once() {
    for (int i = 1; i < Globals::HeatCPU::ITERATIONS; ++i) {
        heat_cpu_naive(*g_expected_reordered_matrix, i);
    }
} */

uint64 clock_to_ns(const struct timespec& clk) {
    return clk.tv_sec * BILLION + clk.tv_nsec;
}

uint64 now_as_ns() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    return clock_to_ns(now);
}

unsigned int omp_nb_threads() {
    unsigned int nb_threads = -1;
    #pragma omp parallel 
    {
        #pragma omp master
        {
            nb_threads = omp_get_num_threads();
        }
    }

    return nb_threads;
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
