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

#define NO_COPY_CTR_T2(CLASS, T1, T2) CLASS(CLASS<T1, T2> const&) = delete
#define NO_COPY_OP_T2(CLASS, T1, T2) CLASS<T1, T2>& operator=(CLASS<T1, T2> const&) = delete
#define NO_COPY_T2(CLASS, T1, T2) NO_COPY_CTR_T2(CLASS, T1, T2); NO_COPY_OP_T2(CLASS, T1, T2)

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

unsigned int omp_nb_threads();

template<typename Matrix>
bool same_shape(const Matrix& lhs, const Matrix& rhs) {
    size_t dims = Matrix::dimensionality;
    auto lhs_shape = lhs.shape();
    auto rhs_shape = rhs.shape();

    return ! memcmp(lhs_shape, rhs_shape, sizeof(typename Matrix::size_type) * dims);
}

template<typename T1, typename T2, typename Result>
struct decay_enable_if {
    typedef typename std::enable_if_t<std::is_same_v<std::decay_t<T1>, std::decay_t<T2>>, Result> type;
};

template<typename T1, typename T2, typename Result>
using decay_enable_if_t =  typename decay_enable_if<T1, T2, Result>::type;

std::ofstream open_out_file(const std::string& output_file);
std::ifstream open_in_file(const std::string& input_file);

void print_matrix(Matrix2D const& matrix);
void print_matrix(Matrix4D const& matrix);

std::ostream& serialize_float(std::ostream& out, float const& f);
std::istream& deserialize_float(std::istream& in, float& f);

#include "utils.tpp"

#endif /* UTILS_H */
