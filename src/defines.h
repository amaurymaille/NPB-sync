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

enum class PromiseTypes : unsigned char {
    NATIVE,
    ACTIVE
};

std::string to_string(PromiseTypes v); 

#ifdef ACTIVE_PROMISES
template<typename T>
class ActivePromise;

template<>
class ActivePromise<void>;

template<typename T>
using Promise = ActivePromise<T>;

namespace Globals {
    static constexpr PromiseTypes PROMISE_TYPE = PromiseTypes::ACTIVE;
}

#else
template<typename T>
using Promise = std::promise<T>;

namespace Globals {
    static constexpr PromiseTypes PROMISE_TYPE = PromiseTypes::NATIVE;
}
#endif // ACTIVE_PROMISES

template<typename T>
class PromisePlus;

template<>
class PromisePlus<void>;

// Point synchronization
typedef ThreadStore<std::array<Promise<void>, g::NB_POINTS_PER_ITERATION>> PointPromiseContainer;
typedef OptionalReference<PointPromiseContainer> PointPromiseStore;

// Block synchronization
// typedef ThreadStore<Promise<std::vector<MatrixValue>*>> BlockPromiseContainer;
typedef ThreadStore<Promise<void>> BlockPromiseContainer;
typedef OptionalReference<BlockPromiseContainer> BlockPromiseStore;

// Increasing points synchronization
typedef ThreadStore<std::vector<Promise<size_t>>> IncreasingPointPromiseContainer;
typedef OptionalReference<IncreasingPointPromiseContainer> IncreasingPointPromiseStore;

// Line synchronization
typedef ThreadStore<std::array<Promise<void>, Globals::NB_J_LINES_PER_ITERATION>> JLinePromiseContainer;
typedef OptionalReference<JLinePromiseContainer> JLinePromiseStore;

typedef ThreadStore<std::array<Promise<void>, Globals::NB_K_LINES_PER_ITERATION>> KLinePromiseContainer;
typedef OptionalReference<KLinePromiseContainer> KLinePromiseStore;

// Increasing line synchronization
typedef ThreadStore<std::vector<Promise<size_t>>> IncreasingJLinePromiseContainer;
typedef OptionalReference<IncreasingJLinePromiseContainer> IncreasingJLinePromiseStore;

typedef IncreasingJLinePromiseContainer IncreasingKLinePromiseContainer;
typedef IncreasingJLinePromiseStore IncreasingKLinePromiseStore;

/*
// Block PromisePlus synchronization
typedef ThreadStore<PromisePlus<void>> BlockPromisePlusContainer;
typedef OptionalReference<BlockPromisePlusContainer> BlockPromisePlusStore;

// JLine PromisePlus synchronization
typedef ThreadStore<PromisePlus<void>> JLinePromisePlusContainer;
typedef OptionalReference<JLinePromisePlusContainer> JLinePromisePlusStore;

// Increasing JLine PromisePlus synchronization
typedef ThreadStore<PromisePlus<size_t>> IncreasingJLinePromisePlusContainer;
typedef OptionalReference<IncreasingJLinePromisePlusContainer> IncreasingJLinePromisePlusStore;

// KLine PromisePlus & Increasing KLine PromisePlus synchronizations
typedef JLinePromisePlusContainer KLinePromisePlusContainer;
typedef JLinePromisePlusStore KLinePromisePlusStore;

typedef IncreasingJLinePromisePlusContainer IncreasingKLinePromisePlusContainer;
typedef IncreasingJLinePromisePlusStore IncreasingKLinePromisePlusStore; */

typedef ThreadStore<PromisePlus<void>*> PromisePlusContainer;
typedef std::optional<PromisePlusContainer> PromisePlusStore;

template<typename T>
class NaivePromise;

template<>
class NaivePromise<void>;

// typedef ThreadStore<std::promise<void>*> ArrayOfPromisesContainer;
typedef ThreadStore<NaivePromise<void>*> ArrayOfPromisesContainer;
typedef std::optional<ArrayOfPromisesContainer> ArrayOfPromisesStore;

typedef ThreadStore<NaivePromise<void>*> PromiseOfArrayContainer;
typedef std::optional<PromiseOfArrayContainer> PromiseOfArrayStore;

// class MatrixReorderer;

// The initial matrix
extern Matrix g_start_matrix;
// The expected final matrix 
extern Matrix g_expected_matrix;

// The initial reordered matrix
// extern Matrix g_reordered_start_matrix;
// The expected final reordered matrix
// extern MatrixReorderer* g_expected_reordered_matrix;

#endif /* DEFINES_H */
