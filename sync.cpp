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

#include "spdlog/spdlog.h"

#include "config.h"
#include "defines.h"
#include "functions.h"
#include "logging.h"
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

        for (int m = 1; m < g::ITERATIONS; ++m) {
            heat_cpu(matrix, m);
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

        #pragma omp barrier

        for (int m = 1; m < g::ITERATIONS; m++) {
            sync_left(thread_num, n_threads - 1, m);

            f(_matrix, std::forward<Args>(args)..., m);

            sync_right(thread_num, n_threads - 1, m);
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

        #pragma omp barrier

        for (int m = 1; m < g::ITERATIONS; ++m) {
            sync_left(thread_num, n_threads - 1, m);

            f(_matrix, std::forward<Args>(args)..., m);

            sync_right(thread_num, n_threads - 1, m);
        }
    }
    }
    
private:
    void sync_init() {
        for (int i = 0; i < _isync.size(); ++i) {
            _isync[i].store(1, std::memory_order_acq_rel);
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

template<typename T>
class IterationPromisingSynchronizer : public Synchronizer {
public:
    IterationPromisingSynchronizer(int n) : Synchronizer() {
        for (auto& w: _promises_store) {
            w.resize(n);
        }
    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args) {
        #pragma omp parallel
        {
            for (int m = 1; m < g::ITERATIONS; ++m) {
                auto src_store = omp_get_thread_num() != 0 ? std::make_optional(std::ref(_promises_store[m])) : std::nullopt;
                auto dst_store = omp_get_thread_num() != omp_get_num_threads() - 1 ? std::make_optional(std::ref(_promises_store[m])) : std::nullopt;

                f(_matrix, std::forward<Args>(args)..., m, dst_store, src_store);
            }
        }
    }

protected:
    std::array<T, g::ITERATIONS> _promises_store;
};

using LinePromisingSynchronizer = IterationPromisingSynchronizer<LinePromiseContainer>;
using BlockPromisingSynchronizer = IterationPromisingSynchronizer<BlockPromiseContainer>;

class IncreasingLinePromisingSynchronizer : public IterationPromisingSynchronizer<IncreasingLinePromiseContainer> {
public:
    IncreasingLinePromisingSynchronizer(int n) : IterationPromisingSynchronizer<IncreasingLinePromiseContainer>(n) {
        for (int i = 1; i < _promises_store.size(); ++i) {
            IncreasingLinePromiseContainer& container = _promises_store[i];
            int nb_elements_per_vector = pow(4, i - 1);
            int nb_vectors = g::NB_LINES_PER_ITERATION / nb_elements_per_vector;

            if (nb_vectors * nb_elements_per_vector < g::NB_LINES_PER_ITERATION) {
                ++nb_vectors;
                assert(nb_vectors * nb_elements_per_vector >= g::NB_LINES_PER_ITERATION);
            }

            for (int j = 0; j < container.size(); j++) {
                container[j].resize(nb_vectors);
            }
        }
    }
};

/* class LinePromisingSynchronizer : public Synchronizer {
public:
    LinePromisingSynchronizer(int n) : Synchronizer() {
        for (auto& w: _promises_store) {
            w.resize(n);
        }
    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args) {
        #pragma omp parallel
        {
            for (int m = 1; m < g::ITERATIONS; ++m) {
                auto src_store = omp_get_thread_num() != 0 ? std::make_optional(std::ref(_promises_store[m])) : std::nullopt;
                auto dst_store = omp_get_thread_num() != omp_get_num_threads() - 1 ? std::make_optional(std::ref(_promises_store[m])) : std::nullopt;

                f(_matrix, std::forward<Args>(args)..., m, dst_store, src_store);
            }
        }
    }

private:
    std::array<LinePromiseContainer, Globals::ITERATIONS> _promises_store;
};

class BlockPromisingSynchronizer : public Synchronizer {
public:
    BlockPromisingSynchronizer(int n) : Synchronizer() {
        for (auto& w: _promises_store) {
            w.resize(n);
        }
    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args)
    {
        #pragma omp parallel
        {
            for (int m = 1; m < g::ITERATIONS; ++m) {
                auto src_store = omp_get_thread_num() != 0 ? std::make_optional(std::ref(_promises_store[m])) : std::nullopt;
                auto dst_store = omp_get_thread_num() != omp_get_num_threads() - 1 ? std::make_optional(std::ref(_promises_store[m])) : std::nullopt;

                f(_matrix, std::forward<Args>(args)..., m, dst_store, src_store);
            }
        }
    }

private:
    std::array<BlockPromiseContainer, Globals::ITERATIONS> _promises_store;
};

class IncreasingLinePromisingSynchronizer : public Synchronizer {
public:
    IncreasingLinePromisingSynchronizer(int n) : Synchronizer() {
        for (auto& w: _promises_store) {
            w.resize(n);
        }
    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args)
    {
        #pragma omp parallel
        {
            for (int m = 1; m < g::ITERATIONS; ++m) {
                auto src_store = omp_get_thread_num() != 0 ? std::make_optional(std::ref(_promises_store[m])) : std::nullopt;
                auto dst_store = omp_get_thread_num() != omp_get_num_threads() - 1 ? std::make_optional(std::ref(_promises_store[m])) : std::nullopt;

                f(_matrix, std::forward<Args>(args)..., m, dst_store, src_store);
            }
        }
    }

private:
    std::array<IncreasingLinePromiseContainer, Globals::ITERATIONS> _promises_store;
}; */

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

    init_logging();

    omp_debug();

    SynchronizationMeasurer<AltBitSynchronizer>::measure_time(std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2), "heat_cpu with alt bit", 20);
    SynchronizationMeasurer<IterationSynchronizer>::measure_time(std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2), "heat_cpu with counter", 20);

    SynchronizationMeasurer<AltBitSynchronizer>::measure_time(std::bind(heat_cpu_switch_loops, std::placeholders::_1, std::placeholders::_2), "heat_cpu_switch_loops with alt bit", 20);
    SynchronizationMeasurer<IterationSynchronizer>::measure_time(std::bind(heat_cpu_switch_loops, std::placeholders::_1, std::placeholders::_2), "heat_cpu_switch_loops with counter", 20);
    
    SynchronizationMeasurer<LinePromisingSynchronizer>::measure_time(std::bind(heat_cpu_line_promise, 
                                                                               std::placeholders::_1,
                                                                               std::placeholders::_2,
                                                                               std::placeholders::_3,
                                                                               std::placeholders::_4),
                                                                     "heat_cpu_line_promise with PromisingIterationSynchronizer", 20);

    SynchronizationMeasurer<BlockPromisingSynchronizer>::measure_time(std::bind(heat_cpu_block_promise, 
                                                                                std::placeholders::_1,
                                                                                std::placeholders::_2,
                                                                                std::placeholders::_3,
                                                                                std::placeholders::_4),
                                                                      "heat_cpu_block_promise with BlockPromisingSynchronizer", 20);

    spdlog::get(Loggers::Names::global_logger)->info("Ending");
    return 0;
}
