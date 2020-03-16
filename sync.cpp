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
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <type_traits>

#include <omp.h>

#include "config.h"

template<typename IntType>
class RandomGenerator {
public:
    template<typename... Args>
    RandomGenerator(Args&&... args) : _generator(std::random_device()()), _distribution(std::forward<Args>(args)...) {

    }

    IntType operator()() {
        return _distribution(_generator);
    }

private:
    std::mt19937 _generator;
    std::uniform_int_distribution<IntType> _distribution;
};

namespace Globals {
    static const size_t DIM_W = 10;
    static const size_t DIM_X = 8;
    static const size_t DIM_Y = 8;
    static const size_t DIM_Z = 8;
    static const size_t NB_ELEMENTS = DIM_W * DIM_X * DIM_Y * DIM_Z;

    static const size_t ZONE_X_SIZE = 32;
    static const size_t ZONE_Y_SIZE = 32;
    static const size_t ZONE_Z_SIZE = ::Globals::DIM_Z;

    static const size_t ITERATIONS = DIM_W;
    static RandomGenerator<unsigned int> generator(0, 100);
    static RandomGenerator<unsigned char> binary_generator(0, 1);
}

template<typename T, typename R>
auto count_duration_cast(std::chrono::duration<R> const& tp) {
    return std::chrono::duration_cast<T>(tp).count();
}

typedef int Matrix[Globals::DIM_W][Globals::DIM_X][Globals::DIM_Y][Globals::DIM_Z];

static void heat_cpu(Matrix, size_t);

size_t to1d(size_t w, size_t x, size_t y, size_t z) {
    namespace g = Globals;
    return w * g::DIM_X * g::DIM_Y  * g::DIM_Z +
           x            * g::DIM_Y  * g::DIM_Z + 
           y                        * g::DIM_Z +
           z;
}

std::tuple<size_t, size_t, size_t, size_t> to4d(size_t n) {
    namespace g = Globals;
    size_t z = n % g::DIM_Z;
    size_t y = ((n - z) / g::DIM_Z) % g::DIM_Y;
    size_t x = ((n - z - y * g::DIM_Z) / (g::DIM_Y * g::DIM_Z)) % g::DIM_X;
    size_t w = (n - z - y * g::DIM_Z - x * g::DIM_Y * g::DIM_Z) / (g::DIM_X * g::DIM_Y * g::DIM_Z);

    return std::make_tuple(w, x, y, z);
}

void init_matrix(int* ptr) {
    namespace g = Globals;
    for (int i = 0; i < g::NB_ELEMENTS; ++i) {
        ptr[i] = i % 10;
    }
}

