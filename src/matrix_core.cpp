#include "matrix_core.h"

// ----------------------------------------------------------------------------
// HeatCPUMatrix

namespace h = Globals::HeatCPU;

HeatCPUMatrix::HeatCPUMatrix() : _matrix(boost::extents[h::DIM_W][h::DIM_X][h::DIM_Y][h::DIM_Z]), 
    _expected(boost::extents[h::DIM_W][h::DIM_X][h::DIM_Y][h::DIM_Z]) {

}

void HeatCPUMatrix::init() {
    auto start_matrix_filename_opt = sDynamicConfigFiles.get_start_matrix_filename();
    if (start_matrix_filename_opt) {
        init_from_file(start_matrix_filename_opt.value());
    } else {
        _init();
    }

}

void HeatCPUMatrix::assert_okay_init(Matrix const& check) const {
    assert_matrix_equals(_matrix, check);
}

void HeatCPUMatrix::init_expected() {
    auto input_matrix_filename_opt = sDynamicConfigFiles.get_input_matrix_filename();
    if (input_matrix_filename_opt) {
        const std::string& filename = input_matrix_filename_opt.value();
        init_expected_from_file(filename);
    } else {
        compute_matrix(g_expected_matrix, g::HeatCPU::DIM_W, g::HeatCPU::DIM_X, g::HeatCPU::DIM_Y, g::HeatCPU::DIM_Z);
    }

}

void HeatCPUMatrix::assert_okay_expected(Matrix const& check) const {
    assert_matrix_equals(_expected, check);
}

void HeatCPUMatrix::init_from(Matrix const& other) {
    _init_matrix_from(_matrix, other);
}

void HeatCPUMatrix::init_expected_from(Matrix const& other) {
    _init_matrix_from(_expected, other);
}

void HeatCPUMatrix::assert_equals(Matrix const& other) const {
    assert_matrix_equals(_matrix, other);
}

void HeatCPUMatrix::assert_expected_equals(Matrix const& other) const {
    assert_matrix_equals(_expected, other);
}

void HeatCPUMatrix::_init() {
    init_matrix(_matrix, g::HeatCPU::NB_ELEMENTS);    
}

void HeatCPUMatrix::_init_expected() {
    init_matrix(_expected, g::HeatCPU::NB_ELEMENTS);
    compute_matrix(_expected, g::HeatCPU::DIM_W, g::HeatCPU::DIM_X, g::HeatCPU::DIM_Y, g::HeatCPU::DIM_Z);
}

void HeatCPUMatrix::init_from_file(std::string const& filename) {
    init_matrix_from_file(_matrix, filename);
}

void HeatCPUMatrix::init_expected_from_file(std::string const& filename) {
    init_matrix_from_file(_expected, filename);
}

static void HeatCPUMatrix::assert_matrix_equals(Matrix const& lhs, Matrix const& rhs) {
    if (lhs.size() != rhs.size()) {
        throw std::runtime_error("Matrices not equal");
    }

    if (memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(MatrixValue)) != 0) {
        throw std::runtime_error("Matrices not equal");
    }
}

static void HeatCPUMatrix::init_matrix_from(Matrix& dst, const Matrix& src) {
    if (dst.size() != src.size()) {
        throw std::runtime_error("Matrices don't have the same size !");
    }

    memcpy(dst.data(), src.data(), sizeof(Matrix::element) * src.size());
}

static void HeatCPUMatrix::init_matrix_from_file(Matrix& dst, const std::string& filename) {
    std::ifstream stream(filename);
    if (!stream.good()) {
        std::ostringstream err;
        err << "Unable to open input matrix file " << filename << std::endl;
        throw std::runtime_error(err.str());
    }

    std::copy(std::istream_iterator<Matrix::element>(stream), std::istream_iterator<Matrix::element>(), dst.data());
   
}

static void HeatCPUMatrix::init_matrix(Matrix& matrix, uint64 nb_elements) {
    Matrix::element* ptr = matrix.data();
    for (size_t i = 0; i < nb_elements; ++i) {
        ptr[i] = double((i % 10) + 1);
    }
}

static void HeatCPUMatrix::compute_matrix(Matrix& matrix, size_t dimw, size_t dimx, size_t dimy, size_t dimz) {
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
