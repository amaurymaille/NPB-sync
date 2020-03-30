#include <cassert>
#include <cstdio>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <utility>

#include <sys/time.h>

#include <omp.h>

#include "measure-time.h"
#include "spdlog/spdlog.h"

#include "config.h"
#include "defines.h"
#include "functions.h"
#include "logging.h"
#include "utils.h"

using Clock = std::chrono::system_clock;

Matrix g_expected_matrix;
Matrix g_start_matrix;

class Synchronizer {
protected:
    Synchronizer() {
        // init_matrix(reinterpret_cast<int*>(_matrix));
        init_from_start_matrix(_matrix);
        assert_okay_init(_matrix);
    }
    
public:
    void assert_okay() {
        namespace g = Globals;
        assert_matrix_equals(g_expected_matrix, _matrix);
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
            // printf("Running with %d threads\n", n_threads);
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
            // printf("[%s][sync_left] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, i);
            int neighbour = thread_num - 1;
            bool sync_state = _isync[neighbour].load(std::memory_order_acq_rel);

            while (sync_state == false)
                sync_state = _isync[neighbour].load(std::memory_order_acq_rel);

            _isync[neighbour].store(false, std::memory_order_acq_rel);
            // printf("[%s][sync_left] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, i);
        }
    }

    void sync_right(int thread_num, int n_threads, int i) {
        namespace g = Globals;

        if (thread_num < n_threads) {
            // printf("[%s][sync_right] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, i);
            
            bool sync_state = _isync[thread_num].load(std::memory_order_acq_rel);
            while (sync_state == true)
                sync_state = _isync[thread_num].load(std::memory_order_acq_rel);

            _isync[thread_num].store(true, std::memory_order_acq_rel);

            // printf("[%s][sync_right] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, i);
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
            // printf("[run] Running with %d threads\n", n_threads);
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
            // printf("[%s][sync_left] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, i);
            int neighbour = thread_num - 1;
            unsigned int sync_state = _isync[neighbour].load(std::memory_order_acq_rel);

            // Wait for left neighbour to have finished its iteration
            while (sync_state == i)
                sync_state = _isync[neighbour].load(std::memory_order_acq_rel);

            // printf("[%s][sync_left] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, i);
        }
    }

    void sync_right(int thread_num, int n_threads, int i) {
        namespace g = Globals;

        if (thread_num < n_threads) {
            // printf("[%s][sync_right] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, i);
            _isync[thread_num]++;
            // printf("[%s][sync_right] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, i);
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
            int nb_elements_per_vector = i < g::INCREASING_LINES_ITERATION_LIMIT ? std::pow(g::INCREASING_LINES_BASE_POWER, i - 1) : g::NB_LINES_PER_ITERATION;
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

template<class Synchronizer>
class SynchronizationMeasurer {
public:
    template<class F, class... SynchronizerArgs>
    static /* std::tuple<uint64, uint64, uint64> */ uint64 measure_time(F&& f, SynchronizerArgs&&... synchronizer_args) {
        struct timespec begin, end;
	// struct timespec init_begin, init_end;
	// struct timespec assert_begin, assert_end;

	// clock_gettime(CLOCK_MONOTONIC, &init_begin);
        Synchronizer synchronizer(synchronizer_args...);
	// clock_gettime(CLOCK_MONOTONIC, &init_end);

        clock_gettime(CLOCK_MONOTONIC, &begin);
        synchronizer.run(f);
        clock_gettime(CLOCK_MONOTONIC, &end);

	// clock_gettime(CLOCK_MONOTONIC, &assert_begin);
        synchronizer.assert_okay();
	// clock_gettime(CLOCK_MONOTONIC, &assert_end);

        uint64 diff = clock_diff(&end, &begin);
	// uint64 init_diff = clock_diff(&init_end, &init_begin);
	// uint64 assert_diff = clock_diff(&assert_end, &assert_begin);

        // return std::make_tuple(init_diff, diff, assert_diff);
	return diff;
    }
};

class SynchronizationTimeCollector {
public:
    template<typename Synchronizer>
    class Collector {
    public:
        template<typename F, typename... SynchronizerArgs>
        static void collect(std::string const& name, F&& f, SynchronizerArgs&&... args) {
            uint64 /* init_diff = 0, diff = 0, assert_diff = 0 */ diff = 0;
	    // struct timespec begin, end;

	    // clock_gettime(CLOCK_MONOTONIC, &begin);
            for (int i = 0; i < 10000; ++i) {
                // auto [init_time, compute_time, assert_time] = SynchronizationMeasurer<Synchronizer>::measure_time(std::forward<F>(f), std::forward<SynchronizerArgs>(args)...);
		// init_diff += init_time;
		// diff += compute_time;
		// assert_diff += assert_time;
		diff += SynchronizationMeasurer<Synchronizer>::measure_time(std::forward<F>(f), std::forward<SynchronizerArgs>(args)...);
            }
	    // clock_gettime(CLOCK_MONOTONIC, &end);
	    
	    // uint64 global_diff = clock_diff(&end, &begin);

	    // lldiv_t global_d = lldiv(global_diff, BILLION);
	    // lldiv_t init_d = lldiv(init_diff, BILLION);
	    // lldiv_t assert_d = lldiv(assert_diff, BILLION);
            lldiv_t d = lldiv(diff, BILLION);

            std::cout << "Simulation " << name << " took " << d.quot << ":" << d.rem << " seconds (" << diff << ")" << std::endl;
	    // std::cout << "Initialization took " << init_d.quot << ":" << init_d.rem << " seconds (" << init_diff << ")" << std::endl;
	    // std::cout << "Assertion took " << assert_d.quot << ":" << assert_d.rem << " seconds (" << assert_diff << ")" << std::endl;
	    // std::cout << "Globally, it took " << global_d.quot << ":" << global_d.rem << " seconds (" << global_diff << ")" << std::endl;
	    // std::cout << std::endl;
        }
    };

    static void collect_all() {
        Collector<AltBitSynchronizer>::collect("heat_cpu with AltBitSynchronizer", std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2), 20);
        Collector<IterationSynchronizer>::collect("heat_cpu with IterationSynchronizer", std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2), 20);
        Collector<AltBitSynchronizer>::collect("heat_cpu_switch_loops with AltBitSynchronizer", std::bind(heat_cpu_switch_loops, std::placeholders::_1, std::placeholders::_2), 20);
        Collector<IterationSynchronizer>::collect("heat_cpu_switch_loops with IterationSynchronizer", std::bind(heat_cpu_switch_loops, std::placeholders::_1, std::placeholders::_2), 20);

        Collector<LinePromisingSynchronizer>::collect("LinePromisingSynchronizer",
                                                      std::bind(heat_cpu_line_promise, 
                                                                std::placeholders::_1,
                                                                std::placeholders::_2,
                                                                std::placeholders::_3,
                                                                std::placeholders::_4),
                                                      20);

        Collector<BlockPromisingSynchronizer>::collect("BlockPromisingSynchronizer",
                                                       std::bind(heat_cpu_block_promise, 
                                                                 std::placeholders::_1,
                                                                 std::placeholders::_2,
                                                                 std::placeholders::_3,
                                                                 std::placeholders::_4),
                                                       20);

        Collector<IncreasingLinePromisingSynchronizer>::collect("IncreasingLinePromisingSynchronizer",
                                                                std::bind(heat_cpu_increasing_line_promise,
                                                                          std::placeholders::_1,
                                                                          std::placeholders::_2,
                                                                          std::placeholders::_3,
                                                                          std::placeholders::_4),
                                                                20);
    }
};

template<typename T>
using Collector = SynchronizationTimeCollector::Collector<T>;

int main() {
    namespace g = Globals;

    srand((unsigned)time(nullptr));

    init_logging();

    spdlog::get(Loggers::Names::global_logger)->info("Starting");

    init_start_matrix_once();
    init_from_start_matrix(g_expected_matrix);
    assert_matrix_equals(g_start_matrix, g_expected_matrix);
    init_expected_matrix_once();

    // SynchronizationTimeCollector::collect_all();
    for (int i = 0; i < 10; ++i) {
        SynchronizationTimeCollector::Collector<IterationSynchronizer>::collect("Iteration", std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2), 20);
        SynchronizationTimeCollector::Collector<BlockPromisingSynchronizer>::collect("Block", std::bind(heat_cpu_block_promise, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4), 20);
    }

    spdlog::get(Loggers::Names::global_logger)->info("Ending");
    return 0;
}
