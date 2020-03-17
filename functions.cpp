#include <thread>

#include <omp.h>

#include "config.h"
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

// In this version we switch the i and j loops, in order to complete a line before
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
        #pragma omp for schedule(static) nowait
        for (int i = 1; i < g::DIM_X - 1; ++i) {
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