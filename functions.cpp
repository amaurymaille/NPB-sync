#include <cassert>
#include <cmath>

#include <thread>

#include <omp.h>

#include "config.h"
#include "functions.h"
#include "utils.h"

void heat_cpu(Matrix array, size_t m) {
    namespace g = Globals;

    int* ptr = reinterpret_cast<int*>(array);

    #pragma omp for schedule(static) nowait
    for (int i = 1; i < g::DIM_X; ++i) {
        for (int j = 1; j < g::DIM_Y; ++j) {
            for (int k = 0; k < g::DIM_Z; ++k) {
                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);
                size_t nm1m = to1d(m - 1, i, j, k);

                int orig = ptr[n];
                int to_add = ptr[nm1] + ptr[nm1j] + ptr[nm1m];

                int result = orig + to_add;
                ptr[n] = result;
                
                if (sConfig.heat_cpu_has_random_yield_and_sleep()) {
                    // Sleep only in OMP parallel, speed up the sequential version
                    if (omp_get_num_threads() != 1) {
                        if (g::binary_generator()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(g::sleep_generator()));
                        } else {
                            std::this_thread::yield();
                        }
                    }
                }
            }
        }
    }
}

// In this version we switch the i / k and k / j loops, in order to complete a line before
// completing a column in the (restricted) sub-array extracted from the second and
// third dimensions of "array". The main dependency between threads, the one requiring
// (hard) synchronization is the one depending on the "i" variable, as it is the loop
// over the second dimension that is parallelized, and computations required the use
// of a value at index (i - 1).
//
// Switching the order of the loops will probably have dramatic consequences for 
// cache coherency, so this has to be checked.
void heat_cpu_switch_loops(Matrix array, size_t m) {
    namespace g = Globals;

    int* ptr = reinterpret_cast<int*>(array);

    for (int j = 1; j < g::DIM_Y; ++j) {
        for (int k = 0; k < g::DIM_Z; ++k) {
            #pragma omp for schedule(static) nowait
            for (int i = 1; i < g::DIM_X; ++i) {
                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);
                size_t nm1m = to1d(m - 1, i, j ,k);

                int orig = ptr[n];
                int to_add = ptr[nm1] + ptr[nm1j] + ptr[nm1m];

                int result = orig + to_add;
                ptr[n] = result;
                
                if (sConfig.heat_cpu_has_random_yield_and_sleep()) {
                    // Sleep only in OMP parallel, speed up the sequential version
                    if (omp_get_num_threads() != 1) {
                        if (g::binary_generator()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(g::sleep_generator()));
                        } else {
                            std::this_thread::yield();
                        }
                    }
                }
            }
        }
    }
}

namespace g = Globals;

void heat_cpu_line_promise(Matrix array, size_t m, LinePromiseStore& dst, const LinePromiseStore& src) {
    namespace g = Globals;

    int* ptr = reinterpret_cast<int*>(array);

    for (int j = 1; j < g::DIM_Y; ++j) {
        for (int k = 0; k < g::DIM_Z; ++k) {
            bool used_value = false;
            int promise_pos = j * g::DIM_Z + k;
            int last_i = -1;
            
            #pragma omp for schedule(static) nowait
            for (int i = 1; i < g::DIM_X; ++i) {
                if (!used_value && src) {
                    // printf("[Thread %d] Accessing (%d, %d, %d, %d)\n", omp_get_thread_num(), m, i - 1, j, k);
                }

                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);
                size_t nm1m = to1d(m - 1, i, j, k);

                int orig = ptr[n];
                int to_add = (used_value || !src ? ptr[nm1] : src->get()[omp_get_thread_num()][promise_pos].get_future().get()) + ptr[nm1j] + ptr[nm1m];

                used_value = true;

                int result = orig + to_add;
                ptr[n] = result;
                
                if (sConfig.heat_cpu_has_random_yield_and_sleep()) {
                    // Sleep only in OMP parallel, speed up the sequential version
                    if (omp_get_num_threads() != 1) {
                        if (g::binary_generator()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(g::sleep_generator()));
                        } else {
                            std::this_thread::yield();
                        }
                    }
                }
                last_i = i;
            }

            if (dst && last_i != -1) {
                size_t pos = to1d(m, last_i, j, k);
                // printf("[Thread %d] Setting value for promise %d (%d, %d, %d, %d)\n", omp_get_thread_num(), promise_pos, m, last_i, j, k);
                dst->get()[omp_get_thread_num() + 1][promise_pos].set_value(ptr[pos]);
            }
        }
    }
}

