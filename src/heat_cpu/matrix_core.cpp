#include <cstring>

#include <algorithm>
#include <iterator>
#include <sstream>
#include <stdexcept>

#include "dynamic_config.h"
#include "heat_cpu/dynamic_defines.h"
#include "heat_cpu/matrix_core.h"
#include "utils.h"

// ----------------------------------------------------------------------------
// HeatCPUMatrix

namespace h = Globals::HeatCPU;

HeatCPUMatrix::HeatCPUMatrix() {
    _matrix.resize(boost::extents[h::DIM_W][h::DIM_X][h::DIM_Y][h::DIM_Z]);
    _expected.resize(boost::extents[h::DIM_W][h::DIM_X][h::DIM_Y][h::DIM_Z]);
}

void HeatCPUMatrix::init() {
    auto start_matrix_filename_opt = sDynamicConfigFiles.get_start_matrix_filename();
    if (start_matrix_filename_opt) {
        init_from_file(start_matrix_filename_opt.value());
    } else {
        _init();
    }
}

void HeatCPUMatrix::assert_okay_init(Matrix4D const& check) const {
    assert_matrix_equals(_matrix, check);
}

void HeatCPUMatrix::init_expected() {
    auto input_matrix_filename_opt = sDynamicConfigFiles.get_input_matrix_filename();
    if (input_matrix_filename_opt) {
        const std::string& filename = input_matrix_filename_opt.value();
        init_expected_from_file(filename);
    } else {
        compute_matrix(_expected, h::DIM_W, h::DIM_X, h::DIM_Y, h::DIM_Z);
    }

}

void HeatCPUMatrix::assert_okay_expected(Matrix4D const& check) const {
    assert_matrix_equals(_expected, check);
}

void HeatCPUMatrix::init_from(Matrix4D const& other) {
    init_matrix_from(_matrix, other);
}

void HeatCPUMatrix::init_expected_from(Matrix4D const& other) {
    init_matrix_from(_expected, other);
}

void HeatCPUMatrix::assert_equals(Matrix4D const& other) const {
    assert_matrix_equals(_matrix, other);
}

void HeatCPUMatrix::assert_expected_equals(Matrix4D const& other) const {
    assert_matrix_equals(_expected, other);
}

void HeatCPUMatrix::_init() {
    init_matrix(_matrix, h::NB_ELEMENTS);    
}

void HeatCPUMatrix::_init_expected() {
    init_matrix(_expected, h::NB_ELEMENTS);
    compute_matrix(_expected, h::DIM_W, h::DIM_X, h::DIM_Y, h::DIM_Z);
}

void HeatCPUMatrix::init_from_file(std::string const& filename) {
    init_matrix_from_file(_matrix, filename);
}

void HeatCPUMatrix::init_expected_from_file(std::string const& filename) {
    init_matrix_from_file(_expected, filename);
}

void HeatCPUMatrix::assert_matrix_equals(Matrix4D const& lhs, Matrix4D const& rhs) {
    if (!same_shape(lhs, rhs)) {
        throw std::runtime_error("Matrices not equal (different shapes)");
    }

    if (memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(MatrixTValue)) != 0) {
        throw std::runtime_error("Matrices not equal (different content)");
    }
}

void HeatCPUMatrix::init_matrix_from(Matrix4D& dst, const Matrix4D& src) {
    if (!same_shape(dst, src)) {
        throw std::runtime_error("Matrices don't have the same shape !");
    }

    memcpy(dst.data(), src.data(), sizeof(MatrixTValue) * src.size());
}

void HeatCPUMatrix::init_matrix_from_file(Matrix4D& dst, const std::string& filename) {
    std::ifstream stream(filename);
    if (!stream.good()) {
        std::ostringstream err;
        err << "Unable to open input matrix file " << filename << std::endl;
        throw std::runtime_error(err.str());
    }

    std::copy(std::istream_iterator<MatrixTValue>(stream), std::istream_iterator<MatrixTValue>(), dst.data());
   
}

void HeatCPUMatrix::init_matrix(Matrix4D& matrix, uint64 nb_elements) {
    Matrix4D::element* ptr = matrix.data();
    for (size_t i = 0; i < nb_elements; ++i) {
        ptr[i] = double((i % 10) + 1);
    }
}

void HeatCPUMatrix::compute_matrix(Matrix4D& matrix, size_t dimw, size_t dimx, size_t dimy, size_t dimz) {
    for (int m = 1; m < dimw; ++m) {
        for (int i = 1; i < dimx; ++i) {
            for (int j = 1; j < dimy; ++j) {
                for (int k = 0; k < dimz; ++k) {
                    HeatCPUMatrix::update_matrix(matrix, m, i, j, k);
                }
            }
        }
    }
}


