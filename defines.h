#ifndef DEFINES_H
#define DEFINES_H

#include <cstdint>

#include <array>
#include <future>
#include <optional>
#include <utility>
#include <vector>

#include "dynamic_defines.h"

#define assertM(C, M, ...) do { if (!C) { fprintf(stderr, M, __VA_ARGS__); assert(false); }} while (0);

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
#endif

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

// The initial matrix
extern Matrix g_start_matrix;
// The expected final matrix 
extern Matrix g_expected_matrix;

#endif /* DEFINES_H */