void heat_cpu_block_promise(Matrix array, size_t m, BlockPromiseStore& dst, const BlockPromiseStore& src) {
    namespace g = Globals;

    int* ptr = reinterpret_cast<int*>(array);
    bool used_values = false;
    int last_i = -1;

    // There is a copy here and it angers me (copy of the array stored in the promise)
    std::optional<std::array<MatrixValue, g::NB_VALUES_PER_BLOCK>> values = 
        src ? std::make_optional(src->get()[omp_get_thread_num()].get_future().get()) : std::nullopt;

    #pragma omp for schedule(static) nowait
    for (int i = 1; i < g::DIM_X; ++i) {
        if (!used_values && src) {
            printf("[Thread %d] Getting promise at i = %d\n", omp_get_thread_num(), i);
        }

        for (int j = 1; j < g::DIM_Y; ++j) {
            for (int k = 0; k < g::DIM_Z; ++k) {
                int promise_pos = j * g::DIM_Z + k;

                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);
                size_t nm1m = to1d(m - 1, i, j, k);

                int orig = ptr[n];
                int to_add = (used_values || !values ? ptr[nm1] : (*values)[promise_pos]) + ptr[nm1j] + ptr[nm1m];

                int result = orig + to_add;
                ptr[n] = result;
                
                if (sConfig.heat_cpu_has_random_yield_and_sleep()) {
                    // Sleep only in OMP parallel, speed up the sequential version
                    if (omp_get_num_threads() != 1) {
                        if (g::binary_generator()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(g::sleep_generator()));
                        } else {
                            std::this_thread::yield();
                        }
                    }
                }
                
            }
        }

        used_values = true;
        last_i = i;
    }

    if (dst && last_i != -1) {
        printf("[Thread %d] Setting promise at i = %d\n", omp_get_thread_num(), last_i);
        std::array<MatrixValue, g::NB_VALUES_PER_BLOCK> arr;
        for (int j = 1; j < g::DIM_Y; ++j) {
            for (int k = 0; k < g::DIM_Z; ++k) {
                size_t ptr_pos = to1d(m, last_i, j, k);
                size_t arr_pos = j * g::DIM_Z + k;
                arr[arr_pos] = ptr[ptr_pos];
            }
        }

        dst->get()[omp_get_thread_num() + 1].set_value(arr);
    }
}

void heat_cpu_increasing_line_promise(Matrix array, size_t m, 
                                      IncreasingLinePromiseStore& dst, 
                                      const IncreasingLinePromiseStore& src) {
    namespace g = Globals;

    printf("[Thread %d] Starting iteration %d\n", omp_get_thread_num(), m);

    int* ptr = reinterpret_cast<int*>(array);
    
    /* auto all_promises = src >>= [](const IncreasingLinePromiseStore::value_type& v) {
        return std::move(v.get()[omp_get_thread_num()]);
    }; */

    auto all_promises = src ? std::make_optional(std::ref(src->get()[omp_get_thread_num()])) : std::nullopt;

    auto promise_store_iter = all_promises ? std::make_optional(all_promises->get().begin()) : std::nullopt;

    auto values = promise_store_iter >>= [](auto& iter) {
        return iter->get_future().get();
    };

    auto values_iter = values >>= [](auto& vect) {
        return vect.begin();
    };

    // TODO: better way to do that ? More flexible maybe ?
    int nb_elements_for_neighbor = m < g::INCREASING_LINES_ITERATION_LIMIT ? std::pow(g::INCREASING_LINES_BASE_POWER, m - 1) : g::NB_LINES_PER_ITERATION;
    std::vector<MatrixValue> values_for_neighbor;
    int nb_vectors_filled = 0;

    for (int j = 1; j < g::DIM_Y; ++j) {
        for (int k = 0; k < g::DIM_Z; ++k) {
            bool used_value = false;
            int last_i = -1;
            
            #pragma omp for schedule(static) nowait
            for (int i = 1; i < g::DIM_X; ++i) {
                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);
                size_t nm1m = to1d(m - 1, i, j, k);

                int orig = ptr[n];
                int to_add = (used_value || !src ? ptr[nm1] : (**values_iter)) + ptr[nm1j] + ptr[nm1m];

                used_value = true;

                int result = orig + to_add;
                ptr[n] = result;
                
                if (sConfig.heat_cpu_has_random_yield_and_sleep()) {
                    // Sleep only in OMP parallel, speed up the sequential version
                    if (omp_get_num_threads() != 1) {
                        if (g::binary_generator()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(g::sleep_generator()));
                        } else {
                            std::this_thread::yield();
                        }
                    }
                }
                last_i = i;
            }

            if (src) {
                if (*values_iter == values->end()) {
                    ++(*promise_store_iter);

                    values = promise_store_iter >>= [](auto& iter) {
                        return iter->get_future().get();
                    };

                    values_iter = values >>= [](auto& vect) {
                        return vect.begin();
                    };
                } else {
                    ++(*values_iter);
                }
            }

            if (dst && last_i != -1) {
                size_t pos = to1d(m, last_i, j, k);
                std::vector<std::promise<std::vector<MatrixValue>>>& target = dst->get()[omp_get_thread_num() + 1];

                values_for_neighbor.push_back(ptr[pos]);

                if (values_for_neighbor.size() == nb_elements_for_neighbor) {
                    target[nb_vectors_filled].set_value(values_for_neighbor);
                    values_for_neighbor.resize(0);
                    ++nb_vectors_filled;
                }
            }
        }
    }

    if (values_for_neighbor.size() != 0) {
        printf("[Thread %d] Leaving iteration %d with vector not empty\n", omp_get_thread_num(), m);
        dst->get()[omp_get_thread_num() + 1][nb_vectors_filled].set_value(values_for_neighbor);
    }

    printf("[Thread %d] Finished iteration %d\n", omp_get_thread_num(), m);
}