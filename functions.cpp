#include <cassert>
#include <cmath>

#include <thread>

#include <omp.h>

#include "config.h"
#include "functions.h"
#include "increase.h"
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

void heat_cpu_point_promise(Matrix array, size_t m, PointPromiseStore& dst, const PointPromiseStore& src) {
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
                    src->get()[omp_get_thread_num()][promise_pos].get_future().get();
                    used_value = true;
                }

                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);
                size_t nm1m = to1d(m - 1, i, j, k);

                int orig = ptr[n];
                // int to_add = (used_value || !src ? ptr[nm1] : src->get()[omp_get_thread_num()][promise_pos].get_future().get()) + ptr[nm1j] + ptr[nm1m];
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
                last_i = i;
            }

            if (dst && last_i != -1) {
                // printf("[Thread %d] Setting value for promise %d (%d, %d, %d, %d)\n", omp_get_thread_num(), promise_pos, m, last_i, j, k);
                dst->get()[omp_get_thread_num() + 1][promise_pos].set_value(/* ptr[pos] */);
            }
        }
    }
}

void heat_cpu_block_promise(Matrix array, size_t m, BlockPromiseStore& dst, const BlockPromiseStore& src) {
    namespace g = Globals;

    int* ptr = reinterpret_cast<int*>(array);
    int last_i = -1;

    uint64 diff = 0;
    lldiv_t d;

    struct timespec begin, end;
    clock_gettime(CLOCK_MONOTONIC, &begin);
    /* std::optional<std::vector<MatrixValue>*> values = 
        src ? std::make_optional(src->get()[omp_get_thread_num()].get_future().get()) : std::nullopt; */
    if (src)
        src->get()[omp_get_thread_num()].get_future().get();
    clock_gettime(CLOCK_MONOTONIC, &end);
    d = lldiv(clock_diff(&end, &begin), BILLION);

    // printf("Synchro|%d:%d\n", d.quot, d.rem);
    
    clock_gettime(CLOCK_MONOTONIC, &begin);

    #pragma omp for schedule(static) nowait
    for (int i = 1; i < g::DIM_X; ++i) {
        for (int j = 1; j < g::DIM_Y; ++j) {
            for (int k = 0; k < g::DIM_Z; ++k) {
                // int promise_pos = j * g::DIM_Z + k;

                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);
                size_t nm1m = to1d(m - 1, i, j, k);

                int orig = ptr[n];
                // int to_add = (used_values || !values ? ptr[nm1] : (**values)[promise_pos]) + ptr[nm1j] + ptr[nm1m];
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

        last_i = i;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    diff = clock_diff(&end, &begin);
    d = lldiv(diff, BILLION);
    // printf("[Block][Thread %d] Iteration %d took %d:%d seconds\n", omp_get_thread_num(), m, d.quot, d.rem);

    clock_gettime(CLOCK_MONOTONIC, &begin);
    if (dst && last_i != -1) {
        // printf("[Thread %d] Setting promise at i = %d\n", omp_get_thread_num(), last_i);
        /* std::vector<MatrixValue>* arr = new std::vector<MatrixValue>(g::NB_VALUES_PER_BLOCK);
        for (int j = 1; j < g::DIM_Y; ++j) {
            for (int k = 0; k < g::DIM_Z; ++k) {
                size_t ptr_pos = to1d(m, last_i, j, k);
                size_t arr_pos = j * g::DIM_Z + k;
                (*arr)[arr_pos] = ptr[ptr_pos];
            }
        } */

        dst->get()[omp_get_thread_num() + 1].set_value();
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    d = lldiv(clock_diff(&end, &begin), BILLION);

    // printf("Thread %d|Block|VectorFill|Iteration %d|%d:%d\n", omp_get_thread_num(), m, d.quot, d.rem);


    /* if (values) {
        delete *values;
    } */
}

void heat_cpu_increasing_point_promise(Matrix array, size_t m, 
                                      IncreasingPointPromiseStore& dst, 
                                      const IncreasingPointPromiseStore& src) {
    namespace g = Globals;

    // printf("[Thread %d] Starting iteration %d\n", omp_get_thread_num(), m);

    int* ptr = reinterpret_cast<int*>(array);

    auto all_promises = src ? std::make_optional(std::ref(src->get()[omp_get_thread_num()])) : std::nullopt;

    auto promise_store_iter = all_promises ? std::make_optional(all_promises->get().begin()) : std::nullopt;

    auto values = promise_store_iter >>= [](auto& iter) {
        return iter->get_future().get();
    };

    /* auto values_iter = values >>= [](auto& vect) {
        return vect.begin();
    }; */

    // TODO: better way to do that ? More flexible maybe ?
    int nb_elements_for_neighbor = nb_points_for_iteration(m);
    // std::vector<MatrixValue> values_for_neighbor;
    size_t values_ready = 0;
    int nb_vectors_filled = 0;

    for (int j = 1; j < g::DIM_Y; ++j) {
        for (int k = 0; k < g::DIM_Z; ++k) {
            // bool used_value = false;
            int last_i = -1;
            
            #pragma omp for schedule(static) nowait
            for (int i = 1; i < g::DIM_X; ++i) {
                /* if (!used_value && src) {
                    // printf("[Thread %d] Getting (%d, %d, %d, %d) = %d\n", omp_get_thread_num(), m, i - 1, j, k, **values_iter);
                } */

                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);
                size_t nm1m = to1d(m - 1, i, j, k);

                int orig = ptr[n];
                // int to_add = (used_value || !src ? ptr[nm1] : (**values_iter)) + ptr[nm1j] + ptr[nm1m];
                int to_add = ptr[nm1] + ptr[nm1j] + ptr[nm1m];

                // used_value = true;

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
                // ++(*values_iter);
                --values.value();
                if (/* *values_iter == values->end() */ *values == 0) {
                    ++(*promise_store_iter);

                    if (*promise_store_iter != all_promises->get().end()) {
                        values = promise_store_iter >>= [](auto& iter) {
                            return iter->get_future().get();
                        };

                        /* values_iter = values >>= [](auto& vect) {
                            return vect.begin();
                        }; */
                    }
                }
            }

            if (dst && last_i != -1) {
                // size_t pos = to1d(m, last_i, j, k);
                // std::vector<std::promise<std::vector<MatrixValue>>>& target = dst->get()[omp_get_thread_num() + 1];
                std::vector<Promise<size_t>>& target = dst->get()[omp_get_thread_num() + 1];

                // printf("[Thread %d] Sending (%d, %d, %d, %d) = %d\n", omp_get_thread_num(), m, last_i, j, k, ptr[pos]);
                // values_for_neighbor.push_back(ptr[pos]);
                values_ready++;

                if (values_ready == nb_elements_for_neighbor) {
                    target[nb_vectors_filled].set_value(nb_elements_for_neighbor);
                    // values_for_neighbor.resize(0);
                    values_ready = 0;
                    ++nb_vectors_filled;
                }
            }
        }
    }

    if (/* values_for_neighbor.size() != 0 */ values_ready != 0) {
        // printf("[Thread %d] Leaving iteration %d with vector not empty\n", omp_get_thread_num(), m);
        dst->get()[omp_get_thread_num() + 1][nb_vectors_filled].set_value(values_ready);
    }

    // printf("[Thread %d] Finished iteration %d\n", omp_get_thread_num(), m);
}

void heat_cpu_jline_promise(Matrix array, size_t m, JLinePromiseStore& dst, const JLinePromiseStore& src) {
    namespace g = Globals;

    int* ptr = reinterpret_cast<int*>(array);

    for (int k = 0; k < g::DIM_Z; ++k) {
        if (src)
            src->get()[omp_get_thread_num()][k].get_future().get();

        for (int j = 1; j < g::DIM_Y; ++j) {            
            #pragma omp for schedule(static) nowait
            for (int i = 1; i < g::DIM_X; ++i) {
                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);
                size_t nm1m = to1d(m - 1, i, j, k);

                int orig = ptr[n];
                // int to_add = (used_value || !src ? ptr[nm1] : src->get()[omp_get_thread_num()][promise_pos].get_future().get()) + ptr[nm1j] + ptr[nm1m];
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

        if (dst)
            dst->get()[omp_get_thread_num() + 1][k].set_value();
    }
}

void heat_cpu_kline_promise(Matrix array, size_t m, KLinePromiseStore& dst, const KLinePromiseStore& src) {
    (void)array;
    (void)m;
    (void)dst;
    (void)src;
}

void heat_cpu_increasing_jline_promise(Matrix array, size_t m, 
                                       IncreasingJLinePromiseStore& dst, 
                                       const IncreasingJLinePromiseStore& src) {
    namespace g = Globals;

    int* ptr = reinterpret_cast<int*>(array);
    int nb_lines_for_neighbor = nb_jlines_for_iteration(m);

    // How many lines before we synchronize with our left neighbor (next get), i.e
    // when we are out of lines to process
    int remaining_lines = -1;
    // Which promise holds the number of lines we need to compute before synchronizing
    // with left neighbor
    int src_promise_index = 0;
    // Which promise holds the number of lines the right neighbor will need to process
    // before synchronizing with us
    int dst_promise_index = 0;
    // How many lines we have processed since last synchronization with our right 
    // neighbor (next set), i;e when we have processed enough lines
    int processed_lines = 0;

    // In practice, remaining_lines and processed_lines are mirrors : 
    // remaining_lines + processed_lines = nb_lines_for_neighbor. But it might
    // be interesting to change that, for example by increasing the amount of lines
    // we send to the neighbor as we progress.

    if (src)
        remaining_lines = src->get()[omp_get_thread_num()][src_promise_index].get_future().get();
    else
        remaining_lines = nb_lines_for_neighbor;

    for (int k = 0; k < g::DIM_Z; ++k) {
        for (int j = 1; j < g::DIM_Y; ++j) {            
            #pragma omp for schedule(static) nowait
            for (int i = 1; i < g::DIM_X; ++i) {
                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);
                size_t nm1m = to1d(m - 1, i, j, k);

                int orig = ptr[n];
                // int to_add = (used_value || !src ? ptr[nm1] : src->get()[omp_get_thread_num()][promise_pos].get_future().get()) + ptr[nm1j] + ptr[nm1m];
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

        ++processed_lines;
        --remaining_lines;

        if (remaining_lines == 0) {
            ++src_promise_index;

            if (src) { 
                auto& promises_array = src->get()[omp_get_thread_num()];
                if (src_promise_index < promises_array.size())
                    remaining_lines = promises_array[src_promise_index].get_future().get();
                else
                    remaining_lines = -1;
            }
            else
                remaining_lines = nb_lines_for_neighbor;
        }

        if (processed_lines == nb_lines_for_neighbor) {
            if (dst)
                dst->get()[omp_get_thread_num() + 1][dst_promise_index].set_value(processed_lines);

            ++dst_promise_index;
            processed_lines = 0;
        }
    }

    if (remaining_lines != 0 && dst) {
        auto& promises_array = dst->get()[omp_get_thread_num() + 1];
        if (dst_promise_index < promises_array.size())
            promises_array[dst_promise_index].set_value(processed_lines);
    }
}

void heat_cpu_increasing_kline_promise(Matrix, size_t, IncreasingKLinePromiseStore&, const IncreasingKLinePromiseStore&) {

}
