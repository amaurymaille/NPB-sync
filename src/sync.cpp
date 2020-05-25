#include <cassert>
#include <cstdio>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sys/time.h>

#include <omp.h>

#include "measure-time.h"
#include "spdlog/spdlog.h"

#include "active_promise.h"
#include "argv.h"
#include "config.h"
#include "defines.h"
#include "dynamic_config.h"
#include "functions.h"
#include "increase.h"
#include "logging.h"
#include "promise_plus.h"
#include "promises/naive_promise.h"
#include "promises/static_step_promise.h"
#include "utils.h"

using Clock = std::chrono::system_clock;
namespace g = Globals;

Matrix g_expected_matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z]);
Matrix g_start_matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z]);

namespace Globals {
    // Abort if a **single** simulation takes more than the given time
    DeadlockDetector deadlock_detector(10LL * MINUTES * TO_NANO);
    std::thread deadlock_detector_thread;
}

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

class SequentialSynchronizer : public Synchronizer {
public:
    SequentialSynchronizer() : Synchronizer() {

    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args) {
        for (int m = 1; m < Globals::ITERATIONS; ++m) {
            f(_matrix, std::forward<Args>(args)..., m);
        }   
    }
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

class CounterSynchronizer : public Synchronizer {
public:
    CounterSynchronizer(int nthreads) : Synchronizer(), _isync(nthreads) {
    
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
        _times[0] = 0;

        for (int i = 0; i < g::ITERATIONS; ++i)
            _promises_store.push_back(T(n));
    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args) {
        struct timespec begin, end;
        #pragma omp parallel
        {
            for (int m = 1; m < g::ITERATIONS; ++m) {                
                auto src_store = omp_get_thread_num() != 0 ? std::make_optional(std::ref(_promises_store[m])) : std::nullopt;
                auto dst_store = omp_get_thread_num() != omp_get_num_threads() - 1 ? std::make_optional(std::ref(_promises_store[m])) : std::nullopt;

                #pragma omp master
                {
                    clock_gettime(CLOCK_MONOTONIC, &begin);
                }

                f(_matrix, std::forward<Args>(args)..., m, dst_store, src_store);
                #pragma omp master
                {
                    clock_gettime(CLOCK_MONOTONIC, &end);
                    uint64 diff = clock_diff(&end, &begin);
                    _times[m] = diff;
                }
            }
        }
    }

    std::array<uint64, g::ITERATIONS> const& get_iterations_times() const {
        return _times;
    }

protected:
    std::vector<T> _promises_store;
    std::array<uint64, g::ITERATIONS> _times;
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

            int nb_promises = 0;

            for (int k = 0; k < MAX; ) {
                int nb_elements_for_synchronization = f(i, k);
                nb_promises++;
                k += nb_elements_for_synchronization;
            }

            for (int j = 0; j < container.size(); j++) {
                std::vector<typename Store::value_type::value_type> v(nb_promises);
                container[j] = std::move(v);
            }
        }
    }
};

using IncreasingPointPromisingSynchronizer = IncreasingIterationPromisingSynchronizer<IncreasingPointPromiseContainer>;
using IncreasingJLinePromisingSynchronizer = IncreasingIterationPromisingSynchronizer<IncreasingJLinePromiseContainer>;
using IncreasingKLinePromisingSynchronizer = IncreasingIterationPromisingSynchronizer<IncreasingKLinePromiseContainer>;

/*
template<typename Store>
class IterationPromisePlusSynchronizer : public IterationPromisingSynchronizer<Store> {
public:
    IterationPromisePlusSynchronizer(int n_threads, int index_max, PromisePlusWaitMode wait_mode = PromisePlusBase::DEFAULT_WAIT_MODE) : 
        IterationPromisingSynchronizer<Store>(n_threads) {
        for (Store& container: this->_promises_store) {
            for (auto& promise: container) {
                promise.set_wait_mode(wait_mode);
                promise.set_max_index(index_max);
            }
        }
    }
}; */

