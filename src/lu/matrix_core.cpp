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
// Utilities

static void assert_vector_equals(const std::vector<Vector1D>& lhs, const std::vector<Vector1D>& rhs) {
    if (!(lhs == rhs)) {
        std::ostringstream err;
        err << "[assert_vector_equals] Vectors are not equal !" << std::endl;
        throw std::runtime_error(err.str());
    }
}

static void init_vector_from(std::vector<Vector1D>& dst, const std::vector<Vector1D>& src) {
    dst.resize(src.size());
    dst = src;
}

// ----------------------------------------------------------------------------
// LUSolver

namespace lu = Globals::LU;

void LUSolver::init_matrix(Matrix2D& matrix) {
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

void LUSolver::compute_matrix(Matrix2D const& start, Matrix2D& res) {
    kernel_lu(start, res);
}

LUSolver::LUSolver() {
    _matrix.resize(boost::extents[lu::DIM][lu::DIM]); 
    _expected.resize(boost::extents[lu::DIM][lu::DIM]);

}

void LUSolver::init() {
    auto start_matrix_filename_opt = sDynamicConfigFiles.get_start_matrix_filename();
    if (start_matrix_filename_opt) {
        init_from_file(start_matrix_filename_opt.value());
    } else {
        throw std::runtime_error("LUSolver requires a start matrix file");
    } 
}

void LUSolver::init_expected() {
    auto input_matrix_filename_opt = sDynamicConfigFiles.get_input_matrix_filename();
    if (input_matrix_filename_opt) {
        const std::string& filename = input_matrix_filename_opt.value();
        init_expected_from_file(filename);
    } else {
        throw std::runtime_error("LUSolver requires a compute matrix file");
    }
}

void LUSolver::init_from(Matrix2D const& other, const std::vector<Vector1D>& bs) {
    init_matrix_from(_matrix, other);
    init_bs_from(_bs, bs);
}

void LUSolver::init_expected_from(Matrix2D const& other, const std::vector<Vector1D>& xs) {
    init_matrix_from(_expected, other);
    init_xs_from(_xs, xs);
}

void LUSolver::assert_equals(Matrix2D const& other, const std::vector<Vector1D>& bs) const {
    assert_matrix_equals(_matrix, other);
    assert_bs_equals(_bs, bs);
}

void LUSolver::assert_expected_equals(Matrix2D const& other, const std::vector<Vector1D>& xs) const {
    assert_matrix_equals(_expected, other);
    assert_xs_equals(_xs, xs);
}

void LUSolver::assert_matrix_equals(Matrix2D const& lhs, Matrix2D const& rhs) {
    if (!same_shape(lhs, rhs)) {
        throw std::runtime_error("Matrices not equal (different shapes)");
    }

    if (memcmp(lhs.data(), rhs.data(), lhs.num_elements() * sizeof(MatrixTValue))) {
        // const unsigned char* lhs_begin = reinterpret_cast<const unsigned char*>(lhs.data());
        // const unsigned char* rhs_begin = reinterpret_cast<const unsigned char*>(rhs.data());

        /* std::ostream_iterator<unsigned char> iter(std::cout, "");
        std::cout << "=== LHS ===" << std::endl;
        // std::copy(lhs_begin, lhs_begin + lhs.num_elements() * sizeof(MatrixTValue), iter);
        for (int i = 0; i < lhs.num_elements(); ++i)
            serialize_float(std::cout, *(lhs.data() + i)) << " ";

        std::cout << std::endl << "=== RHS ===" << std::endl;
        // std::copy(rhs_begin, rhs_begin + rhs.num_elements() * sizeof(MatrixTValue), iter);
        for (int i = 0; i < rhs.num_elements(); ++i)
            serialize_float(std::cout, *(lhs.data() + i)) << " ";
        std::cout << std::endl << "=== DONE ===" << std::endl; */
        for (int i = 0; i < lhs.num_elements(); ++i) {
            std::cout << "Left: " << *(lhs.data() + i) << ", Right: " << *(rhs.data() + i) << ", Diff = " << *(lhs.data() + i) - *(rhs.data() + i) << std::endl;
        }
        throw std::runtime_error("Matrices not equal (different content)");
    }
}

void LUSolver::assert_xs_equals(const std::vector<Vector1D>& lhs, const std::vector<Vector1D>& rhs) {
    assert_vector_equals(lhs, rhs);
}

void LUSolver::assert_bs_equals(const std::vector<Vector1D>& lhs, const std::vector<Vector1D>& rhs) {
    assert_vector_equals(lhs, rhs);
}

void LUSolver::init_matrix_from(Matrix2D& dst, const Matrix2D& src) {
    if (!same_shape(dst, src)) {
        throw std::runtime_error("Matrices not equal (different shapes)");
    }

    memcpy(dst.data(), src.data(), src.num_elements() * sizeof(MatrixTValue));
}

void LUSolver::init_xs_from(std::vector<Vector1D>& dst, const std::vector<Vector1D>& src) {
    init_vector_from(dst, src);
}

void LUSolver::init_bs_from(std::vector<Vector1D>& dst, const std::vector<Vector1D>& src) {
    init_vector_from(dst, src);
}

void LUSolver::init_all_from_file(Matrix2D& matrix, std::vector<Vector1D>& vectors,
                                  const std::string& filename) {
    using T = Vector1D::value_type;

    std::ifstream stream(filename);
    if (!stream.good()) {
        std::ostringstream err;
        err << "[LUSolver::init_all_from_file] Error while opening initialization file " << filename << std::endl;
        throw std::runtime_error(err.str());
    }

    std::string str;
    std::getline(stream, str);
    std::istringstream str_stream(str);
    // std::copy(std::istream_iterator<MatrixTValue>(str_stream), std::istream_iterator<MatrixTValue>(), matrix.data());
    for (int i = 0; i < matrix.num_elements(); ++i) {
        deserialize_float(str_stream, *(matrix.data() + i));
    }

    int nb_vectors;
    stream >> nb_vectors;
    
    vectors.resize(nb_vectors);

    for (int i = 0; i < nb_vectors; ++i) {
        vectors[i].resize(matrix.size());
        str.clear();
        std::getline(stream, str);
        str_stream.str(str);
        // std::copy(std::istream_iterator<T>(str_stream), std::istream_iterator<T>(), std::back_inserter(vectors[i]));
        for (int j = 0; j < matrix.size(); ++j) {
            deserialize_float(str_stream, vectors[i][j]);
        }
    }
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
    RandomGenerator<unsigned int> gen(1, matrix.size() / 10 + 1);

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

void LUSolver::init_from_file(std::string const& filename) {
    init_all_from_file(_matrix, _bs, filename);
}

void LUSolver::init_expected_from_file(const std::string& filename) {
    init_all_from_file(_expected, _xs, filename);
}

void LUSolver::_init() {
    throw std::runtime_error("");
}

void LUSolver::_init_expected() {
    throw std::runtime_error("");
}
