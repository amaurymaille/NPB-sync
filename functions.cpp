#include <cassert>

#include <thread>

#include <omp.h>

#include "config.h"
#include "functions.h"
#include "utils.h"

void heat_cpu(Matrix array, size_t w) {
    namespace g = Globals;

    int thread_num = omp_get_thread_num();
    int omp_num_threads = omp_get_num_threads();
    int* ptr = reinterpret_cast<int*>(array);
    // printf("[%s][heat_cpu] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, w);

    #pragma omp for schedule(static) nowait
    for (int i = 1; i < g::DIM_X - 1; ++i) {
        for (int j = 1; j < g::DIM_Y; ++j) {
            for (int k = 0; k < g::DIM_Z; ++k) {
                size_t n = to1d(w, i, j, k);
                size_t nm1 = to1d(w, i - 1, j, k);
                size_t nm1j = to1d(w, i, j - 1, k);

                int orig = ptr[n];
                int to_add = ptr[nm1] + ptr[nm1j];

                int result = orig + to_add;
                ptr[n] = result;
                
                if (sConfig.heat_cpu_has_random_yield_and_sleep()) {
                    // Sleep only in OMP parallel, speed up the sequential version
                    if (omp_num_threads != 1) {
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

    // printf("[%s][heat_cpu] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, w);
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
void heat_cpu_switch_loops(Matrix array, size_t w) {
    namespace g = Globals;

    int thread_num = omp_get_thread_num();
    int omp_num_threads = omp_get_num_threads();
    int* ptr = reinterpret_cast<int*>(array);
    // printf("[%s][heat_cpu] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, w);

    for (int j = 1; j < g::DIM_Y; ++j) {
        for (int k = 0; k < g::DIM_Z; ++k) {
            #pragma omp for schedule(static) nowait
            for (int i = 1; i < g::DIM_X - 1; ++i) {
                size_t n = to1d(w, i, j, k);
                size_t nm1 = to1d(w, i - 1, j, k);
                size_t nm1j = to1d(w, i, j - 1, k);

                int orig = ptr[n];
                int to_add = ptr[nm1] + ptr[nm1j];

                int result = orig + to_add;
                ptr[n] = result;
                
                if (sConfig.heat_cpu_has_random_yield_and_sleep()) {
                    // Sleep only in OMP parallel, speed up the sequential version
                    if (omp_num_threads != 1) {
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

    // printf("[%s][heat_cpu] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, w);
}

namespace g = Globals;

void heat_cpu_switch_loops_promise(Matrix array, size_t w, 
                                   std::optional<std::reference_wrapper<std::array<std::vector<std::promise<MatrixValue>>, g::DIM_Y * g::DIM_Z>>>& dst,
                                   const std::optional<std::reference_wrapper<std::array<std::vector<std::promise<MatrixValue>>, g::DIM_Y * g::DIM_Z>>>& src) {
    namespace g = Globals;

    // printf("Working on array %p\n", dst ? (&dst->get()) : 0);
    int thread_num = omp_get_thread_num();
    int omp_num_threads = omp_get_num_threads();
    int* ptr = reinterpret_cast<int*>(array);

    for (int j = 1; j < g::DIM_Y; ++j) {
        for (int k = 0; k < g::DIM_Z; ++k) {
            bool used_value = false;
            int value = src ? src->get()[j * g::DIM_Z + k][omp_get_thread_num()].get_future().get() : -1;
            int last_i = -1;
            #pragma omp for schedule(static) nowait
            for (int i = 1; i < g::DIM_X - 1; ++i) {
                printf("[Thread %d] Iterating over i = %d\n", omp_get_thread_num(), i);
                if (!used_value && src) {
                    printf("[Thread %d] Accessing (%d, %d, %d, %d)\n", omp_get_thread_num(), w, i - 1, j, k);
                }

                size_t n = to1d(w, i, j, k);
                size_t nm1 = to1d(w, i - 1, j, k);
                size_t nm1j = to1d(w, i, j - 1, k);

                int orig = ptr[n];
                int to_add = (used_value || !src ? ptr[nm1] : value) + ptr[nm1j];

                used_value = true;

                int result = orig + to_add;
                ptr[n] = result;
                
                if (sConfig.heat_cpu_has_random_yield_and_sleep()) {
                    // Sleep only in OMP parallel, speed up the sequential version
                    if (omp_num_threads != 1) {
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
                size_t pos = to1d(w, last_i, j, k);
                printf("[Thread %d] Setting value for promise %d (%d, %d, %d, %d)\n", omp_get_thread_num(), j * g::DIM_Z + k, w, last_i, j, k);
                dst->get()[j * g::DIM_Z + k][omp_get_thread_num() + 1].set_value(ptr[pos]);
            }
        }
    }
}