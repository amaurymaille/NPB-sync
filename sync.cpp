#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <tuple>

#include <omp.h>

namespace Globals {
    static const size_t DIM_X = 128;
    static const size_t DIM_Y = 128;
    static const size_t DIM_Z = 16;
    static const size_t NB_ELEMENTS = DIM_X * DIM_Y * DIM_Z;

    static const size_t ZONE_X_SIZE = 32;
    static const size_t ZONE_Y_SIZE = 32;
    static const size_t ZONE_Z_SIZE = ::Globals::DIM_Z;

    static const size_t ITERATIONS = 10;
}

size_t to1d(size_t x, size_t y, size_t z) {
    namespace g = Globals;
    return x * g::DIM_Y * g::DIM_Z + y * g::DIM_Z + z;
}

std::tuple<size_t, size_t, size_t> to3d(size_t n) {
    namespace g = Globals;
    size_t z = n % g::DIM_Z;
    size_t y = ((n - z) / g::DIM_Z) % g::DIM_Y;
    size_t x = (n - z - y * g::DIM_Z) / (g::DIM_Y * g::DIM_Z);

    return std::make_tuple(x, y, z);
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
                size_t as1d = to1d(i, j, k);
                auto [ci, cj, ck] = to3d(as1d);

                assert(matrix[i][j][k] == to1d(i, j, k));
                assert(ci == i && cj == j && ck == k);
            }
        }
    }
}

std::string get_time_fmt(const char* fmt) {
    auto now = std::chrono::system_clock::now();
    std::time_t as_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* now_as_tm = std::gmtime(&as_time_t);

    std::unique_ptr<char[]> res(new char[100]);
    std::size_t size = std::strftime(res.get(), 100, fmt, now_as_tm);

    std::string result(res.get());
    return result;
}

const char* get_time_fmt_cstr(const char* fmt) {
    return get_time_fmt(fmt).c_str();
}

const char* get_time_default_fmt() {
    return get_time_fmt_cstr("%H:%M:%S");
}

class AltBitSynchronizer {
public:
    AltBitSynchronizer(int nthreads) : _isync(nthreads) {

    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args) {
        namespace g = Globals;

        int thread_num = -1, n_threads = -1;
    #pragma omp parallel private(thread_num, n_threads)
    {
        thread_num = omp_get_thread_num();
        n_threads = omp_get_num_threads();

        #pragma omp master
        {
            printf("[run] Running with %d threads\n", n_threads);
        }

        #pragma omp master
        {
        sync_init();
        }

        #pragma omp barrier

        for (int i = 0; i < g::ITERATIONS; ++i) {
            // std::this_thread::sleep_for(std::chrono::seconds(rand() % 5));

            sync_left(thread_num, n_threads - 1, i);

            // std::this_thread::sleep_for(std::chrono::seconds(rand() % 5));

            f(std::forward<Args>(args)..., i);

            // std::this_thread::sleep_for(std::chrono::seconds(rand() % 5));

            sync_right(thread_num, n_threads - 1, i);
        }
    }
    }

protected:
    void sync_init() {
        for (int i = 0; i < _isync.size(); ++i) {
            _isync[i].store(false, std::memory_order_acq_rel);
        }
    }

    void sync_left(int thread_num, int n_threads, int i) {
        if (thread_num > 0 && thread_num <= n_threads) {
            printf("[%s][sync_left] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, i);
            int neighbour = thread_num - 1;
            bool sync_state = _isync[neighbour].load(std::memory_order_acq_rel);

            while (sync_state == false)
                sync_state = _isync[neighbour].load(std::memory_order_acq_rel);

            printf("[%s][sync_left] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, i);   
            _isync[neighbour].store(false, std::memory_order_acq_rel);
        }
        // FLUSH ?
    }

    void sync_right(int thread_num, int n_threads, int i) {
        // FLUSH ?

        if (thread_num < n_threads) {
            printf("[%s][sync_right] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, i);
            bool sync_state = _isync[thread_num].load(std::memory_order_acq_rel);

            while (sync_state == true)
                sync_state = _isync[thread_num].load(std::memory_order_acq_rel);

            printf("[%s][sync_right] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, i);
            _isync[thread_num].store(true, std::memory_order_acq_rel);
        }
    }

    std::vector<std::atomic<bool>> _isync;
};

void heat_cpu(int* ptr, int global_thread_num, int extern_i) {
    namespace g = Globals;

    int thread_num = omp_get_thread_num();
    printf("[%s][heat_cpu] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, extern_i);

    #pragma omp for schedule(static) nowait
    for (int i = 1; i < g::DIM_X - 1; ++i) {
        for (int j = 1; j < g::DIM_Y; j++) {
            for (int k = 0; k < g::DIM_Z; k++) {
                size_t n = to1d(i, j, k);
                size_t nm1 = to1d(i - 1, j, k);

                ptr[n] += ptr[n - 1];
            }
        }
    }

    printf("[%s][heat_cpu] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, extern_i);
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

    srand((unsigned)time(nullptr));

    omp_debug();

    int matrix[g::DIM_X][g::DIM_Y][g::DIM_Z]; 
    init_matrix(reinterpret_cast<int*>(matrix));

    int matrix2[g::DIM_X][g::DIM_Y][g::DIM_Z];
    memcpy(matrix2, matrix, g::NB_ELEMENTS * sizeof(int));
    for (int i = 0; i < g::ITERATIONS; i++) {
        heat_cpu(reinterpret_cast<int*>(matrix2), -1, i);
    }

    assert_okay_init(matrix);

    AltBitSynchronizer synchronizer(100);

#pragma omp parallel
{
    printf("[main] I am thread %d, there are %d threads\n", omp_get_thread_num(), omp_get_num_threads());
    #pragma omp barrier
    synchronizer.run(&heat_cpu, reinterpret_cast<int*>(matrix), omp_get_thread_num());
}

    for (int n = 0; n < g::NB_ELEMENTS; ++n) {
        auto [i, j, k] = to3d(n);
        assert(matrix[i][j][k] == matrix2[i][j][k]);
    }

    return 0;
}
