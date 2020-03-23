#include <cassert>

#include <thread>

#include <omp.h>

#include "config.h"
#include "functions.h"
#include "utils.h"

void heat_cpu(Matrix array, size_t m) {
    namespace g = Globals;

    int thread_num = omp_get_thread_num();
    int omp_num_threads = omp_get_num_threads();
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
void heat_cpu_switch_loops(Matrix array, size_t m) {
    namespace g = Globals;

    int thread_num = omp_get_thread_num();
    int omp_num_threads = omp_get_num_threads();
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

namespace g = Globals;

void heat_cpu_line_promise(Matrix array, size_t m, 
                           std::optional<std::reference_wrapper<std::array<std::vector<std::promise<MatrixValue>>, g::NB_LINES_PER_ITERATION>>>& dst,
                           const std::optional<std::reference_wrapper<std::array<std::vector<std::promise<MatrixValue>>, g::NB_LINES_PER_ITERATION>>>& src) {
    namespace g = Globals;

    int thread_num = omp_get_thread_num();
    int omp_num_threads = omp_get_num_threads();
    int* ptr = reinterpret_cast<int*>(array);

    for (int j = 1; j < g::DIM_Y; ++j) {
        for (int k = 0; k < g::DIM_Z; ++k) {
            bool used_value = false;
            int promise_pos = j * g::DIM_Z + k;
            int last_i = -1;
            
            #pragma omp for schedule(static) nowait
            for (int i = 1; i < g::DIM_X; ++i) {
                if (!used_value && src) {
                    printf("[Thread %d] Accessing (%d, %d, %d, %d)\n", omp_get_thread_num(), m, i - 1, j, k);
                }

                size_t n = to1d(m, i, j, k);
                size_t nm1 = to1d(m, i - 1, j, k);
                size_t nm1j = to1d(m, i, j - 1, k);
                size_t nm1m = to1d(m - 1, i, j, k);

                int orig = ptr[n];
                int to_add = (used_value || !src ? ptr[nm1] : src->get()[promise_pos][omp_get_thread_num()].get_future().get()) + ptr[nm1j] + ptr[nm1m];

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
                size_t pos = to1d(m, last_i, j, k);
                printf("[Thread %d] Setting value for promise %d (%d, %d, %d, %d)\n", omp_get_thread_num(), promise_pos, m, last_i, j, k);
                dst->get()[promise_pos][omp_get_thread_num() + 1].set_value(ptr[pos]);
            }
        }
    }
}