#include <cassert>
#include <cmath>

#include <thread>

#include <omp.h>

#include "config.h"
#include "heat_cpu.h"
#include "matrix_core.h"
#include "naive_promise.h"
#include "promise_plus.h"
#include "utils.h"

void heat_cpu(Matrix& array, size_t m) {
    namespace g = Globals;

    #pragma omp for schedule(static) nowait
    for (int j = 1; j < g::HeatCPU::DIM_Y; ++j) {
        for (int i = 1; i < g::HeatCPU::DIM_X; ++i) {
            for (int k = 0; k < g::HeatCPU::DIM_Z; ++k) {
                update_matrix(array, m, i, j, k);
            }
        }
    }
}

namespace g = Globals;

void heat_cpu_promise_plus(Matrix& array, size_t m, PromisePlusStore& dst, const PromisePlusStore& src) {
    namespace g = Globals;
    
    int thread_num = omp_get_thread_num();

    for (int j = 1; j < g::HeatCPU::DIM_Y; ++j) {
        if (src) {
            (*src)[thread_num]->get(j);
        }

        #pragma omp for schedule(static) nowait
        for (int i = 1; i < g::HeatCPU::DIM_X; ++i) {
            for (int k = 0; k < g::HeatCPU::DIM_Z; ++k) {
                update_matrix(array, m, i, j, k);
            }
        }

        if (dst) {
            (*dst)[thread_num + 1]->set(j);
        }
    }

    if (dst) {
        (*dst)[thread_num + 1]->set_immediate(g::HeatCPU::DIM_Y);
    }
}

void heat_cpu_array_of_promises(Matrix& array, size_t m, 
                                ArrayOfPromisesStore& dst, 
                                ArrayOfPromisesStore& src) {
    namespace g = Globals;

    int thread_num = omp_get_thread_num();

    for (int j = 1; j < g::HeatCPU::DIM_Y; ++j) {
        if (src) {
            (*src)[thread_num][j].get();
        }

        #pragma omp for schedule(static) nowait
        for (int i = 1; i < g::HeatCPU::DIM_X; ++i) {
            for (int k = 0; k < g::HeatCPU::DIM_Z; ++k) {
                update_matrix(array, m, i, j, k);
            }
        }

        if (dst) {
            (*dst)[thread_num + 1][j].set();
        }
    }
}

void heat_cpu_promise_of_array(Matrix& array, size_t m, 
                               PromiseOfArrayStore& dst, 
                               PromiseOfArrayStore& src) {
    namespace g = Globals;

    int thread_num = omp_get_thread_num();

    if (src) {
        (*src)[thread_num]->get();
    }

    for (int j = 1; j < g::HeatCPU::DIM_Y; ++j) {
        #pragma omp for schedule(static) nowait
        for (int i = 1; i < g::HeatCPU::DIM_X; ++i) {
            for (int k = 0; k < g::HeatCPU::DIM_Z; ++k) {
                update_matrix(array, m, i, j, k);
            }
        }

    }

    if (dst) {
        (*dst)[thread_num + 1]->set();
    }
  
}
