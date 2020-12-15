#include <cstring>

#include <sstream>

#include <omp.h>

#include "fifo.h"
#include "lu.h"
#include "promise_plus.h"

static void validate_diagonal(Matrix2D const& matrix);
static void prepare_output(Matrix2D const& matrix, Matrix2D& out);

static void validate_diagonal_and_prepare_output(Matrix2D const& matrix, Matrix2D& out);

void kernel_lu(Matrix2D const& matrix, Matrix2D& out) {
    validate_diagonal_and_prepare_output(matrix, out);

    // LU
    for (int k = 0; k < matrix.size(); ++k) {
        for (int i = k + 1; i < matrix.size(); ++i) {
            out[i][k] /= out[k][k];
        }

        for (int i = k + 1; i < matrix.size(); ++i) {
            for (int j = k + 1; j < matrix.size(); ++j) {
                out[i][j] -= out[i][k] * out[k][j];
            }
        }
    }
}

void kernel_lu_omp(Matrix2D const& matrix, Matrix2D& out) {
    validate_diagonal_and_prepare_output(matrix, out);

    // LU
    for (int k = 0; k < matrix.size(); ++k) {
        #pragma omp parallel for
        for (int i = k + 1; i < matrix.size(); ++i) {
            out[i][k] /= out[k][k];
        }

        #pragma omp parallel for
        for (int i = k + 1; i < matrix.size(); ++i) {
            for (int j = k + 1; j < matrix.size(); ++j) {
                out[i][j] -= out[i][k] * out[k][j];
            }
        }
    }
}

void kernel_lu_omp(Matrix2D const& matrix,
                   std::vector<PromisePlus<Matrix2DValue>>& promises) {

/*    std::vector<std::unique_ptr<PromisePlus<int>>> inner_promises(matrix.size());
    for (int i = 0; i < inner_promises.size(); ++i)
        inner_promises[i].reset(new PromisePlus<int>(matrix.size(), 1));
*/
    validate_diagonal(matrix);

    const size_t n = matrix.size();
    Matrix2D work(boost::extents[n][n]);
    memcpy(work.data(), matrix.data(), sizeof(Matrix2DValue) * n * n);
    std::vector<fifo<int>> indices(n);

#pragma omp parallel
{
    int n_threads = omp_get_num_threads();
    int rank = omp_get_thread_num();
    int bsize = n / n_threads;

    if (rank == 0) {
        for (int k = 0; k < n; ++k) {
            indices[rank + 1].push(k);

            for (int i = k + 1; i < bsize; ++i) {
                work[i][k] /= work[k][k];
                promises[i].set(k, work[i][k]);
            }

            for (int i = k + 1; i < bsize; ++i) {
                for (int j = k + 1; j < n; ++j) {
                    work[i][j] -= work[i][k] * work[k][j];
                    if (i == k + 1) {
                        promises[i].set(j, work[i][j]);
                    }
                }
            }
        }
    } else if (rank == n_threads - 1) {
        for (int k = 0; k < n; ++k) {
            int row = indices[rank].pop();

            // pragma parallel for workaround : if n % n_threads != 0
            // let the last thread process all the remaining rows
            // This is not optimal.
            for (int i = std::max(rank * bsize, row + 1); i < n; ++i) {
                work[i][row] /= work[row][row];
                promises[i].set(row, work[i][row]);
            }

            for (int i = std::max(rank * bsize, row + 1); i < n; ++i) {
                for (int j = row + 1; j < n; ++j) {
                    work[i][j] -= work[i][row] * work[row][j];

                    if (i == std::max(rank * bsize, row + 1)) {
                        promises[i].set(j, work[i][j]);
                    }
                }
            }
        }
    } else {
        for (int k = 0; k < n; ++k) {
            int row = indices[rank].pop();
            indices[rank + 1].push(row);

            // pragma parallel for workaround : if n % n_threads != 0
            // let the last thread process all the remaining rows
            // This is not optimal.
            for (int i = std::max(rank * bsize, row + 1); i < rank * bsize + bsize; ++i) {
                work[i][row] /= work[row][row];
                promises[i].set(row, work[i][row]);
            }

            for (int i = std::max(rank * bsize, row + 1); i < rank * bsize + bsize; ++i) {
                for (int j = row + 1; j < n; ++j) {
                    work[i][j] -= work[i][row] * work[row][j];

                    if (i == std::max(rank * bsize, row + 1)) {
                        promises[i].set(j, work[i][j]);
                    }
                }
            }
        }
    }
}
}

void validate_diagonal(Matrix2D const& matrix) {
    // Assert if 0 on diagonal
    for (int i = 0; i < matrix.size(); ++i) {
        if (matrix[i][i] == 0) {
            std::ostringstream stream;
            stream << "[ERROR] LU: Matrix (" << i << ", " << i << ") is 0" << std::endl;
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

