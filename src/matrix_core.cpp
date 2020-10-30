#include "matrix_core.h"

void init_matrix(Matrix& matrix, uint64 nb_elements) {
    namespace g = Globals;

    Matrix::element* ptr = matrix.data();
    for (size_t i = 0; i < nb_elements; ++i) {
        ptr[i] = double((i % 10) + 1);
    }
}

void compute_matrix(Matrix& matrix, int dimw, int dimx, int dimy, int dimz) {
    namespace g = Globals;

    for (int m = 1; m < dimw; ++m) {
        for (int i = 1; i < dimx; ++i) {
            for (int j = 1; j < dimy; ++j) {
                for (int k = 0; k < dimz; ++k) {
                    update_matrix(matrix, m, i, j, k);
                }
            }
        }
    }
}


