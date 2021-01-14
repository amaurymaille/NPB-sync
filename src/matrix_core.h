#ifndef MATRIX_CORE_H
#define MATRIX_CORE_H

#include <cmath>

#include "defines.h"

typedef uint64_t uint64;

class HeatCPUMatrix {
public:
    HeatCPUMatrix() { }
    void init();
    void assert_okay_init() const;

    void init_expected();
    void assert_okay_expected();

    void init_from(Matrix const& other);
    void init_expected_from(Matrix const& other);

    void assert_equals(Matrix const& other);
    void assert_expected_equals(Matrix const& other);

    Matrix& get_matrix() { return _matrix; }
    const Matrix& get_expected() const { return _expected; }

    static void assert_matrix_equals(Matrix const& lhs, Matrix const& rhs);
    static void init_matrix_from(Matrix& dst, const Matrix& src);
    static void init_matrix_from_file(Matrix& dst, const std::string& filename);

    static void init_matrix(Matrix& matrix, uint64 nb_elements);
    static void compute_matrix(Matrix& matrix, size_t dimw, size_t dimx, size_t dimy, size_t dimz);

    static inline void update_matrix(Matrix& matrix, size_t w, size_t x, size_t y, size_t z) {
        int lx = matrix[w][x - 1][y][z];
        int ly = matrix[w][x][y - 1][z];
        int lw = matrix[w - 1][x][y][z];

        for (int i = 0; i < 15; ++i)
            matrix[w][x][y][z] += std::sqrt(lw) + std::log(lx) + std::sin(ly);
    }

private:
    Matrix _matrix;
    Matrix _expected;

    void _init();
    void _init_expected();

    void init_from_file(std::string const& filename);
    void init_expected_from_file(std::string const& filename);
};


#endif /* MATRIX_CORE_H */
