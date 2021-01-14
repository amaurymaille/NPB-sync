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
#include "functions/heat_cpu.h"
#include "matrix_core.h"
#include "utils.h"

namespace Globals {
    RandomGenerator<unsigned int> sleep_generator(0, 100);
    RandomGenerator<unsigned char> binary_generator(0, 1);
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

void init_matrix_from_file(Matrix::element* ptr, const std::string& filename) {
    std::ifstream stream(filename);
    if (!stream.good()) {
        std::ostringstream err;
        err << "Unable to open input matrix file " << filename << std::endl;
        throw std::runtime_error(err.str());
    }

    std::copy(std::istream_iterator<Matrix::element>(stream), std::istream_iterator<Matrix::element>(), ptr);
}

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
