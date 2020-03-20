#ifndef DEFINES_H
#define DEFINES_H

namespace Globals {
    static const size_t DIM_W = 8;
    static const size_t DIM_X = 10;
    static const size_t DIM_Y = 8;
    static const size_t DIM_Z = 10;
    static const size_t NB_ELEMENTS = DIM_W * DIM_X * DIM_Y * DIM_Z;

    static const size_t ZONE_X_SIZE = 32;
    static const size_t ZONE_Y_SIZE = 32;
    static const size_t ZONE_Z_SIZE = ::Globals::DIM_Z;

    static const size_t ITERATIONS = DIM_W;
    static const size_t NB_LINES_PER_ITERATION = DIM_Y * DIM_Z;
}

typedef int MatrixValue;
typedef MatrixValue Matrix[Globals::DIM_W][Globals::DIM_X][Globals::DIM_Y][Globals::DIM_Z];

#endif /* DEFINES_H */