void assert_okay_init(Matrix matrix) {
    namespace g = Globals;

    for (int i = 0; i < g::DIM_W; ++i) {
        for (int j = 0; j < g::DIM_X; ++j) {
            for (int k = 0; k < g::DIM_Y; k++) {
                for (int l = 0; l < g::DIM_Z; l++) {
                    size_t as1d = to1d(i, j, k, l);
                    auto [ci, cj, ck, cl] = to4d(as1d);

                    assert(matrix[i][j][k][l] == to1d(i, j, k, l) % 10);
                    assert(ci == i && cj == j && ck == k && cl == l);
                }
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

class Synchronizer {
public:
    Synchronizer() {
        init_matrix(reinterpret_cast<int*>(_matrix));
        assert_okay_init(_matrix);
    }
    
    void assert_okay() {
        namespace g = Globals;
        Matrix matrix;
        init_matrix(reinterpret_cast<int*>(matrix));

        for (int i = 0; i < g::ITERATIONS; ++i) {
            heat_cpu(matrix, i);
        }

        for (int i = 0; i < g::DIM_W; ++i) {
            for (int j = 0; j < g::DIM_X; ++j) {
                for (int k = 0;  k < g::DIM_Y; ++k) {
                    for (int l = 0; l < g::DIM_Z; ++l) {
                        if (matrix[i][j][k][l] != _matrix[i][j][k][l]) {
                            printf("Error: %d, %d, %d, %d (%lu) => expected %d, got %d\n", i, j, k, l, to1d(i, j, k, l), matrix[i][j][k][l], _matrix[i][j][k][l]);
                            assert(false);
                        }
                    }
                }
            }
        }
    }

protected:
    virtual void sync_init() = 0;

    Matrix _matrix;
};

class AltBitSynchronizer : public Synchronizer {
public:
    AltBitSynchronizer(int nthreads) : Synchronizer(), _isync(nthreads) {

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
            printf("Running with %d threads\n", n_threads);
            sync_init();
        }

        // Est-ce que cette barrière est vraiment utile ?
        #pragma omp barrier

        for (int i = 0; i < g::ITERATIONS; i++) {
            sync_left(thread_num, n_threads - 1, i);

            f(_matrix, std::forward<Args>(args)..., i);

            sync_right(thread_num, n_threads - 1, i);
        }
    }
    }

private:
    std::vector<std::atomic<bool>> _isync;

    void sync_init() {
        namespace g = Globals;

        for (int i = 0; i < _isync.size(); i++)
            _isync[i].store(false, std::memory_order_acq_rel);
    }

    void sync_left(int thread_num, int n_threads, int i) {
        namespace g = Globals;

        if (thread_num > 0 && thread_num <= n_threads) {
            printf("[%s][sync_left] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, i);
            int neighbour = thread_num - 1;
            bool sync_state = _isync[neighbour].load(std::memory_order_acq_rel);

            while (sync_state == false)
                sync_state = _isync[neighbour].load(std::memory_order_acq_rel);

            _isync[neighbour].store(false, std::memory_order_acq_rel);
            printf("[%s][sync_left] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, i);
        }
    }

    void sync_right(int thread_num, int n_threads, int i) {
        namespace g = Globals;

        if (thread_num < n_threads) {
            printf("[%s][sync_right] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, i);
            
            bool sync_state = _isync[thread_num].load(std::memory_order_acq_rel);
            while (sync_state == true)
                sync_state = _isync[thread_num].load(std::memory_order_acq_rel);

            _isync[thread_num].store(true, std::memory_order_acq_rel);

            printf("[%s][sync_right] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, i);
        }
    }
};

class IterationSynchronizer : public Synchronizer {
public:
    IterationSynchronizer(int nthreads) : Synchronizer(), _isync(nthreads) {
    
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
            sync_init();
        }

        // Est-ce que cette barrière est vraiment utile ?
        #pragma omp barrier

        for (int i = 0; i < g::ITERATIONS; ++i) {
            sync_left(thread_num, n_threads - 1, i);

            f(_matrix, std::forward<Args>(args)..., i);

            sync_right(thread_num, n_threads - 1, i);
        }
    }
    }
    
private:
    void sync_init() {
        for (int i = 0; i < _isync.size(); ++i) {
            _isync[i].store(0, std::memory_order_acq_rel);
        }
    }

    void sync_left(int thread_num, int n_threads, int i) {
        namespace g = Globals;

        if (thread_num > 0 && thread_num <= n_threads) {
            printf("[%s][sync_left] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, i);
            int neighbour = thread_num - 1;
            unsigned int sync_state = _isync[neighbour].load(std::memory_order_acq_rel);

            // Wait for left neighbour to have finished its iteration
            while (sync_state == i)
                sync_state = _isync[neighbour].load(std::memory_order_acq_rel);

            printf("[%s][sync_left] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, i);
        }
    }

    void sync_right(int thread_num, int n_threads, int i) {
        namespace g = Globals;

        if (thread_num < n_threads) {
            printf("[%s][sync_right] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, i);
            _isync[thread_num]++;
            printf("[%s][sync_right] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, i);
        }
    }

    // Utiliser un tableau d'entiers (ou double tableau de booléen pour optimiser le cache)
    // pour permettre aux threads de prendre de l'avance
    std::vector<std::atomic<unsigned int>> _isync;
};

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
                    // Sleep only in OMP section, speed up the sequential version
                    if (omp_num_threads != 1) {
                        if (g::binary_generator()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(g::generator()));
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

template<class Synchronizer>
class SynchronizationMeasurer {
public:
    template<class F, class... SynchronizerArgs>
    static void measure_time(F&& f, SynchronizerArgs&&... synchronizer_args) {
        using Clock = std::chrono::steady_clock;

        Synchronizer synchronizer(synchronizer_args...);
        std::chrono::time_point<Clock> tp;

        #pragma omp parallel
        {
            #pragma omp master
            {
                tp = Clock::now();
            }

            synchronizer.run(f);
            #pragma omp barrier

            #pragma omp master
            {
                auto now = Clock::now();
                std::chrono::duration<double> diff = now - tp;
                std::cout << "Elapsed time : " << diff.count() << ", " <<
                             count_duration_cast<std::chrono::milliseconds>(diff) << ", " << 
                             count_duration_cast<std::chrono::microseconds>(diff) << 
                             std::endl;
                ;
            }
        }

        synchronizer.assert_okay();
    }
};

int main() {
    namespace g = Globals;

    srand((unsigned)time(nullptr));

    omp_debug();

    SynchronizationMeasurer<AltBitSynchronizer>::measure_time(std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2), 20);
    SynchronizationMeasurer<IterationSynchronizer>::measure_time(std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2), 20);

    return 0;
}