/*
template<typename Store>
class IterationValuesPromisePlusSynchronizer : public IterationPromisingSynchronizer<Store> {
public:
    IterationValuesPromisePlusSynchronizer(int n_threads, int nb_values, int index_max, 
                                           PromisePlusWaitMode wait_mode = PromisePlusBase::DEFAULT_WAIT_MODE) : 
        IterationPromisingSynchronizer<Store>(n_threads) {
        for (Store& container: this->_promises_store) {
            for (auto& promise: container) {
                promise.set_wait_mode(wait_mode);
                promise.set_max_index(index_max);
                promise.set_nb_values(nb_values);
            }
        }
    }
}; */

/*using BlockPromisePlusSynchronizer = IterationPromisePlusSynchronizer<BlockPromisePlusContainer>;
using JLinePromisePlusSynchronizer = IterationPromisePlusSynchronizer<JLinePromisePlusContainer>;
using KLinePromisePlusSynchronizer = IterationPromisePlusSynchronizer<KLinePromisePlusContainer>;
using IncreasingJLinePromisePlusSynchronizer = IterationValuesPromisePlusSynchronizer<IncreasingJLinePromisePlusContainer>;
using IncreasingKLinePromisePlusSynchronizer = IterationValuesPromisePlusSynchronizer<IncreasingKLinePromisePlusContainer>; */

template<typename T>
class PromisePlusSynchronizer : public Synchronizer {
public:
    PromisePlusSynchronizer(int n_threads, const PromisePlusBuilder<T>& builder) {
        for (int i = 0; i < g::ITERATIONS; ++i) {
            for (int j = 0; j < n_threads; j++)
                _promises_store[i].push_back(builder.new_promise());
        }
    }

    ~PromisePlusSynchronizer() {
        for (auto& store: _promises_store) {
            for (PromisePlus<T>* promise: store)
                delete promise;
        }
    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args) {
        struct timespec begin, end;
        _times[0] = 0;

        #pragma omp parallel
        {
            int thread_num = omp_get_thread_num();
            int num_threads = omp_get_num_threads();

            for (int i = 1; i < g::ITERATIONS; ++i) {
                #pragma omp master
                {
                    clock_gettime(CLOCK_MONOTONIC, &begin);
                }

                auto src = thread_num != 0 ? std::make_optional(_promises_store[i]) : std::nullopt;
                auto dst = thread_num != num_threads - 1 ? std::make_optional(_promises_store[i]) : std::nullopt;

                f(_matrix, i, dst, src);

                #pragma omp master
                {
                    clock_gettime(CLOCK_MONOTONIC, &end);
                    _times[i] = clock_diff(&end, &begin);
                }
            }
        }
    }

    std::array<uint64, g::ITERATIONS> const& get_iterations_times() const {
        return _times;
    }

private:
    std::array<PromisePlusContainer, g::ITERATIONS> _promises_store;
    std::array<uint64, g::ITERATIONS> _times;
};

template<class Synchronizer, class F, class... Args>
static uint64 measure_time(Synchronizer& synchronizer, F&& f, Args&&... args) {
    struct timespec begin, end;

    clock_gettime(CLOCK_MONOTONIC, &begin);
    synchronizer.run(f, std::forward<Args>(args)...);
    clock_gettime(CLOCK_MONOTONIC, &end);

    Globals::deadlock_detector.reset();

    // synchronizer.assert_okay();

    uint64 diff = clock_diff(&end, &begin);
    return diff;
}

class SynchronizationTimeCollector {
public:
    static void add_time(std::string const& synchronizer, std::string const& function, uint64 time) {
        SynchronizationTimeCollector::__times[std::make_pair(synchronizer, function)].push_back(time);
    }

    static void add_iterations_time(std::string const& synchronizer, std::string const& function, std::array<uint64, g::ITERATIONS> const& times) {
        SynchronizationTimeCollector::__iterations_times[std::make_pair(synchronizer, function)].push_back(times);
    }

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

        DynamicConfig::SynchronizationPatterns const& authorized = sDynamicConfig._patterns;
        uint64 time = 0;

