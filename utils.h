#ifndef UTILS_H
#define UTILS_H

#include <optional>
#include <random>
#include <string>
#include <tuple>

#include "defines.h"

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

namespace Globals {
    extern RandomGenerator<unsigned int> sleep_generator;
    extern RandomGenerator<unsigned char> binary_generator;
}

template<typename T, typename R>
auto count_duration_cast(std::chrono::duration<R> const& tp) {
    return std::chrono::duration_cast<T>(tp).count();
}

size_t to1d(size_t w, size_t x, size_t y, size_t z);
std::tuple<size_t, size_t, size_t, size_t> to4d(size_t n);
void init_matrix(int* ptr);
void assert_okay_init(Matrix matrix);
std::string get_time_fmt(const char* fmt);
const char* get_time_fmt_cstr(const char* fmt);
const char* get_time_default_fmt();
void omp_debug();

#endif /* UTILS_H */