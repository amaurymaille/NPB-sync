#ifndef MATRIX_CORE_H
#define MATRIX_CORE_H

#include <cmath>

#include "defines.h"

typedef uint64_t uint64;

void init_matrix(Matrix& matrix, uint64 nb_elements);
void compute_matrix(Matrix& matrix, int dimw, int dimx, int dimy, int dimz);

inline void update_matrix(Matrix& matrix, size_t w, size_t x, size_t y, size_t z) {
    int lx = matrix[w][x - 1][y][z];
    int ly = matrix[w][x][y - 1][z];
    int lw = matrix[w - 1][x][y][z];

    for (int i = 0; i < 15; ++i)
        matrix[w][x][y][z] += std::sqrt(lw) + std::log(lx) + std::sin(ly);

}

#endif /* MATRIX_CORE_H */
