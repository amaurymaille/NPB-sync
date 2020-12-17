#include <cstring>

#include <sstream>

#include <omp.h>

#include "fifo.h"
#include "lu.h"
#include "promise_plus.h"

static void validate_diagonal(Matrix2D const& matrix);
static void prepare_output(Matrix2D const& matrix, Matrix2D& out);
static void validate_diagonal_and_prepare_output(Matrix2D const& matrix, Matrix2D& out);

static void init_promise_plus_vector(Matrix2D const& matrix,
    std::vector<PromisePlus<Matrix2DValue>*>& promises,
    PromisePlusBuilder<Matrix2DValue> const& builder);

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

void kernel_lu_omp_pp(Matrix2D const& matrix,
                      std::vector<PromisePlus<Matrix2DValue>*>& promises) {

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
                
                if (i == bsize - 1) {
                    promises[i]->set_immediate(k, work[i][k]);
                } else {
                    promises[i]->set(k, work[i][k]);
                }
            }

            for (int i = k + 1; i < bsize; ++i) {
                for (int j = k + 1; j < n; ++j) {
                    work[i][j] -= work[i][k] * work[k][j];
                    if (i == k + 1) {
                        if (j == n -1) {
                            promises[i]->set_immediate(j, work[i][j]); 
                        } else {
                            promises[i]->set(j, work[i][j]);
                        }
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

                if (i == rank * bsize + bsize - 1) {
                    promises[i]->set_immediate(row, work[i][row]);
                } else {
                    promises[i]->set(row, work[i][row]);
                }
            }

            for (int i = std::max(rank * bsize, row + 1); i < n; ++i) {
                for (int j = row + 1; j < n; ++j) {
                    work[i][j] -= work[i][row] * work[row][j];

                    if (i == std::max(rank * bsize, row + 1)) {
                        if (j == n - 1) {
                            promises[i]->set_immediate(j, work[i][j]);
                        } else {
                            promises[i]->set(j, work[i][j]);
                        }
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

                if (i == rank * bsize + bsize - 1) {
                    promises[i]->set_immediate(row, work[i][row]);
                } else {
                    promises[i]->set(row, work[i][row]);
                }
            }

            for (int i = std::max(rank * bsize, row + 1); i < rank * bsize + bsize; ++i) {
                for (int j = row + 1; j < n; ++j) {
                    work[i][j] -= work[i][row] * work[row][j];

                    if (i == std::max(rank * bsize, row + 1)) {
                        if (j == n - 1) {
                            promises[i]->set_immediate(j, work[i][j]);
                        } else {
                            promises[i]->set(j, work[i][j]);
                        }
                    }
                }
            }
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

void kernel_lu_solve_pp(std::vector<PromisePlus<Matrix2DValue>*>& lu,
                        Vector1D const& b, Vector1D& x) {
    Vector1D y;
    for (int i = 0; i < lu.size(); ++i) {
        Matrix2DValue sum = 0;
        for (int j = 0; j < i - 1; j++) {
            sum += lu[i]->get(j) * y[j];
        }

        y[i] = (b[i] - sum) / lu[i]->get(i);
    }

    // Compute x
    // Reminder : Unn * xn = yn, Un-1n * xn + Un-1n-1 * xn-1 = yn-1
    // Ergo : xi = (yi - sigma (for j = i + 1 to n) of (Ui,j * xj)) / Uii
    for (int i = lu.size() - 1; i >= 0; --i) {
        Matrix2DValue sum = 0;
        for (int j = i + 1; j < lu.size(); ++j) {
            sum += lu[i]->get(j) * x[j];
        }

        x[i] = (y[i] - sum) / lu[i]->get(i);
    }
}

void kernel_lu_solve_n_pp(std::vector<PromisePlus<Matrix2DValue>*>& lu,
                          std::vector<Vector1D> const& b,
                          std::vector<Vector1D>& x) {
    std::vector<std::thread> threads;
    for (int i = 0; i < b.size(); ++i)
        threads.push_back(std::thread(kernel_lu_solve_pp, std::ref(lu), std::cref(b[i]), std::ref(x[i])));

    for (auto& th: threads)
        th.join(); 
}

void kernel_lu_combine_pp(Matrix2D& a, Vector1D const& b, Vector1D& x,
                          PromisePlusBuilder<Matrix2DValue> const& builder) {
    std::vector<PromisePlus<Matrix2DValue>*> promises;
    init_promise_plus_vector(a, promises, builder);

    std::thread lu(kernel_lu_omp_pp, std::cref(a), std::ref(promises));
    std::thread solver(kernel_lu_solve_pp, std::ref(promises), std::cref(b), std::ref(x));

    lu.join();
    solver.join();     
}

void kernel_lu_combine_n_pp(Matrix2D& a, std::vector<Vector1D>& b, 
                                         std::vector<Vector1D>& x,
                                         PromisePlusBuilder<Matrix2DValue> const& builder
                                         ) {
    std::vector<PromisePlus<Matrix2DValue>*> promises; 
    init_promise_plus_vector(a, promises, builder);

    std::thread lu(kernel_lu_omp_pp, std::cref(a), std::ref(promises));
    kernel_lu_solve_n_pp(promises, b, x);

    lu.join();
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

void init_promise_plus_vector(Matrix2D const& matrix,
    std::vector<PromisePlus<Matrix2DValue>*>& promises,
    PromisePlusBuilder<Matrix2DValue> const& builder) {
    for (int i = 0; i < matrix.size(); ++i)
        promises.push_back(builder.new_promise());
}
