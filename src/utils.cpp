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

#ifdef NDEBUG
    (void)size;
#endif

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

template<typename Stream>
Stream open_file(const std::string& filename) {
    Stream str(filename);
    if (!str) {
        std::ostringstream err;
        err << "Error while opening " << filename << std::endl;
        throw std::runtime_error(err.str());
    }

    return str;
}

std::ofstream open_out_file(const std::string& output_file) {
    return open_file<std::ofstream>(output_file);
}

std::ifstream open_in_file(const std::string& input_file) {
    return open_file<std::ifstream>(input_file);
}

void print_matrix(Matrix2D const& matrix) {
    for (int i = 0; i < matrix.size(); ++i) {
        for (int j = 0; j < matrix[i].size(); ++j) {
            std::cout << matrix[i][j] << " ";
        }
        std::cout << std::endl;
    }
}

void print_matrix(Matrix4D const& matrix) {
    for (int i = 0; i < matrix.size(); ++i) {
        for (int j = 0; j < matrix[i].size(); ++j) {
            for (int k = 0; k < matrix[i][j].size(); ++k) {
                for (int l = 0; l < matrix[i][j][k].size(); ++l) {
                    std::cout << matrix[i][j][k][l] << " ";
                }
                std::cout << std::endl;
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }
}

std::ostream& serialize_float(std::ostream& out, float const& f) {
    const uint32_t* ptr = reinterpret_cast<const uint32_t*>(&f);
    out << *ptr;
    return out;
}

std::istream& deserialize_float(std::istream& in, float& f) {
    uint32_t* ptr = reinterpret_cast<uint32_t*>(&f);
    in >> *ptr;
    return in;
}
