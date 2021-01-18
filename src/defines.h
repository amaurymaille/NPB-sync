#ifndef DEFINES_H
#define DEFINES_H

#include <cstdint>

#include <array>
#include <future>
#include <optional>
#include <utility>
#include <vector>

#include <boost/multi_array.hpp>

#define assertM(C, M, ...) do { if (!C) { fprintf(stderr, M, __VA_ARGS__); assert(false); }} while (0);

enum Tenths {
    TEN         = 10,
    HUNDRED     = TEN       * TEN,
    THOUSAND    = HUNDRED   * TEN,
    MILLION     = THOUSAND  * THOUSAND,
    BILLION     = MILLION   * THOUSAND,
};

enum SecondsTimes {
    MINUTES = 60,
    HOURS   = 60 * MINUTES,
    DAY     = 24 * HOURS,
};

enum ToSecondsTimes {
    TO_MILLI   = THOUSAND,
    TO_MICRO   = MILLION,
    TO_NANO    = BILLION,
};

typedef uint64_t uint64;

template<typename T>
using OptionalReference = std::optional<std::reference_wrapper<T>>;

/// A vector of omp_get_num_threads() T
template<typename T>
using ThreadStore = std::vector<T>;

template<typename T>
class PromisePlus;

template<>
class PromisePlus<void>;

#endif /* DEFINES_H */
