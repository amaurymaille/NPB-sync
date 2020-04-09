#ifndef DEFINES_H
#define DEFINES_H

#include <cstdint>

#include <array>
#include <future>
#include <optional>
#include <utility>
#include <vector>

/*
 * Let there be a hypercube H of dimensions DIM_W * DIM_X * DIM_Y * DIM_Z.
 * 
 * We iterate over DIM_W, producing DIM_W cubes C1, C2, ... C(DIM_W), each of 
 * dimensions DIM_X * DIM_Y * DIM_Z.
 * 
 * Let vectors I, J and K identify the 3D grid created by one cube. Let I be oriented
 * from left to right, J be oriented from front to back, and K be oriented from
 * bottom to top.
 * 
 * Let the I axis identify the axis created by vector I for one cube, the J axis 
 * identify the axis created by vector J and the K axis identify the axis created 
 * by vector K.
 * 
 * Let "the I dimension" refer to the DIM_X of one cube, "the J dimension" refer 
 * to the DIM_Y of one cube and "the K dimension" refer to the DIM_Z of one cube.
 * 
 * OpenMP will segment the computation on the I axis. For one cube C, OpenMP will
 * create N sub-cubes SubC1, ..., SubCN. Cube M has dimensions OMP_DIMX(M) * DIM_Y *
 * DIM_Z, with OMP_DIMX(M) a function from [O..N[ to [0..N[, representing the segmentation of 
 * a static for schedule in OpenMP. If N = 8 and DIM_X = 8, OMP_DIMX(M) = 1 for every 
 * M. If N = 8 and DIM_X = 9, OMP_DIMX(0) = 2 and OMP_DIMX(M) = 1 for every other M.
 */

namespace Globals {
    static const size_t DIM_W = 8;
    static const size_t DIM_X = 25;
    static const size_t DIM_Y = 30;
    static const size_t DIM_Z = 27;
    static const size_t NB_ELEMENTS = DIM_W * DIM_X * DIM_Y * DIM_Z;

    static const size_t ZONE_X_SIZE = 32;
    static const size_t ZONE_Y_SIZE = 32;
    static const size_t ZONE_Z_SIZE = ::Globals::DIM_Z;

    static const size_t ITERATIONS = DIM_W;
    // How many points to SEND on the junction between two adjacent faces of two
    // separate sub-cubes. This properly ignores the value J = 0 as we don't
    // compute anything at this J value.
    static const size_t NB_POINTS_PER_ITERATION = (DIM_Y - 1) * DIM_Z;
    // How many points on one (JK) face of a sub-cube.
    static const size_t NB_VALUES_PER_BLOCK = DIM_Y * DIM_Z;

    // Base power for how many points to send when increasing the number of points
    // send with each iteration.
    static const size_t INCREASING_POINTS_BASE_POWER = 4;
    // After how many iteration we start sending full blocks and no longer sets of 
    // points.
    static const size_t INCREASING_POINTS_ITERATION_LIMIT = 4;

    // How many lines parallel to the J axis on the (JK) face of a sub-cube
    static const size_t NB_J_LINES_PER_ITERATION = DIM_Z;
    // How many lines parallel to the K axis on the (JK) face of a sub-cube
    static const size_t NB_K_LINES_PER_ITERATION = DIM_Y;

    static const size_t INCREASING_LINES_BASE_POWER = 4;
    static const size_t INCREASING_LINES_ITERATION_LIMIT = 4;
}

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
