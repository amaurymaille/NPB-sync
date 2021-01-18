#ifndef LU_MATRIX_CORE_H
#define LU_MATRIX_CORE_H

#include <matrix_core.h>
#include "lu/defines.h"

class LUMatrix final : public IReferenceMatrix<Matrix2D> {
public:
    static LUMatrix& instance() {
        static LUMatrix instance;
        return instance;
    }

    void init();
    void assert_okay_init(const Matrix2D& other) const;

    void init_expected();
    void assert_okay_expected(const Matrix2D& other) const;

    void init_from(Matrix2D const& other);
    void init_expected_from(Matrix2D const& other);

    void assert_equals(Matrix2D const& other) const;
    void assert_expected_equals(Matrix2D const& other) const;

    static void assert_matrix_equals(Matrix2D const& lhs, Matrix2D const& rhs);
    static void init_matrix_from(Matrix2D& dst, const Matrix2D& src);
    static void init_matrix_from_file(Matrix2D& dst, const std::string& filename);

    static void init_matrix(Matrix2D& matrix, uint64 nb_elements);
    static void compute_matrix(Matrix2D& matrix, size_t dimx, size_t dimy);

private:
    LUMatrix();

    void _init();
    void _init_expected();

    void init_from_file(std::string const& filename);
    void init_expected_from_file(std::string const& filename);
};

#define sLU LUMatrix::instance()

#endif // LU_MATRIX_CORE_H
