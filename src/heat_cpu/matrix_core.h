#ifndef HEAT_CPU_MATRIX_CORE_H
#define HEAT_CPU_MATRIX_CORE_H

#include <cmath>

#include <matrix_core.h>
#include "heat_cpu/defines.h"

class HeatCPUMatrix final : public IReferenceMatrix<Matrix4D> {
public:
    static HeatCPUMatrix& instance() {
        static HeatCPUMatrix instance;
        return instance;
    }

    void init();
    void assert_okay_init(const Matrix4D& other) const;

    void init_expected();
    void assert_okay_expected(const Matrix4D& other) const;

    void init_from(Matrix4D const& other);
    void init_expected_from(Matrix4D const& other);

    void assert_equals(Matrix4D const& other) const;
    void assert_expected_equals(Matrix4D const& other) const;

    static void assert_matrix_equals(Matrix4D const& lhs, Matrix4D const& rhs);
    static void init_matrix_from(Matrix4D& dst, const Matrix4D& src);
    static void init_matrix_from_file(Matrix4D& dst, const std::string& filename);

    static void init_matrix(Matrix4D& matrix, uint64 nb_elements);
    static void compute_matrix(Matrix4D& matrix, size_t dimw, size_t dimx, size_t dimy, size_t dimz);

    static inline void update_matrix(Matrix4D& matrix, size_t w, size_t x, size_t y, size_t z) {
        int lx = matrix[w][x - 1][y][z];
        int ly = matrix[w][x][y - 1][z];
        int lw = matrix[w - 1][x][y][z];

        for (int i = 0; i < 15; ++i)
            matrix[w][x][y][z] += std::sqrt(lw) + std::log(lx) + std::sin(ly);
    }

private:
    HeatCPUMatrix();

    void _init();
    void _init_expected();

    void init_from_file(std::string const& filename);
    void init_expected_from_file(std::string const& filename);
};

#define sHeatCPU HeatCPUMatrix::instance()

#endif // HEAT_CPU_MATRIX_CORE_H
