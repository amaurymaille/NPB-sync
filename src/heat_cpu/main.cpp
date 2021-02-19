#include <cassert>
#include <cstdio>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iomanip>
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

#include "argv.h"
#include "config.h"
#include "core.h"
#include "defines.h"
#include "dynamic_config.h"
#include "heat_cpu/dynamic_defines.h"
#include "heat_cpu/heat_cpu.h"
#include "logging.h"
#include "heat_cpu/matrix_core.h"
#include "naive_promise.h"
#include "promise_plus.h"
#include "promises/naive_promise.h"
#include "promises/static_step_promise.h"
#include "utils.h"

using json = nlohmann::json;
namespace g = Globals;

class SequentialSynchronizer : public Synchronizer<HeatCPUMatrix> {
public:
    SequentialSynchronizer(HeatCPUMatrix const& m, Matrix4D& matrix) : Synchronizer(m, matrix) {

    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args) {
        for (int m = 1; m < Globals::HeatCPU::ITERATIONS; ++m) {
            f(_matrix, std::forward<Args>(args)..., m);
        }   
    }

};

class AltBitSynchronizer : public Synchronizer<HeatCPUMatrix> {
public:
    AltBitSynchronizer(HeatCPUMatrix const& m, Matrix4D& matrix, int nthreads) : Synchronizer(m, matrix), _isync(nthreads) {

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

        for (int m = 1; m < g::HeatCPU::ITERATIONS; m++) {
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
            _isync[i].store(false, std::memory_order_relaxed);
    }

    void sync_left(int thread_num, int n_threads) {
        namespace g = Globals;

        if (thread_num > 0 && thread_num <= n_threads) {
            // printf("[%s][sync_left] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, i);
            int neighbour = thread_num - 1;
            bool sync_state = _isync[neighbour].load(std::memory_order_acquire);

            while (sync_state == false)
                sync_state = _isync[neighbour].load(std::memory_order_acquire);

            _isync[neighbour].store(false, std::memory_order_release);
            // printf("[%s][sync_left] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, i);
        }
    }

    void sync_right(int thread_num, int n_threads) {
        namespace g = Globals;

        if (thread_num < n_threads) {
            // printf("[%s][sync_right] Thread %d: begin iteration %d\n", get_time_default_fmt(), thread_num, i);
            
            bool sync_state = _isync[thread_num].load(std::memory_order_acquire);
            while (sync_state == true)
                sync_state = _isync[thread_num].load(std::memory_order_acquire);

            _isync[thread_num].store(true, std::memory_order_release);

            // printf("[%s][sync_right] Thread %d: end iteration %d\n", get_time_default_fmt(), thread_num, i);
        }
    }
};

class CounterSynchronizer : public Synchronizer<HeatCPUMatrix> {
public:
    CounterSynchronizer(HeatCPUMatrix const& m, Matrix4D& matrix, int nthreads) : Synchronizer(m, matrix), _isync(nthreads) {
    
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

        for (int m = 1; m < g::HeatCPU::ITERATIONS; ++m) {
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
            unsigned int sync_state = _isync[neighbour].load(std::memory_order_acquire);

            // Wait for left neighbour to have finished its iteration
            while (sync_state <= i)
                sync_state = _isync[neighbour].load(std::memory_order_release);

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

typedef std::array<std::vector<uint64>, g::HeatCPU::ITERATIONS> IterationTimeByThreadStore;

template<typename T>
class PromisePlusSynchronizer : public Synchronizer<HeatCPUMatrix> {
public:
    PromisePlusSynchronizer(HeatCPUMatrix const& m, Matrix4D& matrix, int n_threads, const PromisePlusBuilder<T>& builder) : Synchronizer(m, matrix) {
#ifdef PROMISE_PLUS_DEBUG_COUNTERS
        _promise_plus_debug_data = json::array();
#endif

        for (int i = 0; i < g::HeatCPU::ITERATIONS; ++i) {
            for (int j = 0; j < n_threads; j++)
                _promises_store[i].push_back(builder.new_promise());
        }

#ifdef PROMISE_PLUS_ITERATION_TIMER
        for (int i = 0; i < g::HeatCPU::ITERATIONS; ++i) {
            _times_by_thread[i].resize(n_threads);
        }
#endif 

    }

    ~PromisePlusSynchronizer() {
        for (auto& store: _promises_store) {
            for (PromisePlus<T>* promise: store)
                delete promise;
        }
    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args) {
        #pragma omp parallel
        {
#ifdef PROMISE_PLUS_ITERATION_TIMER
            struct timespec thread_begin, thread_end;
#endif
            int thread_num = omp_get_thread_num();
            int num_threads = omp_get_num_threads();

            for (int i = 1; i < g::HeatCPU::ITERATIONS; ++i) {
                auto src = thread_num != 0 ? std::make_optional(_promises_store[i]) : std::nullopt;
                auto dst = thread_num != num_threads - 1 ? std::make_optional(_promises_store[i]) : std::nullopt;

#ifdef PROMISE_PLUS_ITERATION_TIMER
                clock_gettime(CLOCK_MONOTONIC, &thread_begin);
#endif

                f(_matrix, i, dst, src);

#ifdef PROMISE_PLUS_ITERATION_TIMER
                clock_gettime(CLOCK_MONOTONIC, &thread_end);

                _times_by_thread[i][omp_get_thread_num()] = clock_diff(&thread_end, &thread_begin);
#endif
            }
        }

#ifdef PROMISE_PLUS_DEBUG_COUNTERS
        gather_promise_plus_datas();
#endif
    }

#ifdef PROMISE_PLUS_DEBUG_COUNTERS
    const json& get_promise_plus_datas() const {
        return _promise_plus_debug_data;
    }
#endif

#ifdef PROMISE_PLUS_ITERATION_TIMER
    IterationTimeByThreadStore const& get_iterations_times_by_thread() const {
        return _times_by_thread;
    }
#endif

private: 
    std::array<PromisePlusContainer, g::HeatCPU::ITERATIONS> _promises_store;
#ifdef PROMISE_PLUS_ITERATION_TIMER
    IterationTimeByThreadStore _times_by_thread;
#endif

#ifdef PROMISE_PLUS_DEBUG_COUNTERS
    void gather_promise_plus_datas() {
        for (int i = 0; i < g::HeatCPU::ITERATIONS; ++i) {
            for (int j = 0; j < _promises_store[i].size(); ++j) {
                ActiveStaticStepPromise<T>* promise = static_cast<ActiveStaticStepPromise<T>*>(_promises_store[i][j]);
                auto [wait, strong, weak] = promise->get_debug_data();

                json debug_data_struct = json::object();
                debug_data_struct["iteration"] = i;
                debug_data_struct["thread"] = j;

                json debug_data = json::object();
                debug_data["wait"] = wait;
                debug_data["strong"] = strong;
                debug_data["weak"] = weak;
#ifdef PROMISE_PLUS_ITERATION_TIMER
                debug_data["iteration_time"] = _times_by_thread[i][j];
#endif

                // debug_data["set_times"] = promise->get_set_times();

                debug_data_struct["data"] = debug_data;
                _promise_plus_debug_data.push_back(debug_data_struct);
            }
        }
    }

    json _promise_plus_debug_data;
#endif
};

class ArrayOfPromisesSynchronizer : public Synchronizer<HeatCPUMatrix> {
public:
    ArrayOfPromisesSynchronizer(HeatCPUMatrix const& m, Matrix4D& matrix, int nb_threads) : Synchronizer(m, matrix) {
        for (int i = 0; i < g::HeatCPU::ITERATIONS; ++i) {
            _promises_store[i].resize(nb_threads);
            for (int j = 0; j < nb_threads; ++j) {
                // _promises_store[i][j] = new std::promise<void>[Globals::HeatCPU::DIM_Y];
                _promises_store[i][j] = new NaivePromise<void>[Globals::HeatCPU::DIM_Y];
            }
        }
    }

    ~ArrayOfPromisesSynchronizer() {
        for (int i = 0; i < g::HeatCPU::ITERATIONS; ++i) {
            for (int j = 0; j < _promises_store[i].size(); ++j) {
                delete[] _promises_store[i][j];
            }
        }
    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args) {
        #pragma omp parallel
        {
            int thread_num = omp_get_thread_num();
            int num_threads = omp_get_num_threads();

            for (int i = 1; i < g::HeatCPU::ITERATIONS; ++i) {
                auto src = thread_num != 0 ? std::make_optional(_promises_store[i]) : std::nullopt;
                auto dst = thread_num != num_threads - 1 ? std::make_optional(_promises_store[i]) : std::nullopt;

                f(_matrix, i, dst, src);
            }
        }
    }

private:
    std::array<ArrayOfPromisesContainer, g::HeatCPU::ITERATIONS> _promises_store;
};

class PromiseOfArraySynchronizer : public Synchronizer<HeatCPUMatrix> {
public:
    PromiseOfArraySynchronizer(HeatCPUMatrix const &m, Matrix4D& matrix, int nb_threads) : Synchronizer(m, matrix) {
        for (int i = 0; i < g::HeatCPU::ITERATIONS; ++i) {
            _promises_store[i].resize(nb_threads);
            for (int j = 0; j < nb_threads; ++j) {
                _promises_store[i][j] = new NaivePromise<void>();
            }
        }
    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args) {
        #pragma omp parallel
        {
            int thread_num = omp_get_thread_num();
            int num_threads = omp_get_num_threads();

            for (int i = 1; i < g::HeatCPU::ITERATIONS; ++i) {
                auto src = thread_num != 0 ? std::make_optional(_promises_store[i]) : std::nullopt;
                auto dst = thread_num != num_threads - 1 ? std::make_optional(_promises_store[i]) : std::nullopt;

                f(_matrix, i, dst, src);
            }
        }
    }

private:
    std::array<PromiseOfArrayContainer, g::HeatCPU::ITERATIONS> _promises_store;
};

class HeatCPUTimeCollector : public TimeCollector {
public:
    void add_iterations_times_by_thread(TimeLog& log, unsigned int global_iteration, IterationTimeByThreadStore const& times) {
        unsigned int local_iteration = 0;
        for (std::vector<uint64> const& store: times) {
            unsigned int thread_id = 0;
            for (uint64 time: store) {
                log.add_time_for_iteration(global_iteration, local_iteration, thread_id, double(time) / BILLION);
                ++thread_id;
            }
            ++local_iteration;
        }
    }

    void run_sequential(unsigned int nb_iterations) {
        uint64 time = 0;
        TimeLog log("Sequential", "heat_cpu");
        Matrix4D matrix(boost::extents[g::HeatCPU::DIM_W][g::HeatCPU::DIM_X][g::HeatCPU::DIM_Y][g::HeatCPU::DIM_Z]);

        for (unsigned int i = 0; i < nb_iterations; ++i) {
            SequentialSynchronizer seq(sHeatCPU, matrix);
            time = measure_synchronizer_time(seq, [](auto&& matrix, auto&& m) {
                heat_cpu(matrix, m);
            });
            add_time(log, i, time);
        }

        _times.push_back(log);
    }

    void run_alt_bit(unsigned int nb_iterations) {
        unsigned int nb_threads = omp_nb_threads();
        uint64 time = 0;
        TimeLog log("AltBit", "heat_cpu");
        Matrix4D matrix(boost::extents[g::HeatCPU::DIM_W][g::HeatCPU::DIM_X][g::HeatCPU::DIM_Y][g::HeatCPU::DIM_Z]);

        for (unsigned int i = 0; i < nb_iterations; ++i) {
            AltBitSynchronizer altBit(sHeatCPU, matrix, nb_threads);
            time = measure_synchronizer_time(altBit, [](auto&& matrix, auto&& m) {
                heat_cpu(matrix, m);
            });
            add_time(log, i, time);
        }

        _times.push_back(log);
    }

    void run_atomic_counter(unsigned int nb_iterations) {
        unsigned int nb_threads = omp_nb_threads();
        uint64 time = 0;
        TimeLog log("Counter", "heat_cpu");
        Matrix4D matrix(boost::extents[g::HeatCPU::DIM_W][g::HeatCPU::DIM_X][g::HeatCPU::DIM_Y][g::HeatCPU::DIM_Z]);

        for (unsigned int i = 0; i < nb_iterations; ++i) {
            CounterSynchronizer iterationSync(sHeatCPU, matrix, nb_threads);
            time = measure_synchronizer_time(iterationSync, [](auto&& matrix, auto&& m) {
                heat_cpu(matrix, m);
            });
            add_time(log, i, time);
        }

        _times.push_back(log);
    }

    void run_static_step_promise_plus(unsigned int nb_iterations, unsigned int step) {
        unsigned int nb_threads = omp_nb_threads();
        uint64 time = 0;
        TimeLog log("StaticStep+", "promise_plus");
        TimeLog iterations_log("StaticStep+", "promise_plus");

        log.add_extra_arg("step", step);
        iterations_log.add_extra_arg("step", step);
        Matrix4D matrix(boost::extents[g::HeatCPU::DIM_W][g::HeatCPU::DIM_X][g::HeatCPU::DIM_Y][g::HeatCPU::DIM_Z]);
         
        StaticStepPromiseBuilder<void> builder(Globals::HeatCPU::DIM_Y, step, nb_threads);

        for (unsigned int i = 0; i < nb_iterations; ++i) {
            printf("StaticStep: iteration %d\n", i);
            PromisePlusSynchronizer<void> promisePlusSynchronizer(sHeatCPU, matrix, nb_threads, builder);

            time = measure_synchronizer_time(promisePlusSynchronizer, [](auto&& matrix, auto&& m, auto&& dst, auto&& src) {
                heat_cpu_promise_plus(matrix, m, dst, src);
            });
#ifdef PROMISE_PLUS_ITERATION_TIMER
            add_iterations_times_by_thread(iterations_log, i, promisePlusSynchronizer.get_iterations_times_by_thread());
#endif

#ifdef PROMISE_PLUS_DEBUG_COUNTERS
            json debug_data = promisePlusSynchronizer.get_promise_plus_datas();
            add_time(log, i, time, debug_data);
#else
            add_time(log, i, time);
#endif
        }

        _times.push_back(log);
        _iterations_times.push_back(iterations_log);
    }

    void run_array_of_promises(unsigned int nb_iterations) {
        unsigned int nb_threads = omp_nb_threads();
        uint64 time = 0;
        TimeLog log("ArrayOfPromises", "array_of_promises");
        Matrix4D matrix(boost::extents[g::HeatCPU::DIM_W][g::HeatCPU::DIM_X][g::HeatCPU::DIM_Y][g::HeatCPU::DIM_Z]);

        for (unsigned int i = 0; i < nb_iterations; ++i) {
            ArrayOfPromisesSynchronizer arrayOfPromisesSynchronizer(sHeatCPU, matrix, nb_threads);
            time = measure_synchronizer_time(arrayOfPromisesSynchronizer, [](auto&& matrix, auto&& m, auto&& dst, auto&& src) {
                heat_cpu_array_of_promises(matrix, m, dst, src);
            });
            add_time(log, i, time);
        }

        _times.push_back(log);
    }

    void run_promise_of_array(unsigned int nb_iterations) {
        unsigned int nb_threads = omp_nb_threads();
        uint64 time = 0;
        TimeLog log("PromiseOfArray", "promise_of_array");
        Matrix4D matrix(boost::extents[g::HeatCPU::DIM_W][g::HeatCPU::DIM_X][g::HeatCPU::DIM_Y][g::HeatCPU::DIM_Z]);

        for (unsigned int i = 0; i < nb_iterations; ++i) {
            PromiseOfArraySynchronizer promiseOfArraySynchronizer(sHeatCPU, matrix, nb_threads);
            time = measure_synchronizer_time(promiseOfArraySynchronizer, [](auto&& matrix, auto&& m, auto&& dst, auto&& src) {
                heat_cpu_promise_of_array(matrix, m, dst, src);
            });
            add_time(log, i, time);
        }

        _times.push_back(log);
    }
   

    void print_times() {
        json runs_times;
        json runs = json::array();

        for (auto const& log: _times) {
            runs.push_back(log.get_json());
        }

        runs_times["runs"] = runs;
        ExtraConfig::runs_times_file() << std::setw(4) << runs_times;
    }

    void print_iterations_times() {
        json data;
        json runs = json::array();
        for (auto const& log: _iterations_times) {
            runs.push_back(log.get_json());
        }

        data["runs"] = runs;
        ExtraConfig::iterations_times_file() << std::setw(4) << data;
    }

private:
    std::vector<TimeLog> _iterations_times;
};

namespace JSON {
    namespace Run {
        namespace Synchronizers {
            static const std::string alt_bit("alt_bit");
            static const std::string counter("counter");
        }

        static const std::vector<std::string> authorized_synchronizers = {
            Synchronizers::sequential,
            Synchronizers::alt_bit,
            Synchronizers::counter,
            Synchronizers::static_step_promise_plus,
            Synchronizers::array_of_promises,
            Synchronizers::promise_of_array
        };

    }
}

namespace Sync = JSON::Run::Synchronizers;

class HeatCPURunner : public Runner {
public:
    HeatCPURunner(std::string const& filename) : Runner(filename) {

    }

    void validate_synchronizer(std::string const& synchronizer) {
        auto const& authorized = JSON::Run::authorized_synchronizers;
        if (std::find(authorized.begin(), authorized.end(), synchronizer) == authorized.end()) {
            std::ostringstream stream;
            stream << "Synchronizer " << synchronizer << " is not valid" << std::endl;
            stream << "Authorized synchronizers are: ";

            std::ostream_iterator<std::string> iter(stream, ", ");
            std::copy(authorized.begin(), authorized.end(), iter);
            stream << std::endl;

            throw std::runtime_error(stream.str());
        }
    }

    void process_run(unsigned int iterations, json run) {
        std::string const& synchronizer = run[JSON::Run::synchronizer];

        if (synchronizer == Sync::sequential) {
            _collector.run_sequential(iterations);
        } else if (synchronizer == Sync::alt_bit) {
            _collector.run_alt_bit(iterations);
        } else if (synchronizer == Sync::counter) {
            _collector.run_atomic_counter(iterations);
        } else if (synchronizer == Sync::static_step_promise_plus) {
            unsigned int step = 1;
            if (run.contains(JSON::Run::extras)) {
                const json& extras = run[JSON::Run::extras];
                if (extras.contains(JSON::Run::Extras::step)) {
                    step = extras[JSON::Run::Extras::step].get<unsigned int>();
                }
            }

            _collector.run_static_step_promise_plus(iterations, step);
        } else if (synchronizer == Sync::array_of_promises) {
            _collector.run_array_of_promises(iterations);
        } else if (synchronizer == Sync::promise_of_array) {
            _collector.run_promise_of_array(iterations);
        } else {
            assert(false);
        }
    }

    void dump() {
        _collector.print_times();
        _collector.print_iterations_times();
    }

private:
    HeatCPUTimeCollector _collector;
};

void log_general_data(std::ostream& out) {
    namespace g = Globals;

    json data;
    data["w"] = g::HeatCPU::DIM_W;
    data["x"] = g::HeatCPU::DIM_X;
    data["y"] = g::HeatCPU::DIM_Y;
    data["z"] = g::HeatCPU::DIM_Z;
    
    std::ifstream stream(sDynamicConfigFiles.get_simulations_filename());
    json simu;
    stream >> simu;
    stream.close();

    data["file"] = sDynamicConfigFiles.get_simulations_filename();
    data["iterations"] = simu["iterations"];

    data["threads"] = omp_nb_threads();
    data["description"] = sDynamicConfigStd._description;

    out << std::setw(4) << data;
}

int main(int argc, char** argv) {
    namespace g = Globals;

    srand((unsigned)time(nullptr));

    init_logging();

    parse_command_line(argc, argv);

    if (!getenv("OMP_NUM_THREADS")) {
        std::cerr << "OMP_NUM_THREADS not set. Abort." << std::endl;
        exit(EXIT_FAILURE);
    }

    HeatCPURunner runner(sDynamicConfigFiles.get_simulations_filename());

    runner.run();
    runner.dump();

    log_general_data(DynamicConfig::_instance()._files.parameters_file());

    return 0;
}
