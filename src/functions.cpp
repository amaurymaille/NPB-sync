#include <cassert>
#include <cmath>

#include <thread>

#include <omp.h>

#include "active_promise.h"
#include "config.h"
#include "functions.h"
#include "increase.h"
#include "promise_plus.h"
#include "utils.h"

void heat_cpu(Matrix& array, size_t m) {
    namespace g = Globals;

    int* ptr = array.data();

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
void heat_cpu_switch_loops(Matrix& array, size_t m) {
    namespace g = Globals;

    int* ptr = array.data();

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

void heat_cpu_point_promise(Matrix& array, size_t m, PointPromiseStore& dst, const PointPromiseStore& src) {
    namespace g = Globals;

    int* ptr = array.data();

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
                dst->get()[omp_get_thread_num() + 1][promise_pos].set_value(/* ptr[pos] */);
            }
        }
    }
}

void heat_cpu_block_promise(Matrix& array, size_t m, BlockPromiseStore& dst, const BlockPromiseStore& src) {
    namespace g = Globals;

    int* ptr = array.data();
    int last_i = -1;

    /* std::optional<std::vector<MatrixValue>*> values = 
        src ? std::make_optional(src->get()[omp_get_thread_num()].get_future().get()) : std::nullopt; */
    if (src)
        src->get()[omp_get_thread_num()].get_future().get();

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


    /* if (values) {
        delete *values;
    } */
}

void heat_cpu_increasing_point_promise(Matrix& array, size_t m, 
                                      IncreasingPointPromiseStore& dst, 
                                      const IncreasingPointPromiseStore& src) {
    namespace g = Globals;

    // printf("[Thread %d] Starting iteration %d\n", omp_get_thread_num(), m);

    int* ptr = array.data();

    auto all_promises = src ? std::make_optional(std::ref(src->get()[omp_get_thread_num()])) : std::nullopt;

    auto promise_store_iter = all_promises ? std::make_optional(all_promises->get().begin()) : std::nullopt;

    auto values = promise_store_iter >>= [](auto& iter) {
        return iter->get_future().get();
    };

    /* auto values_iter = values >>= [](auto& vect) {
        return vect.begin();
    }; */

    // TODO: better way to do that ? More flexible maybe ?
    int nb_elements_for_neighbor = nb_points_for(m, 0, 0, 0);
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
        dst->get()[omp_get_thread_num() + 1][nb_vectors_filled].set_value(values_ready);
    }
}

