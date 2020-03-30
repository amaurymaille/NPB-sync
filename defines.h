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
    static const size_t DIM_X = 20;
    static const size_t DIM_Y = 8;
    static const size_t DIM_Z = 10;
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

typedef ThreadStore<std::array<std::promise<MatrixValue>, g::NB_LINES_PER_ITERATION>> LinePromiseContainer;
typedef OptionalReference<LinePromiseContainer> LinePromiseStore;

typedef ThreadStore<std::promise<std::array<MatrixValue, g::NB_VALUES_PER_BLOCK>>> BlockPromiseContainer;
typedef OptionalReference<BlockPromiseContainer> BlockPromiseStore;

typedef ThreadStore<std::vector<std::promise<std::vector<MatrixValue>>>> IncreasingLinePromiseContainer;
typedef OptionalReference<IncreasingLinePromiseContainer> IncreasingLinePromiseStore;

#endif /* DEFINES_H */