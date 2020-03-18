#include <cassert>
#include <cstdio>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <utility>

#include <omp.h>

#include "config.h"
#include "defines.h"
#include "functions.h"
#include "utils.h"

class Synchronizer {
protected:
    Synchronizer() {
        init_matrix(reinterpret_cast<int*>(_matrix));
        assert_okay_init(_matrix);
    }
    
public:
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

class PromisingIterationSynchronizer : public Synchronizer {
public:
    PromisingIterationSynchronizer(int n) : Synchronizer() {
        for (auto& w: _promises_store) {
            for (auto& jk: w) {
                jk.resize(n);
            }
        }
    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args) {
        #pragma omp parallel
        {
            for (int i = 0; i < g::DIM_W; ++i) {
                auto src_store = omp_get_thread_num() != 0 ? std::make_optional(std::ref(_promises_store[i])) : std::nullopt;
                auto dst_store = omp_get_thread_num() != omp_get_num_threads() - 1 ? std::make_optional(std::ref(_promises_store[i])) : std::nullopt;

                f(_matrix, std::forward<Args>(args)..., i, dst_store, src_store);
            }
        }
    }

private:
    /* There are DIM_W * DIM_Y * DIM_Z lines. We need DIM_W arrays of DIM_Y * DIM_Z
     * promises. Use separate arrays so we can more easily distribute them to the 
     * heat_cpu function.
     */
    std::array<std::array<std::vector<std::promise<MatrixValue>>, Globals::DIM_Y * Globals::DIM_Z>, Globals::DIM_W> _promises_store;
};

template<class Synchronizer>
class SynchronizationMeasurer {
public:
    template<class F, class... SynchronizerArgs>
    static void measure_time(F&& f, std::string const& simulation_name, SynchronizerArgs&&... synchronizer_args) {
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
                std::cout << "[" << simulation_name << "] " << "Elapsed time : " << 
                             diff.count() << ", " << count_duration_cast<std::chrono::milliseconds>(diff) << 
                             ", " << count_duration_cast<std::chrono::microseconds>(diff) << 
                             std::endl;
            }
        }

        synchronizer.assert_okay();
    }
};

int main() {
    namespace g = Globals;

    srand((unsigned)time(nullptr));

    omp_debug();

    assert_switch_and_no_switch_are_identical();

    SynchronizationMeasurer<AltBitSynchronizer>::measure_time(std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2), "heat_cpu with alt bit", 20);
    SynchronizationMeasurer<IterationSynchronizer>::measure_time(std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2), "heat_cpu with counter", 20);

    SynchronizationMeasurer<AltBitSynchronizer>::measure_time(std::bind(heat_cpu_switch_loops, std::placeholders::_1, std::placeholders::_2), "heat_cpu_switch_loops with alt bit", 20);
    SynchronizationMeasurer<IterationSynchronizer>::measure_time(std::bind(heat_cpu_switch_loops, std::placeholders::_1, std::placeholders::_2), "heat_cpu_switch_loops with counter", 20);
    
    SynchronizationMeasurer<PromisingIterationSynchronizer>::measure_time(std::bind(heat_cpu_switch_loops_promise, 
                                                                                    std::placeholders::_1,
                                                                                    std::placeholders::_2,
                                                                                    std::placeholders::_3,
                                                                                    std::placeholders::_4),
                                                                          "heat_cpu_switch_loops_promise with PromisingIterationSynchronizer", 20);
    return 0;
}
