#include <cassert>
#include <cstdio>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sys/time.h>

#include <omp.h>

#include "measure-time.h"
#include "spdlog/spdlog.h"

#include "active_promise.h"
#include "config.h"
#include "defines.h"
#include "functions.h"
#include "increase.h"
#include "logging.h"
#include "promise_plus.h"
#include "utils.h"

using Clock = std::chrono::system_clock;
namespace g = Globals;

Matrix g_expected_matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z]);
Matrix g_start_matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z]);

class Synchronizer {
protected:
    Synchronizer() : _matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z]) {
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
            sync_left(thread_num, n_threads - 1);

            f(_matrix, std::forward<Args>(args)..., m);

            sync_right(thread_num, n_threads - 1);
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

    void sync_left(int thread_num, int n_threads) {
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

    void sync_right(int thread_num, int n_threads) {
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

            sync_right(thread_num, n_threads - 1);
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
            while (sync_state <= i)
                sync_state = _isync[neighbour].load(std::memory_order_acq_rel);

            // printf("[%s][sync_left] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, i);
        }
    }

    void sync_right(int thread_num, int n_threads) {
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
        _promises_store.reserve(g::ITERATIONS);

        for (int i = 0; i < g::ITERATIONS; ++i)
            _promises_store.push_back(T(n));
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
    std::vector<T> _promises_store;
};

using PointPromisingSynchronizer = IterationPromisingSynchronizer<PointPromiseContainer>;
using BlockPromisingSynchronizer = IterationPromisingSynchronizer<BlockPromiseContainer>;
using JLinePromisingSynchronizer = IterationPromisingSynchronizer<JLinePromiseContainer>;
using KLinePromisingSynchronizer = IterationPromisingSynchronizer<KLinePromiseContainer>;

template<class Store>
class IncreasingIterationPromisingSynchronizer : public IterationPromisingSynchronizer<Store> {
public:
    template<class F>
    IncreasingIterationPromisingSynchronizer(int n, F&& f, size_t MAX) : IterationPromisingSynchronizer<Store>(n) {
        for (int i = 1; i < this->_promises_store.size(); ++i) {
            Store& container = this->_promises_store[i];
            int nb_elements_for_synchronization = f(i);
            int nb_promises = MAX / nb_elements_for_synchronization;

            if (nb_promises * nb_elements_for_synchronization < MAX) {
                ++nb_promises;
                assert(nb_promises * nb_elements_for_synchronization >= MAX);
            }

            for (int j = 0; j < container.size(); j++) {
                container[j].resize(nb_promises);
            }
        }
    }
};

using IncreasingPointPromisingSynchronizer = IncreasingIterationPromisingSynchronizer<IncreasingPointPromiseContainer>;
using IncreasingJLinePromisingSynchronizer = IncreasingIterationPromisingSynchronizer<IncreasingJLinePromiseContainer>;

class BlockPromisePlusSynchronizer : public IterationPromisingSynchronizer<BlockPromisePlusContainer> {
public:
    BlockPromisePlusSynchronizer(int n_threads, int nb_blocks, PromisePlusWaitMode wait_mode = PromisePlusBase::DEFAULT_WAIT_MODE) : 
        IterationPromisingSynchronizer<BlockPromisePlusContainer>(n_threads) {
        for (BlockPromisePlusContainer& container: this->_promises_store) {
            for (auto& promise: container) {
                promise.set_wait_mode(wait_mode);
                promise.set_max_index(nb_blocks);
            }
        }
    }
};


template<class Synchronizer, class F, class... Args>
static uint64 measure_time(Synchronizer& synchronizer, F&& f, Args&&... args) {
    struct timespec begin, end;

    clock_gettime(CLOCK_MONOTONIC, &begin);
    synchronizer.run(f, std::forward<Args>(args)...);
    clock_gettime(CLOCK_MONOTONIC, &end);

    synchronizer.assert_okay();

    uint64 diff = clock_diff(&end, &begin);
    return diff;
}

class SynchronizationTimeCollector {
public:

    static void collect_all() {
        // Fuck you OpenMP
        int n_threads = -1;
        #pragma omp parallel
        {
            #pragma omp master
            {
                n_threads = omp_get_num_threads();
            }
        }

        AltBitSynchronizer altBit(n_threads);
        uint64 time = measure_time(altBit, std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2));
        SynchronizationTimeCollector::__times[std::make_pair("AltBitSynchronizer", "heat_cpu")].push_back(time);

        IterationSynchronizer iterationSync(n_threads);
        time = measure_time(iterationSync, std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2));
        SynchronizationTimeCollector::__times[std::make_pair("IterationSynchronizer", "heat_cpu")].push_back(time);

        AltBitSynchronizer altBitSwitchLoops(n_threads);
        time = measure_time(altBitSwitchLoops, std::bind(heat_cpu_switch_loops, std::placeholders::_1, std::placeholders::_2));
        SynchronizationTimeCollector::__times[std::make_pair("AltBitSynchronizer", "heat_cpu_switch_loops")].push_back(time);

        IterationSynchronizer iterationSyncSwitchLoops(n_threads);
        time = measure_time(iterationSyncSwitchLoops, std::bind(heat_cpu_switch_loops, std::placeholders::_1, std::placeholders::_2));
        SynchronizationTimeCollector::__times[std::make_pair("IterationSynchronizer", "heat_cpu_switch_loops")].push_back(time);

        /* time = Collector<PointPromisingSynchronizer>::collect(std::bind(heat_cpu_point_promise, 
                                                                           std::placeholders::_1,
                                                                           std::placeholders::_2,
                                                                           std::placeholders::_3,
                                                                           std::placeholders::_4),
                                                                  n_threads); 
        SynchronizationTimeCollector::__times[std::make_pair("PointPromisingSynchronizer", "heat_cpu_point_promise")].push_back(time);                                                          
        */

        BlockPromisingSynchronizer blockPromise(n_threads);
        time = measure_time(blockPromise, std::bind(heat_cpu_block_promise, 
                                                    std::placeholders::_1,
                                                    std::placeholders::_2,
                                                    std::placeholders::_3,
                                                    std::placeholders::_4));
        SynchronizationTimeCollector::__times[std::make_pair("BlockPromisingSynchronizer", "heat_cpu_block_promise")].push_back(time);

        /* time = Collector<IncreasingPointPromisingSynchronizer>::collect(std::bind(heat_cpu_increasing_point_promise,
                                                                                  std::placeholders::_1,
                                                                                  std::placeholders::_2,
                                                                                  std::placeholders::_3,
                                                                                  std::placeholders::_4),
                                                                        n_threads, &nb_points_for_iteration, g::NB_POINTS_PER_ITERATION);
        SynchronizationTimeCollector::__times[std::make_pair("IncreasingPointPromisingSynchronizer", "heat_cpu_increasing_point_promise")].push_back(time); */

        JLinePromisingSynchronizer jLinePromise(n_threads);
        time = measure_time(jLinePromise, std::bind(heat_cpu_jline_promise,
                                                    std::placeholders::_1,
                                                    std::placeholders::_2,
                                                    std::placeholders::_3,
                                                    std::placeholders::_4));
        SynchronizationTimeCollector::__times[std::make_pair("JLinePromisingSynchronizer", "heat_cpu_jline_promise")].push_back(time);

        IncreasingJLinePromisingSynchronizer increasingJLinePromise(n_threads, &nb_jlines_for_iteration, g::NB_J_LINES_PER_ITERATION);
        time = measure_time(increasingJLinePromise, std::bind(heat_cpu_increasing_jline_promise,
                                                              std::placeholders::_1,
                                                              std::placeholders::_2,
                                                              std::placeholders::_3,
                                                              std::placeholders::_4));
        SynchronizationTimeCollector::__times[std::make_pair("IncreasingJLinePromisingSynchronizer", "heat_cpu_increasing_jline_promise")].push_back(time); 

        BlockPromisePlusSynchronizer blockPromisePlus(n_threads, g::ITERATIONS);
        time = measure_time(blockPromisePlus, std::bind(heat_cpu_block_promise_plus, 
                                                        std::placeholders::_1,
                                                        std::placeholders::_2,
                                                        std::placeholders::_3,
                                                        std::placeholders::_4));
        SynchronizationTimeCollector::__times[std::make_pair("PromisePlusSynchronizer", "heat_cpu_block_promise_plus")].push_back(time);
    }

    static void print_times() {
        for (auto const& p: __times) {
            int count = 0;
            for (uint64 const& time: p.second) {
                lldiv_t result = lldiv(time, BILLION);
                std::cout << count << " " << p.first.first << " " << p.first.second << " " << result.quot << "." << result.rem << std::endl;
                ++count;
            }
        }
    }

    static std::map<std::pair<std::string, std::string>, std::vector<uint64>> __times;
};

std::map<std::pair<std::string, std::string>, std::vector<uint64>> SynchronizationTimeCollector::__times;

int main() {
    namespace g = Globals;

    if (!getenv("OMP_NUM_THREADS")) {
        std::cerr << "OMP_NUM_THREADS not set. Abort." << std::endl;
        exit(EXIT_FAILURE);
    }

    srand((unsigned)time(nullptr));

    init_logging();

    // spdlog::get(Loggers::Names::global_logger)->info("Starting");

    std::cout << "// W X Y Z Loops PromiseType" << std::endl <<
                 "// " << g::DIM_W << " " << g::DIM_X << " " << g::DIM_Y << " " << g::DIM_Z << " " << g::NB_GLOBAL_LOOPS << " " << to_string(g::PROMISE_TYPE) << std::endl;

    init_start_matrix_once();
    init_from_start_matrix(g_expected_matrix);
    assert_matrix_equals(g_start_matrix, g_expected_matrix);
    init_expected_matrix_once();

    for (int i = 0; i < g::NB_GLOBAL_LOOPS; ++i) {
        SynchronizationTimeCollector::collect_all();
    }

    SynchronizationTimeCollector::print_times();

    // spdlog::get(Loggers::Names::global_logger)->info("Ending");
    return 0;
}
