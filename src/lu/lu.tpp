#include <fifo.h>

void validate_diagonal(Matrix2D const&);

template<DynamicStepPromiseMode mode>
static void init_promise_plus_vector(Matrix2D const& matrix,
    std::vector<DynamicStepPromise<Matrix2DValue, mode>*>& promises,
    DynamicStepPromiseBuilder<Matrix2DValue, mode> const& builder);

template<DynamicStepPromiseMode mode>
void kernel_lu_omp_pp(Matrix2D const& matrix,
                      std::vector<DynamicStepPromise<Matrix2DValue, mode>*>& promises) {
    namespace g = Globals;
/*    std::vector<std::unique_ptr<DynamicStepPromise<int>>> inner_promises(matrix.size());
    for (int i = 0; i < inner_promises.size(); ++i)
        inner_promises[i].reset(new DynamicStepPromise<int>(matrix.size(), 1));
*/
    validate_diagonal(matrix);

    constexpr const size_t n = g::LU::DIM;
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

template<DynamicStepPromiseMode mode>
void kernel_lu_solve_pp(std::vector<DynamicStepPromise<Matrix2DValue, mode>*>& lu,
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

template<DynamicStepPromiseMode mode>
void kernel_lu_solve_n_pp(std::vector<DynamicStepPromise<Matrix2DValue, mode>*>& lu,
                          std::vector<Vector1D> const& b,
                          std::vector<Vector1D>& x) {
    std::vector<std::thread> threads;
    for (int i = 0; i < b.size(); ++i)
        threads.push_back(std::thread(kernel_lu_solve_pp<mode>, std::ref(lu), std::cref(b[i]), std::ref(x[i])));

    for (auto& th: threads)
        th.join(); 

}
template<DynamicStepPromiseMode mode>
void kernel_lu_combine_pp(Matrix2D& a, Vector1D const& b, Vector1D& x,
                          DynamicStepPromiseBuilder<Matrix2DValue, mode> const& builder) {
    std::vector<DynamicStepPromise<Matrix2DValue, mode>*> promises;
    init_promise_plus_vector(a, promises, builder);

    std::thread lu(kernel_lu_omp_pp<mode>, std::cref(a), std::ref(promises));
    kernel_lu_solve_pp(promises, b, x);

    lu.join();
}

template<DynamicStepPromiseMode mode>
void kernel_lu_combine_n_pp(Matrix2D& a, std::vector<Vector1D> const& b, 
                                         std::vector<Vector1D>& x,
                                         DynamicStepPromiseBuilder<Matrix2DValue, mode> const& builder
                                         ) {
    std::vector<DynamicStepPromise<Matrix2DValue, mode>*> promises; 
    init_promise_plus_vector(a, promises, builder);

    std::thread lu(kernel_lu_omp_pp<mode>, std::cref(a), std::ref(promises));
    kernel_lu_solve_n_pp(promises, b, x);

    lu.join();
}

template<DynamicStepPromiseMode mode>
void init_promise_plus_vector(Matrix2D const& matrix,
    std::vector<DynamicStepPromise<Matrix2DValue, mode>*>& promises,
    DynamicStepPromiseBuilder<Matrix2DValue, mode> const& builder) {
    for (int i = 0; i < matrix.size(); ++i)
        promises.push_back(static_cast<DynamicStepPromise<Matrix2DValue, mode>*>(builder.new_promise()));
}