        if (authorized._sequential) {
            SequentialSynchronizer seq;
            time = measure_time(seq, std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2));
            SynchronizationTimeCollector::add_time("SequentialSynchronizer", "heat_cpu", time);
        }
    
        if (authorized._alt_bit) {
            AltBitSynchronizer altBit(n_threads);
            time = measure_time(altBit, std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2));
            SynchronizationTimeCollector::add_time("AltBitSynchronizer", "heat_cpu", time);
        }

        if (authorized._counter) {
            CounterSynchronizer iterationSync(n_threads);
            time = measure_time(iterationSync, std::bind(heat_cpu, std::placeholders::_1, std::placeholders::_2));
            SynchronizationTimeCollector::add_time("CounterSynchronizer", "heat_cpu", time);
        }

        /* {
        AltBitSynchronizer altBitSwitchLoops(n_threads);
        time = measure_time(altBitSwitchLoops, std::bind(heat_cpu_switch_loops, std::placeholders::_1, std::placeholders::_2));
        SynchronizationTimeCollector::add_time("AltBitSynchronizer", "heat_cpu_switch_loops", time);
        } */

        /* {
        CounterSynchronizer iterationSyncSwitchLoops(n_threads);
        time = measure_time(iterationSyncSwitchLoops, std::bind(heat_cpu_switch_loops, std::placeholders::_1, std::placeholders::_2));
        SynchronizationTimeCollector::add_time("CounterSynchronizer", "heat_cpu_switch_loops", time);
        } */

        /* time = Collector<PointPromisingSynchronizer>::collect(std::bind(heat_cpu_point_promise, 
                                                                           std::placeholders::_1,
                                                                           std::placeholders::_2,
                                                                           std::placeholders::_3,
                                                                           std::placeholders::_4),
                                                                  n_threads); 
        SynchronizationTimeCollector::add_time("PointPromisingSynchronizer", "heat_cpu_point_promise", time);                                                          
        */

        if (authorized._block) {
            BlockPromisingSynchronizer blockPromise(n_threads);
            time = measure_time(blockPromise, std::bind(heat_cpu_block_promise, 
                                                        std::placeholders::_1,
                                                        std::placeholders::_2,
                                                        std::placeholders::_3,
                                                        std::placeholders::_4));
            SynchronizationTimeCollector::add_time("BlockPromisingSynchronizer", "heat_cpu_block_promise", time);
            SynchronizationTimeCollector::add_iterations_time("BlockPromisingSynchronizer", "heat_cpu_block_promise", blockPromise.get_iterations_times());
        }

        /* time = Collector<IncreasingPointPromisingSynchronizer>::collect(std::bind(heat_cpu_increasing_point_promise,
                                                                                  std::placeholders::_1,
                                                                                  std::placeholders::_2,
                                                                                  std::placeholders::_3,
                                                                                  std::placeholders::_4),
                                                                        n_threads, &nb_points_for_iteration, g::NB_POINTS_PER_ITERATION);
        SynchronizationTimeCollector::add_time("IncreasingPointPromisingSynchronizer", "heat_cpu_increasing_point_promise", time); */

        if (authorized._jline) {
            JLinePromisingSynchronizer jLinePromise(n_threads);
            time = measure_time(jLinePromise, std::bind(heat_cpu_jline_promise,
                                                        std::placeholders::_1,
                                                        std::placeholders::_2,
                                                        std::placeholders::_3,
                                                        std::placeholders::_4));
            SynchronizationTimeCollector::add_time("JLinePromisingSynchronizer", "heat_cpu_jline_promise", time);
            SynchronizationTimeCollector::add_iterations_time("JLinePromisingSynchronizer", "heat_cpu_jline_promise", jLinePromise.get_iterations_times());
        }

        if (authorized._increasing_jline) {
            IncreasingJLinePromisingSynchronizer increasingJLinePromise(n_threads, nb_jlines_for, g::NB_J_LINES_PER_ITERATION);
            time = measure_time(increasingJLinePromise, std::bind(heat_cpu_increasing_jline_promise,
                                                                  std::placeholders::_1,
                                                                  std::placeholders::_2,
                                                                  std::placeholders::_3,
                                                                  std::placeholders::_4));
            SynchronizationTimeCollector::add_time("IncreasingJLinePromisingSynchronizer", "heat_cpu_increasing_jline_promise", time); 
            SynchronizationTimeCollector::add_iterations_time("IncreasingJLinePromisingSynchronizer", "heat_cpu_increasing_jline_promise", increasingJLinePromise.get_iterations_times()); 
        }

        if (authorized._kline) {
            KLinePromisingSynchronizer kLinePromise(n_threads);
            time = measure_time(kLinePromise, std::bind(heat_cpu_kline_promise,
                                                        std::placeholders::_1,
                                                        std::placeholders::_2,
                                                        std::placeholders::_3,
                                                        std::placeholders::_4));
            SynchronizationTimeCollector::add_time("KLinePromisingSynchronizer", "heat_cpu_kline_promise", time);
            SynchronizationTimeCollector::add_iterations_time("KLinePromisingSynchronizer", "heat_cpu_kline_promise", kLinePromise.get_iterations_times());
        }

        if (authorized._increasing_kline) {
            IncreasingKLinePromisingSynchronizer increasingKLinePromise(n_threads, nb_klines_for, g::NB_K_LINES_PER_ITERATION);
            time = measure_time(increasingKLinePromise, std::bind(heat_cpu_increasing_kline_promise,
                                                                  std::placeholders::_1,
                                                                  std::placeholders::_2,
                                                                  std::placeholders::_3,
                                                                  std::placeholders::_4));
            SynchronizationTimeCollector::add_time("IncreasingKLinePromisingSynchronizer", "heat_cpu_increasing_kline_promise", time); 
            SynchronizationTimeCollector::add_iterations_time("IncreasingKLinePromisingSynchronizer", "heat_cpu_increasing_kline_promise", increasingKLinePromise.get_iterations_times()); 
        }

        if (authorized._block_plus) {
            /* BlockPromisePlusSynchronizer blockPromisePlus(n_threads, g::ITERATIONS);
            time = measure_time(blockPromisePlus, std::bind(heat_cpu_block_promise_plus, 
                                                            std::placeholders::_1,
                                                            std::placeholders::_2,
                                                            std::placeholders::_3,
                                                            std::placeholders::_4));
            SynchronizationTimeCollector::add_time("BlockPromisePlusSynchronizer", "heat_cpu_block_promise_plus", time);
            SynchronizationTimeCollector::add_iterations_time("BlockPromisePlusSynchronizer", "heat_cpu_block_promise_plus", blockPromisePlus.get_iterations_times()); */
        }

        if (authorized._jline_plus) {
            NaivePromiseBuilder<void> builder(Globals::NB_J_LINES_PER_ITERATION);
            PromisePlusSynchronizer<void> jLinePromisePlus(n_threads, builder);
            time = measure_time(jLinePromisePlus, std::bind(heat_cpu_promise_plus,
                                                            std::placeholders::_1,
                                                            std::placeholders::_2,
                                                            std::placeholders::_3,
                                                            std::placeholders::_4));
            SynchronizationTimeCollector::add_time("JLinePromisePlusSynchronizer", "heat_cpu_promise_plus", time);
            SynchronizationTimeCollector::add_iterations_time("JLinePromisePlusSynchronizer", "heat_cpu_promise_plus", jLinePromisePlus.get_iterations_times());
        }

        if (authorized._increasing_jline_plus) {
            StaticStepPromiseBuilder<void> builder(Globals::NB_J_LINES_PER_ITERATION, sDynamicConfigExtra._static_step_jline_plus);
            PromisePlusSynchronizer<void> increasingJLinePromisePlus(n_threads, builder);
            time = measure_time(increasingJLinePromisePlus, std::bind(heat_cpu_promise_plus,
                                                                      std::placeholders::_1,
                                                                      std::placeholders::_2,
                                                                      std::placeholders::_3,
                                                                      std::placeholders::_4));
            SynchronizationTimeCollector::add_time("IncreasingJLinePromisePlusSynchronizer", "heat_cpu_promise_plus", time);
            SynchronizationTimeCollector::add_iterations_time("IncreasingJLinePromisePlusSynchronizer", "heat_cpu_promise_plus", increasingJLinePromisePlus.get_iterations_times());
        }

        if (authorized._kline_plus) {
            /* KLinePromisePlusSynchronizer kLinePromisePlus(n_threads, g::NB_K_LINES_PER_ITERATION);
            time = measure_time(kLinePromisePlus, std::bind(heat_cpu_kline_promise_plus,
                                                            std::placeholders::_1,
                                                            std::placeholders::_2,
                                                            std::placeholders::_3,
                                                            std::placeholders::_4));
            SynchronizationTimeCollector::add_time("KLinePromisePlusSynchronizer", "heat_cpu_kline_promise_plus", time);
            SynchronizationTimeCollector::add_iterations_time("KLinePromisePlusSynchronizer", "heat_cpu_kline_promise_plus", kLinePromisePlus.get_iterations_times()); */
        }

        if (authorized._increasing_kline_plus) {
            /* IncreasingKLinePromisePlusSynchronizer increasingKLinePromisePlus(n_threads, g::NB_K_LINES_PER_ITERATION, g::NB_K_LINES_PER_ITERATION);
            time = measure_time(increasingKLinePromisePlus, std::bind(heat_cpu_increasing_kline_promise_plus,
                                                                      std::placeholders::_1,
                                                                      std::placeholders::_2,
                                                                      std::placeholders::_3,
                                                                      std::placeholders::_4));
            SynchronizationTimeCollector::add_time("IncreasingKLinePromisePlusSynchronizer", "heat_cpu_increasing_kline_promise_plus", time);
            SynchronizationTimeCollector::add_iterations_time("IncreasingKLinePromisePlusSynchronizer", "heat_cpu_increasing_kline_promise_plus", increasingKLinePromisePlus.get_iterations_times()); */
        }
    }

    static void print_times() {
        for (auto const& p: __times) {
            int count = 0;
            for (uint64 const& time: p.second) {
                // Do not add the leading zeros the decimal part, because it is way too complicated to do in this god awful language
                lldiv_t result = lldiv(time, BILLION);
                ExtraConfig::runs_times_file() << count << " " << p.first.first << " " << p.first.second << " " << result.quot << "." << ns_with_leading_zeros(result.rem) << std::endl;
                ++count;
            }
        }
    }

    static void print_iterations_times() {
        ExtraConfig::iterations_times_file() << "Global" << " " << "Local" << " " << "Synchronizer" << " " << "Function" << " " << "Time" << std::endl; 
        for (auto const& p: __iterations_times) {
            int count = 0;
            for (std::array<uint64, g::ITERATIONS> const& times: p.second) {
                int iter = 0;
                for (uint64 const& time: times) {
                    lldiv_t result = lldiv(time, BILLION);
                    ExtraConfig::iterations_times_file() << count << " " << iter << " " << p.first.first << " " << p.first.second << " " << result.quot << "." << ns_with_leading_zeros(result.rem) << std::endl;
                    iter++;
                }
                count++;
            }
        }
    }

    static std::map<std::pair<std::string, std::string>, std::vector<uint64>> __times;
    static std::map<std::pair<std::string, std::string>, std::vector<std::array<uint64, g::ITERATIONS>>> __iterations_times;
};

std::map<std::pair<std::string, std::string>, std::vector<uint64>> SynchronizationTimeCollector::__times;
std::map<std::pair<std::string, std::string>, std::vector<std::array<uint64, g::ITERATIONS>>> SynchronizationTimeCollector::__iterations_times;

int main(int argc, char** argv) {
    namespace g = Globals;

    parse_command_line(argc, argv);

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

    // Globals::deadlock_detector_thread = std::thread(&DeadlockDetector::run, &(Globals::deadlock_detector));
    for (int i = 0; i < g::NB_GLOBAL_LOOPS; ++i) {
        SynchronizationTimeCollector::collect_all();
    }

    SynchronizationTimeCollector::print_times();
    SynchronizationTimeCollector::print_iterations_times();
    // Globals::deadlock_detector.stop();
    // Globals::deadlock_detector_thread.join();

    // spdlog::get(Loggers::Names::global_logger)->info("Ending");
    return 0;
}
