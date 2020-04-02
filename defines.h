#ifndef DEFINES_H
#define DEFINES_H

#include <cstdint>

#include <array>
#include <future>
#include <optional>
#include <utility>
#include <vector>

namespace Globals {
    static const size_t DIM_W = 8;
    static const size_t DIM_X = 25;
    static const size_t DIM_Y = 20;
    static const size_t DIM_Z = 22;
    static const size_t NB_ELEMENTS = DIM_W * DIM_X * DIM_Y * DIM_Z;

    static const size_t ZONE_X_SIZE = 32;
    static const size_t ZONE_Y_SIZE = 32;
    static const size_t ZONE_Z_SIZE = ::Globals::DIM_Z;

    static const size_t ITERATIONS = DIM_W;
    static const size_t NB_LINES_PER_ITERATION = (DIM_Y - 1) * DIM_Z;
    static const size_t NB_VALUES_PER_BLOCK = DIM_Y * DIM_Z;

    static const size_t INCREASING_LINES_BASE_POWER = 4;
    static const size_t INCREASING_LINES_ITERATION_LIMIT = 4; 
}

enum Tenths {
    TEN         = 10,
    HUNDRED     = TEN       * TEN,
    THOUSAND    = HUNDRED   * 10,
    MILLION     = THOUSAND  * THOUSAND,
    BILLION     = MILLION   * THOUSAND,
};

enum SecondsTimes {
    MINUTES = 60,
    HOURS   = 60 * MINUTES,
    DAY     = 24 * HOURS,
};

enum ToSecondsTimes {
    MILLI   = THOUSAND,
    MICRO   = MILLION,
    NANO    = BILLION,
};

typedef uint64_t uint64;

namespace g = Globals;

typedef int MatrixValue;
typedef MatrixValue Matrix[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z];

template<typename T>
using OptionalReference = std::optional<std::reference_wrapper<T>>;

template<typename T>
using ThreadStore = std::vector<T>;

#ifdef ACTIVE_PROMISES
template<typename T>
class ActivePromise;

template<>
class ActivePromise<void>;

template<typename T>
using Promise = ActivePromise<T>;
#else
template<typename T>
using Promise = std::promise<T>;
#endif

typedef ThreadStore<std::array<Promise<void>, g::NB_LINES_PER_ITERATION>> LinePromiseContainer;
typedef OptionalReference<LinePromiseContainer> LinePromiseStore;

// typedef ThreadStore<Promise<std::vector<MatrixValue>*>> BlockPromiseContainer;
typedef ThreadStore<Promise<void>> BlockPromiseContainer;
typedef OptionalReference<BlockPromiseContainer> BlockPromiseStore;

typedef ThreadStore<std::vector<Promise<size_t>>> IncreasingLinePromiseContainer;
typedef OptionalReference<IncreasingLinePromiseContainer> IncreasingLinePromiseStore;

// The initial matrix
extern Matrix g_start_matrix;
// The expected final matrix 
extern Matrix g_expected_matrix;

#endif /* DEFINES_H */
