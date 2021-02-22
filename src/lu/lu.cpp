#include <cstring>

#include <sstream>

#include <omp.h>

#include "dynamic_defines.h"
#include "fifo.h"
#include "lu.h"
#include "promise_plus.h"

namespace g = Globals;

static void prepare_output(Matrix2D const& matrix, Matrix2D& out);
static void validate_diagonal_and_prepare_output(Matrix2D const& matrix, Matrix2D& out);

void kernel_lu(Matrix2D const& matrix, Matrix2D& out) {
    validate_diagonal_and_prepare_output(matrix, out);

    // LU
    for (int k = 0; k < g::LU::DIM; ++k) {
        for (int i = k + 1; i < g::LU::DIM; ++i) {
            out[i][k] /= out[k][k];
        }

        for (int i = k + 1; i < g::LU::DIM; ++i) {
            for (int j = k + 1; j < g::LU::DIM; ++j) {
                out[i][j] -= out[i][k] * out[k][j];
            }
        }
    }
}

void kernel_lu_omp(Matrix2D const& matrix, Matrix2D& out) {
    validate_diagonal_and_prepare_output(matrix, out);

    // LU
    for (int k = 0; k < g::LU::DIM; ++k) {
        #pragma omp parallel for
        for (int i = k + 1; i < g::LU::DIM; ++i) {
            out[i][k] /= out[k][k];
        }

        #pragma omp parallel for
        for (int i = k + 1; i < g::LU::DIM; ++i) {
            for (int j = k + 1; j < g::LU::DIM; ++j) {
                out[i][j] -= out[i][k] * out[k][j];
            }
        }
    }
}

void kernel_lu_solve(Matrix2D const& lu, Vector1D const& b, Vector1D& x) {
    // In LUx = b, solve Ly = b, then Ux = y
    // Algorithm kernel_lu computes strictly lower L and diagonal upper U
    // L is assumed to have ones on the diagonal
    
    // Compute y
    // Reminder : L11 * y1 = b1, L21 * y1 + L22 * y2 = b2...
    // Ergo : yi = (bi - sigma (for j = 1 to i - 1) of (Li,j * yj)) / Lii
    Vector1D y;
    for (int i = 0; i < lu.size(); ++i) {
        auto row = lu[i];
        Matrix2DValue sum = 0;
        for (int j = 0; j < i - 1; j++) {
            sum += row[j] * y[j];
        }

        y[i] = (b[i] - sum) / row[i];
    }

    // Compute x
    // Reminder : Unn * xn = yn, Un-1n * xn + Un-1n-1 * xn-1 = yn-1
    // Ergo : xi = (yi - sigma (for j = i + 1 to n) of (Ui,j * xj)) / Uii
    for (int i = lu.size() - 1; i >= 0; --i) {
        auto row = lu[i];
        Matrix2DValue sum = 0;
        for (int j = i + 1; j < lu.size(); ++j) {
            sum += row[j] * x[j];
        }

        x[i] = (y[i] - sum) / row[i];
    }
}

void kernel_lu_solve_n(Matrix2D const& lu, std::vector<Vector1D> const& b,
                                           std::vector<Vector1D>& x) {
    std::vector<std::thread> threads;
    for (int i = 0; i < b.size(); ++i)
        threads.push_back(std::thread(kernel_lu_solve, std::ref(lu), std::cref(b[i]), std::ref(x[i])));

    for (auto& th: threads)
        th.join();
}



void kernel_lu_combine(Matrix2D& a, Vector1D const& b, Vector1D& x) {
    Matrix2D res;
    kernel_lu(a, res);
    kernel_lu_solve(res, b, x);
}

void kernel_lu_combine_n(Matrix2D& a, std::vector<Vector1D> const& b,
                                      std::vector<Vector1D>& x) {
    Matrix2D res;
    kernel_lu(a, res);

    for (int i = 0; i < b.size(); ++i) {
        kernel_lu_solve(res, b[i], x[i]);
    }
}

void kernel_lu_combine_omp(Matrix2D& a, Vector1D const& b, Vector1D& x) {
    Matrix2D res;

    kernel_lu_omp(a, res);
    kernel_lu_solve(res, b, x);
}

void kernel_lu_combine_n_omp(Matrix2D& a, std::vector<Vector1D> const& b,
                                          std::vector<Vector1D>& x) {
    Matrix2D res;
    std::vector<std::thread> threads;

    kernel_lu_omp(a, res);
    for (int i = 0; i < b.size(); ++i) {
        threads.push_back(std::thread(kernel_lu_solve, std::cref(res), std::cref(b[i]), std::ref(x[i])));
    }

    for (std::thread& th: threads) {
        th.join();
    }
}

void validate_diagonal(Matrix2D const& matrix) {
    // Assert if 0 on diagonal
    for (int i = 0; i < matrix.size(); ++i) {
        if (matrix[i][i] == 0) {
            std::ostringstream stream;
            stream << "[ERROR] LU: Matrix2D (" << i << ", " << i << ") is 0" << std::endl;
            throw std::runtime_error(stream.str());
        }
    }
}

void prepare_output(Matrix2D const& matrix, Matrix2D& out) {
    // Prepare output matrix
    out.resize(boost::extents[matrix.size()][matrix.size()]);
    memcpy(out.data(), matrix.data(), sizeof(Matrix2DValue) * matrix.size() * matrix.size());
}

void validate_diagonal_and_prepare_output(Matrix2D const& matrix, Matrix2D& out) {
    validate_diagonal(matrix);
    prepare_output(matrix, out);
} 
