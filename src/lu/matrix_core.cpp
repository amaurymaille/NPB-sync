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
// LUMatrix

namespace lu = Globals::LU;

LUMatrix::LUMatrix() {
    _matrix.resize(boost::extents[lu::DIM][lu::DIM]); 
    _expected.resize(boost::extents[lu::DIM][lu::DIM]);
}

void LUMatrix::init() {
    auto start_matrix_filename_opt = sDynamicConfigFiles.get_start_matrix_filename();
    if (start_matrix_filename_opt) {
        init_from_file(start_matrix_filename_opt.value());
    } else {
        _init();
    }
}

void LUMatrix::assert_okay_init(const Matrix2D& other) const {
    assert_matrix_equals(_matrix, other);
}

void LUMatrix::init_expected() {
    auto input_matrix_filename_opt = sDynamicConfigFiles.get_input_matrix_filename();
    if (input_matrix_filename_opt) {
        const std::string& filename = input_matrix_filename_opt.value();
        init_expected_from_file(filename);
    } else {
        compute_matrix(_expected, lu::DIM, lu::DIM);
    }
}

void LUMatrix::assert_okay_expected(const Matrix2D& other) const {
    assert_matrix_equals(_expected, other);
}

void LUMatrix::init_from(Matrix2D const& other) {
    init_matrix_from(_matrix, other);
}

void LUMatrix::init_expected_from(Matrix2D const& other) {
    init_matrix_from(_expected, other);
}

void LUMatrix::assert_equals(Matrix2D const& other) const {
    assert_matrix_equals(_matrix, other);
}

void LUMatrix::assert_expected_equals(const Matrix2D& other) const {
    assert_matrix_equals(_expected, other);
}

void LUMatrix::assert_matrix_equals(Matrix2D const& lhs, Matrix2D const& rhs) {
    if (!same_shape(lhs, rhs)) {
        throw std::runtime_error("Matrices not equal (different shapes)");
    }

    if (memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(MatrixTValue))) {
        throw std::runtime_error("Matrices not equal (different content)");
    }
}

void LUMatrix::init_matrix_from(Matrix2D& dst, const Matrix2D& src) {
    if (!same_shape(dst, src)) {
        throw std::runtime_error("Matrices not equal (different shapes)");
    }

    memcpy(dst.data(), src.data(), src.size() * sizeof(MatrixTValue));
}

void LUMatrix::init_matrix_from_file(Matrix2D& dst, const std::string& filename) {
    std::ifstream stream(filename);
    if (!stream.good()) {
        std::ostringstream err_stream;
        err_stream << "Error while opening file " << filename << std::endl;
        throw std::runtime_error(err_stream.str());
    }

    std::copy(std::istream_iterator<MatrixTValue>(stream), std::istream_iterator<MatrixTValue>(), dst.data());
}

void LUMatrix::init_matrix(Matrix2D& matrix, uint64 nb_elements) {
    throw std::runtime_error("Non");
}

void LUMatrix::compute_matrix(Matrix2D& matrix, size_t dimx, size_t dimy) {
    kernel_lu(sLU.get_matrix(), matrix);
}

void LUMatrix::_init() {
    init_matrix(_matrix, lu::DIM * lu::DIM);
}

void LUMatrix::_init_expected() {
    init_matrix(_expected, lu::DIM * lu::DIM);
    compute_matrix(_expected, lu::DIM, lu::DIM);
}

void LUMatrix::init_from_file(std::string const& filename) {
    init_matrix_from_file(_matrix, filename);
}

void LUMatrix::init_expected_from_file(std::string const& filename) {
    init_matrix_from_file(_expected, filename);
}
