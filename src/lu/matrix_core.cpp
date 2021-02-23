#include <cstring>

#include <algorithm>
#include <iterator>
#include <sstream>
#include <stdexcept>

#include "dynamic_config.h"
#include "lu/dynamic_defines.h"
#include "lu/lu.h"
#include "lu/matrix_core.h"
#include "utils.h"

// ----------------------------------------------------------------------------
// LUSolver

namespace lu = Globals::LU;

LUSolver::LUSolver() {
    _matrix.resize(boost::extents[lu::DIM][lu::DIM]); 
    _expected.resize(boost::extents[lu::DIM][lu::DIM]);
}

void LUSolver::init() {
    auto start_matrix_filename_opt = sDynamicConfigFiles.get_start_matrix_filename();
    if (start_matrix_filename_opt) {
        init_from_file(start_matrix_filename_opt.value());
    } else {
        _init();
    }
}

void LUSolver::assert_okay_init(const Matrix2D& other) const {
    assert_matrix_equals(_matrix, other);
}

void LUSolver::init_expected() {
    auto input_matrix_filename_opt = sDynamicConfigFiles.get_input_matrix_filename();
    if (input_matrix_filename_opt) {
        const std::string& filename = input_matrix_filename_opt.value();
        init_expected_from_file(filename);
    } else {
        compute_matrix(sLU.get_matrix(), _expected, lu::DIM);
    }
}

void LUSolver::assert_okay_expected(const Matrix2D& other) const {
    assert_matrix_equals(_expected, other);
}

void LUSolver::init_from(Matrix2D const& other) {
    init_matrix_from(_matrix, other);
}

void LUSolver::init_expected_from(Matrix2D const& other) {
    init_matrix_from(_expected, other);
}

void LUSolver::assert_equals(Matrix2D const& other) const {
    assert_matrix_equals(_matrix, other);
}

void LUSolver::assert_expected_equals(const Matrix2D& other) const {
    assert_matrix_equals(_expected, other);
}

void LUSolver::assert_matrix_equals(Matrix2D const& lhs, Matrix2D const& rhs) {
    if (!same_shape(lhs, rhs)) {
        throw std::runtime_error("Matrices not equal (different shapes)");
    }

    if (memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(MatrixTValue))) {
        throw std::runtime_error("Matrices not equal (different content)");
    }
}

void LUSolver::init_matrix_from(Matrix2D& dst, const Matrix2D& src) {
    if (!same_shape(dst, src)) {
        throw std::runtime_error("Matrices not equal (different shapes)");
    }

    memcpy(dst.data(), src.data(), src.size() * sizeof(MatrixTValue));
}

void LUSolver::init_matrix_from_file(Matrix2D& dst, const std::string& filename) {
    std::ifstream stream(filename);
    if (!stream.good()) {
        std::ostringstream err_stream;
        err_stream << "Error while opening file " << filename << std::endl;
        throw std::runtime_error(err_stream.str());
    }

    std::copy(std::istream_iterator<MatrixTValue>(stream), std::istream_iterator<MatrixTValue>(), dst.data());
}

void LUSolver::init_matrix(Matrix2D& matrix, uint64 nb_elements) {
    // Based on polybench code

    namespace g = Globals;

    int n = matrix.size();
    int i, j;

    for (i = 0; i < n; i++) {
        for (j = 0; j <= i; j++) {
	        matrix[i][j] = (-j % n) / n + 1;
        }

        for (j = i+1; j < n; j++) {
	        matrix[i][j] = 0;
        }
        matrix[i][i] = 1;
    }

    /* Make the matrix positive semi-definite. */
    /* not necessary for LU, but using same code as cholesky */
    int r, s, t;
    Matrix2D tmp(boost::extents[g::LU::DIM][g::LU::DIM]);
    for (r = 0; r < n; ++r) {
        for (s = 0; s < n; ++s) {
            tmp[r][s] = 0;
        }
    }

    for (t = 0; t < n; ++t) {
        for (r = 0; r < n; ++r) {
            for (s = 0; s < n; ++s) {
                tmp[r][s] += matrix[r][t] * matrix[s][t];
            }
        }
    }

    for (r = 0; r < n; ++r) {
        for (s = 0; s < n; ++s) {
            matrix[r][s] = tmp[r][s];
        }
    }
}

void LUSolver::compute_matrix(Matrix2D const& start, Matrix2D& res, size_t dim) {
    (void)dim; // ???
    kernel_lu(start, res);
}

void LUSolver::_init() {
    init_matrix(_matrix, lu::DIM * lu::DIM);
}

void LUSolver::_init_expected() {
    Matrix2D start(boost::extents[lu::DIM][lu::DIM]);
    init_matrix(start, lu::DIM * lu::DIM);
    compute_matrix(start, _expected, lu::DIM);
}

void LUSolver::init_from_file(std::string const& filename) {
    init_matrix_from_file(_matrix, filename);
}

void LUSolver::init_expected_from_file(std::string const& filename) {
    init_matrix_from_file(_expected, filename);
}

void LUSolver::generate_vectors(Matrix2D const& matrix, std::vector<Vector1D>& xs, std::vector<Vector1D>& bs) {
    for (int i = 0; i < xs.size(); ++i) {
        Vector1D& x = xs[i];
        Vector1D& b = bs[i];

        LUSolver::generate_vector(matrix, x, b);
    }
}

void LUSolver::generate_vector(Matrix2D const& matrix, Vector1D& x, Vector1D& b) {
    // Ax = b
    RandomGenerator<unsigned int> gen(1, 100);

    for (int i = 0; i < x.size(); ++i)
        x[i] = gen();

    b.resize(x.size());
    std::fill(b.begin(), b.end(), 0);

    for (int i = 0; i < matrix.size(); ++i) {
        for (int j = 0; j < matrix.size(); ++j) {
            b[i] += matrix[i][j] * x[j];
        }
    }
}