void heat_cpu_jline_promise(Matrix& array, size_t m, JLinePromiseStore& dst, const JLinePromiseStore& src) {
    namespace g = Globals;

    int* ptr = array.data();

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

void heat_cpu_kline_promise(Matrix& array, size_t m, KLinePromiseStore& dst, const KLinePromiseStore& src) {
    namespace g = Globals;

    int* ptr = array.data();

    for (int j = 1; j < g::DIM_Y; ++j) {
        if (src)
            src->get()[omp_get_thread_num()][j].get_future().get();

        for (int k = 0; k < g::DIM_Z; ++k) {            
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
            dst->get()[omp_get_thread_num() + 1][j].set_value();
    }
}

void heat_cpu_increasing_jline_promise(Matrix& array, size_t m, 
                                       IncreasingJLinePromiseStore& dst, 
                                       const IncreasingJLinePromiseStore& src) {
    namespace g = Globals;

    int* ptr = array.data();

    // How many lines before we synchronize with our left neighbor (next get). Synchronization
    // occurs when this reaches 0.
    int remaining_lines = -1;
    // Which promise holds the number of lines we need to compute before synchronizing
    // with left neighbor
    int src_promise_index = 0;
    // Which promise holds the number of lines the right neighbor will need to process
    // before synchronizing with us
    int dst_promise_index = 0;
    // How many lines we have processed since the last time we synced (either right
    // or left because we sync with both usually).
    int processed_lines = 0;

    if (src)
        remaining_lines = src->get()[omp_get_thread_num()][src_promise_index].get_future().get();
    else
        remaining_lines = nb_jlines_for(m, 0);;

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

        --remaining_lines;
        ++processed_lines;

        if (remaining_lines == 0) {
            ++src_promise_index;

            if (src && k != g::DIM_Z - 1) { 
                auto& promises_array = src->get()[omp_get_thread_num()];
                if (src_promise_index < promises_array.size())
                    remaining_lines = promises_array[src_promise_index].get_future().get();
                else
                    remaining_lines = -1;
            }
            else
                remaining_lines = nb_jlines_for(m, k + 1);

            if (dst)
                dst->get()[omp_get_thread_num() + 1][dst_promise_index].set_value(processed_lines);

            processed_lines = 0;
            ++dst_promise_index;
        }
    }

    if (remaining_lines != 0 && dst) {
        auto& promises_array = dst->get()[omp_get_thread_num() + 1];
        if (dst_promise_index < promises_array.size())
            promises_array[dst_promise_index].set_value(processed_lines);
    }
}

void heat_cpu_increasing_kline_promise(Matrix& array, size_t m, 
                                       IncreasingKLinePromiseStore& dst, 
                                       const IncreasingKLinePromiseStore& src) {
    namespace g = Globals;

    int* ptr = array.data();

    // How many lines before we synchronize with our left neighbor (next get). Synchronization
    // occurs when this reaches 0.
    int remaining_lines = -1;
    // Which promise holds the number of lines we need to compute before synchronizing
    // with left neighbor
    int src_promise_index = 0;
    // Which promise holds the number of lines the right neighbor will need to process
    // before synchronizing with us
    int dst_promise_index = 0;
    // How many lines we have processed since the last time we synced (either right
    // or left because we sync with both usually).
    int processed_lines = 0;

    if (src)
        remaining_lines = src->get()[omp_get_thread_num()][src_promise_index].get_future().get();
    else
        remaining_lines = nb_klines_for(m, 1);

    for (int j = 1; j < g::DIM_Y; ++j) {
        for (int k = 0; k < g::DIM_Z; ++k) {            
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

        --remaining_lines;
        ++processed_lines;

        if (remaining_lines == 0) {
            ++src_promise_index;

            if (src && j != g::DIM_Y - 1) { 
                auto& promises_array = src->get()[omp_get_thread_num()];
                if (src_promise_index < promises_array.size())
                    remaining_lines = promises_array[src_promise_index].get_future().get();
                else
                    remaining_lines = -1;
            }
            else
                remaining_lines = nb_jlines_for(m, j + 1);

            if (dst)
                dst->get()[omp_get_thread_num() + 1][dst_promise_index].set_value(processed_lines);

            processed_lines = 0;
            ++dst_promise_index;
        }
    }

    if (remaining_lines != 0 && dst) {
        auto& promises_array = dst->get()[omp_get_thread_num() + 1];
        if (dst_promise_index < promises_array.size())
            promises_array[dst_promise_index].set_value(processed_lines);
    }
}

/*
void heat_cpu_block_promise_plus(Matrix& array, size_t m, BlockPromisePlusStore& dst, const BlockPromisePlusStore& src) {
    namespace g = Globals;

    int* ptr = array.data();

    int thread_num = omp_get_thread_num();
    if (thread_num)
        src->get()[thread_num - 1].get(m);

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

    if (thread_num != omp_get_num_threads() - 1)
        dst->get()[thread_num].set(m);
}

void heat_cpu_jline_promise_plus(Matrix& array, size_t m, JLinePromisePlusStore& dst, const JLinePromisePlusStore& src) {
    namespace g = Globals;

    int* ptr = array.data();
    int thread_num = omp_get_thread_num();

    for (int k = 0; k < g::DIM_Z; ++k) {
        if (thread_num)
            src->get()[thread_num - 1].get(k);

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

        if (thread_num < omp_get_num_threads() - 1)
            dst->get()[thread_num].set(k);
    }
}

void heat_cpu_kline_promise_plus(Matrix& array, size_t m, KLinePromisePlusStore& dst, const KLinePromisePlusStore& src) {
    namespace g = Globals;

    int* ptr = array.data();
    int thread_num = omp_get_thread_num();

    for (int j = 1; j < g::DIM_Y; ++j) {
        if (thread_num)
            src->get()[thread_num - 1].get(j);

        for (int k = 0; k < g::DIM_Z; ++k) {            
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

        if (thread_num < omp_get_num_threads() - 1)
            dst->get()[thread_num].set(j);
    }
}

void heat_cpu_increasing_jline_promise_plus(Matrix& array, size_t m, 
                                            IncreasingJLinePromisePlusStore& dst, 
                                            const IncreasingJLinePromisePlusStore& src) {
    namespace g = Globals;

    int* ptr = array.data();

    // How many lines before we synchronize with our left neighbor (next get), i.e
    // when we are out of lines to process
    int remaining_lines = -1;
    // How many lines we have processed since last synchronization with our right 
    // neighbor (next set), i;e when we have processed enough lines
    int processed_lines = 0;

    int thread_num = omp_get_thread_num();

    int prod_index = 0;
    int cons_index = 0;

    if (thread_num)
        remaining_lines = src->get()[thread_num - 1].get(cons_index);
    else
        remaining_lines = nb_jlines_for(m, 0);

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

        if (remaining_lines == 0 && k != g::DIM_Z - 1) {
            if (thread_num) {
                cons_index += processed_lines;

                remaining_lines = src->get()[thread_num - 1].get(cons_index);
            }
            else {
                remaining_lines = nb_jlines_for(m, k + 1);
            }

            if (thread_num < omp_get_num_threads() - 1) {
                dst->get()[thread_num].set(prod_index, processed_lines);
                prod_index += processed_lines;
            }

            processed_lines = 0;
        }
    }

    if (processed_lines != 0 && thread_num < omp_get_num_threads() - 1) {
        dst->get()[thread_num].set(prod_index, processed_lines);
    }
}

void heat_cpu_increasing_kline_promise_plus(Matrix& array, size_t m, 
                                            IncreasingKLinePromisePlusStore& dst, 
                                            const IncreasingKLinePromisePlusStore& src) {
    namespace g = Globals;

    int* ptr = array.data();

    // How many lines before we synchronize with our left neighbor (next get), i.e
    // when we are out of lines to process
    int remaining_lines = -1;
    // How many lines we have processed since last synchronization with our right 
    // neighbor (next set), i;e when we have processed enough lines
    int processed_lines = 0;

    int thread_num = omp_get_thread_num();

    int prod_index = 0;
    int cons_index = 0;

    if (thread_num)
        remaining_lines = src->get()[thread_num - 1].get(cons_index);
    else
        remaining_lines = nb_klines_for(m, 1);

    for (int j = 1; j < g::DIM_Y; ++j) {
        for (int k = 0; k < g::DIM_Z; ++k) {
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

        if (remaining_lines == 0 && j != g::DIM_Y - 1) {
            if (thread_num) {
                cons_index += processed_lines;

                remaining_lines = src->get()[thread_num - 1].get(cons_index);
            }
            else {
                remaining_lines = nb_jlines_for(m, j + 1);
            }

            if (thread_num < omp_get_num_threads() - 1) {
                dst->get()[thread_num].set(prod_index, processed_lines);
                prod_index += processed_lines;
            }

            processed_lines = 0;
        }
    }

    if (processed_lines != 0 && thread_num < omp_get_num_threads() - 1) {
        dst->get()[thread_num].set(prod_index, processed_lines);
    }
}
*/

void heat_cpu_promise_plus(Matrix& array, size_t m, PromisePlusStore& dst, const PromisePlusStore& src) {
    namespace g = Globals;

    int* ptr = array.data();

    for (int k = 0; k < g::DIM_Z; ++k) {
        if (src)
            (*src)[omp_get_thread_num()]->get(k);

        for (int j = 1; j < g::DIM_Y; ++j) {
            #pragma omp for schedule(static) nowait
            for (int i = 1; i < g::DIM_X; ++i) {
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

        if (dst)
            (*dst)[omp_get_thread_num() + 1]->set(k);
    }

    if (dst)
        (*dst)[omp_get_thread_num() + 1]->set_final(g::DIM_Z - 1);
}