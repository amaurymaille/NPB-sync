#include <cassert>

#include <thread>

#include <omp.h>

#include "config.h"
#include "functions.h"
#include "utils.h"

void heat_cpu(Matrix array, size_t k) {
    namespace g = Globals;

    int thread_num = omp_get_thread_num();
    int omp_num_threads = omp_get_num_threads();
    int* ptr = reinterpret_cast<int*>(array);

    for (int m = 0; m < g::DIM_W; ++m) {
        for (int i = 1; i < g::DIM_X; ++i) {
            #pragma omp for schedule(static) nowait
            for (int j = 1; j < g::DIM_Y; ++j) {
                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);
                size_t nm1k = to1d(m, i, j, k - 1);

                int orig = ptr[n];
                int to_add = ptr[nm1] + ptr[nm1j] + ptr[nm1k];

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
/* void heat_cpu_switch_loops(Matrix array, size_t k) {
    namespace g = Globals;

    int thread_num = omp_get_thread_num();
    int omp_num_threads = omp_get_num_threads();
    int* ptr = reinterpret_cast<int*>(array);

    for (int j = 1; j < g::DIM_Y; ++j) {
        for (int m = 0; m < g::DIM_W; ++m) {
            #pragma omp for schedule(static) nowait
            for (int i = 1; i < g::DIM_X - 1; ++i) {
                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);

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
} */

namespace g = Globals;

void heat_cpu_promise(Matrix array, size_t k, 
                                    std::optional<std::reference_wrapper<std::array<std::vector<std::promise<MatrixValue>>, g::DIM_W * g::DIM_X>>>& dst,
                                    const std::optional<std::reference_wrapper<std::array<std::vector<std::promise<MatrixValue>>, g::DIM_W * g::DIM_X>>>& src) {
    namespace g = Globals;

    int thread_num = omp_get_thread_num();
    int omp_num_threads = omp_get_num_threads();
    int* ptr = reinterpret_cast<int*>(array);

    for (int m = 0; m < g::DIM_W; ++m) {
        for (int i = 1; i < g::DIM_X; ++i) {
            bool used_value = false;
            int promise_pos = m * g::DIM_X + i;
            int value = src ? src->get()[promise_pos][omp_get_thread_num()].get_future().get() : -1;
            int last_j = -1;
            #pragma omp for schedule(static) nowait
            for (int j = 1; j < g::DIM_Y; ++j) {
                if (!used_value && src) {
                    printf("[Thread %d] Accessing (%d, %d, %d, %d)\n", omp_get_thread_num(), m, i, j - 1, k);
                }

                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);
                size_t nm1k = to1d(m, i, j, k - 1);

                int orig = ptr[n];
                int to_add = (used_value || !src ? ptr[nm1j] : value) + ptr[nm1] + ptr[nm1k];

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
                last_j = j;
            }

            if (dst && last_j != -1) {
                size_t pos = to1d(m, i, last_j, k);
                printf("[Thread %d] Setting value for promise %d (%d, %d, %d, %d)\n", omp_get_thread_num(), promise_pos, m, i, last_j, k);
                dst->get()[promise_pos][omp_get_thread_num() + 1].set_value(ptr[pos]);
            }
        }
    }
}