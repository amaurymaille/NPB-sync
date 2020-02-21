#include <cassert>
#include <cstdio>

#include <atomic>
#include <functional>
#include <iostream>

#include <omp.h>

namespace Globals {
    static const size_t DIM_X = 128;
    static const size_t DIM_Y = 128;
    static const size_t DIM_Z = 16;
    static const size_t NB_ELEMENTS = DIM_X * DIM_Y * DIM_Z;

    static const size_t ZONE_X_SIZE = 32;
    static const size_t ZONE_Y_SIZE = 32;
    static const size_t ZONE_Z_SIZE = ::Globals::DIM_Z;
}

size_t to1d(size_t x, size_t y, size_t z) {
    namespace g = Globals;
    return x * g::DIM_Y * g::DIM_Z + y * g::DIM_Z + z;
}

void init_matrix(int* ptr) {
    namespace g = Globals;
    for (int i = 0; i < g::NB_ELEMENTS; ++i) {
        ptr[i] = i;
    }
}

void assert_okay_init(int matrix[Globals::DIM_X][Globals::DIM_Y][Globals::DIM_Z]) {
    namespace g = Globals;

    for (int i = 0; i < g::DIM_X; ++i) {
        for (int j = 0; j < g::DIM_Y; ++j) {
            for (int k = 0; k < g::DIM_Z; k++) {
                assert(matrix[i][j][k] == to1d(i, j, k));
            }
        }
    }
}

class AltBitSynchronizer {
public:
    AltBitSynchronizer(int nthreads) : _isync(nthreads) {

    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args) {
        int thread_num = -1, n_threads = -1;
    #pragma omp parallel private(thread_num, n_threads)
    {
        thread_num = omp_get_thread_num();
        n_threads = omp_get_num_threads();

        #pragma omp master
        {
            printf("[run] Running with %d threads\n", n_threads);
        }

        printf("[run] I am thread %d, there are %d threads\n", thread_num, n_threads);

        #pragma omp master
        {
        sync_init();
        }

        #pragma omp barrier

        sync_left(thread_num, n_threads - 1);

        f(args...);

        sync_right(thread_num, n_threads - 1);
    }
    }

protected:
    void sync_init() {
        for (int i = 0; i < _isync.size(); ++i) {
            _isync[i].store(false, std::memory_order_acq_rel);
        }
    }

    void sync_left(int thread_num, int n_threads) {
        printf ("[sync_left] thread %d, %d threads\n", thread_num, n_threads);
        if (thread_num > 0 && thread_num <= n_threads) {
            int neighbour = thread_num - 1;
            bool sync_state = _isync[neighbour].load(std::memory_order_acq_rel);

            while (sync_state == false)
                sync_state = _isync[neighbour].load(std::memory_order_acq_rel);
            
            _isync[neighbour].store(false, std::memory_order_acq_rel);
        }
        // FLUSH ?
    }

    void sync_right(int thread_num, int n_threads) {
        // FLUSH ?

        printf ("[sync_right] thread %d, %d threads\n", thread_num, n_threads);
        if (thread_num < n_threads) {
            bool sync_state = _isync[thread_num].load(std::memory_order_acq_rel);

            while (sync_state == true)
                sync_state = _isync[thread_num].load(std::memory_order_acq_rel);

            _isync[thread_num].store(true, std::memory_order_acq_rel);
        }
    }

    std::vector<std::atomic<bool>> _isync;
};

void heat_cpu(int* ptr, int global_thread_num) {
    namespace g = Globals;

    int thread_num = omp_get_thread_num();
    #pragma omp for schedule(static) nowait
    for (int i = 0; i < g::NB_ELEMENTS; ++i) {
        printf("[data] %d, %d, %d\n", global_thread_num, thread_num, i);
    }
}

void omp_debug() {
    std::cout << "Number of threads (out of parallel): " << omp_get_num_threads() << std::endl;

#pragma omp parallel
{
    #pragma omp master
    {
        std::cout << "Number of threads (in parallel) : " << omp_get_num_threads() << std::endl;
    }
}

    std::cout << "Number of threads (out of parallel again) : " << omp_get_num_threads() << std::endl;
}

int main() {
    namespace g = Globals;

    omp_debug();

    int matrix[g::DIM_X][g::DIM_Y][g::DIM_Z];
    init_matrix(reinterpret_cast<int*>(matrix));
    assert_okay_init(matrix);

    AltBitSynchronizer synchronizer(100);

#pragma omp parallel
{
    printf("[main] I am thread %d, there are %d threads\n", omp_get_thread_num(), omp_get_num_threads());
    #pragma omp barrier
    synchronizer.run(&heat_cpu, reinterpret_cast<int*>(matrix), omp_get_thread_num());
}

    return 0;
}
