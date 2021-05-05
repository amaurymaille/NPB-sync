#ifndef LU_MATRIX_CORE_H
#define LU_MATRIX_CORE_H

#include <defines.h>
#include <matrix_core.h>
#include "lu/defines.h"

class LUSolver final : public IReferenceMatrix<Matrix2D> {
public:
    static LUSolver& instance() {
        static LUSolver instance;
        return instance;
    }

    void init();
    void init_expected();

    void init_from(Matrix2D const& other, const std::vector<Vector1D>& bs);
    void init_expected_from(Matrix2D const& other, const std::vector<Vector1D>& xs);

    void assert_equals(Matrix2D const& other, const std::vector<Vector1D>& bs) const;
    void assert_expected_equals(Matrix2D const& other, const std::vector<Vector1D>& xs) const;

    static void init_matrix(Matrix2D& matrix);
    static void compute_matrix(Matrix2D const& start, Matrix2D& res);

    static void assert_matrix_equals(Matrix2D const& lhs, Matrix2D const& rhs);
    static void assert_xs_equals(const std::vector<Vector1D>& lhs, const std::vector<Vector1D>& rhs);
    static void assert_bs_equals(const std::vector<Vector1D>& lhs, const std::vector<Vector1D>& rhs);

    static void init_matrix_from(Matrix2D& dst, const Matrix2D& src);
    static void init_xs_from(std::vector<Vector1D>& dst, const std::vector<Vector1D>& src);
    static void init_bs_from(std::vector<Vector1D>& dst, const std::vector<Vector1D>& src);
    static void init_all_from_file(Matrix2D& matrix, std::vector<Vector1D>& xs, 
                                   const std::string& filename);

    static void generate_vectors(Matrix2D const& matrix, std::vector<Vector1D>& xs, std::vector<Vector1D>& bs);
    static void generate_vector(Matrix2D const& matrix, Vector1D& x, Vector1D& b);

private:
    LUSolver();

    void init_from_file(std::string const& filename);
    void init_expected_from_file(std::string const& filename);

    void _init(); 
    void _init_expected();

    std::vector<Vector1D> _xs;
    std::vector<Vector1D> _bs;
};

#define sLU LUSolver::instance()

#endif // LU_MATRIX_CORE_H
