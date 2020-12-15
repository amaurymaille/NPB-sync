#ifndef DEFINES_H
#define DEFINES_H

#include <cstdint>

#include <array>
#include <future>
#include <optional>
#include <utility>
#include <vector>

#include <boost/multi_array.hpp>

#include "dynamic_defines.h"

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

namespace g = Globals;

typedef boost::multi_array<int, 4> Matrix;
typedef Matrix::element MatrixValue;
// typedef MatrixValue Matrix[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z];

typedef boost::multi_array<float, 2> Matrix2D;
typedef Matrix2D::element Matrix2DValue;

typedef std::vector<float> Vector1D;
typedef Vector1D::value_type Vector1DValue;

template<typename T>
using OptionalReference = std::optional<std::reference_wrapper<T>>;

/// A vector of omp_get_num_threads() T
template<typename T>
using ThreadStore = std::vector<T>;

template<typename T>
class PromisePlus;

template<>
class PromisePlus<void>;

typedef ThreadStore<PromisePlus<void>*> PromisePlusContainer;
typedef std::optional<PromisePlusContainer> PromisePlusStore;

template<typename T>
class NaivePromise;

template<>
class NaivePromise<void>;

typedef ThreadStore<NaivePromise<void>*> ArrayOfPromisesContainer;
typedef std::optional<ArrayOfPromisesContainer> ArrayOfPromisesStore;

typedef ThreadStore<NaivePromise<void>*> PromiseOfArrayContainer;
typedef std::optional<PromiseOfArrayContainer> PromiseOfArrayStore;

// The initial matrix
extern Matrix g_start_matrix;
// The expected final matrix 
extern Matrix g_expected_matrix;

#endif /* DEFINES_H */
