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
#include "nlohmann/json.hpp"

#include "argv.h"
#include "config.h"
#include "defines.h"
#include "dynamic_config.h"
#include "functions.h"
#include "logging.h"
#include "naive_promise.h"
#include "promise_plus.h"
#include "promises/naive_promise.h"
#include "promises/static_step_promise.h"
#include "utils.h"

using Clock = std::chrono::system_clock;
using json = nlohmann::json;
namespace g = Globals;

Matrix g_start_matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z]);
Matrix g_expected_matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z]);
// MatrixReorderer* g_expected_matrix = new StandardMatrixReorderer(g::DIM_W, g::DIM_X, g::DIM_Y, g::DIM_Z);

// Matrix g_reordered_start_matrix(boost::extents[g::DIM_W][g::DIM_Z][g::DIM_Y][g::DIM_X]);
// MatrixReorderer* g_expected_reordered_matrix = new JLinePromiseMatrixReorderer(g::DIM_W, g::DIM_X, g::DIM_Y, g::DIM_Z);

class Synchronizer {
protected:
    Synchronizer(Matrix& matrix) : _matrix(matrix) {
        init_from_start_matrix(_matrix);
        // _matrix.init();
        assert_matrix_equals(_matrix, g_start_matrix);
        // _matrix.assert_okay_init();
    }
    
public:
    void assert_okay() {
        assert_matrix_equals(_matrix, g_expected_matrix);
        // _matrix.assert_okay_compute();
    }

protected:
    Matrix& _matrix;
};

class SequentialSynchronizer : public Synchronizer {
public:
    SequentialSynchronizer(Matrix& matrix) : Synchronizer(matrix) {

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
    AltBitSynchronizer(Matrix& matrix, int nthreads) : Synchronizer(matrix), _isync(nthreads) {

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

class CounterSynchronizer : public Synchronizer {
public:
    CounterSynchronizer(Matrix& matrix, int nthreads) : Synchronizer(matrix), _isync(nthreads) {
    
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

typedef std::array<std::vector<uint64>, g::ITERATIONS> IterationTimeByThreadStore;

template<typename T>
class PromisePlusSynchronizer : public Synchronizer {
public:
    PromisePlusSynchronizer(Matrix& matrix, int n_threads, const PromisePlusBuilder<T>& builder) : Synchronizer(matrix) {
#ifdef PROMISE_PLUS_DEBUG_COUNTERS
        _promise_plus_debug_data = json::array();
#endif

        for (int i = 0; i < g::ITERATIONS; ++i) {
            for (int j = 0; j < n_threads; j++)
                _promises_store[i].push_back(builder.new_promise());
        }

#ifdef PROMISE_PLUS_ITERATION_TIMER
        for (int i = 0; i < g::ITERATIONS; ++i) {
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

            for (int i = 1; i < g::ITERATIONS; ++i) {
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
    std::array<PromisePlusContainer, g::ITERATIONS> _promises_store;
#ifdef PROMISE_PLUS_ITERATION_TIMER
    IterationTimeByThreadStore _times_by_thread;
#endif

#ifdef PROMISE_PLUS_DEBUG_COUNTERS
    void gather_promise_plus_datas() {
        for (int i = 0; i < g::ITERATIONS; ++i) {
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

class ArrayOfPromisesSynchronizer : public Synchronizer {
public:
    ArrayOfPromisesSynchronizer(Matrix& matrix, int nb_threads) : Synchronizer(matrix) {
        for (int i = 0; i < g::ITERATIONS; ++i) {
            _promises_store[i].resize(nb_threads);
            for (int j = 0; j < nb_threads; ++j) {
                // _promises_store[i][j] = new std::promise<void>[Globals::DIM_Y];
                _promises_store[i][j] = new NaivePromise<void>[Globals::DIM_Y];
            }
        }
    }

    ~ArrayOfPromisesSynchronizer() {
        for (int i = 0; i < g::ITERATIONS; ++i) {
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

            for (int i = 1; i < g::ITERATIONS; ++i) {
                auto src = thread_num != 0 ? std::make_optional(_promises_store[i]) : std::nullopt;
                auto dst = thread_num != num_threads - 1 ? std::make_optional(_promises_store[i]) : std::nullopt;

                f(_matrix, i, dst, src);
            }
        }
    }

private:
    std::array<ArrayOfPromisesContainer, g::ITERATIONS> _promises_store;
};

class PromiseOfArraySynchronizer : public Synchronizer {
public:
    PromiseOfArraySynchronizer(Matrix& matrix, int nb_threads) : Synchronizer(matrix) {
        for (int i = 0; i < g::ITERATIONS; ++i) {
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

            for (int i = 1; i < g::ITERATIONS; ++i) {
                auto src = thread_num != 0 ? std::make_optional(_promises_store[i]) : std::nullopt;
                auto dst = thread_num != num_threads - 1 ? std::make_optional(_promises_store[i]) : std::nullopt;

                f(_matrix, i, dst, src);
            }
        }
    }

private:
    std::array<PromiseOfArrayContainer, g::ITERATIONS> _promises_store;
};

template<typename F, typename... Args>
static uint64 measure_time(F&& f, Args&&... args) {
    struct timespec begin, end;

    clock_gettime(CLOCK_MONOTONIC, &begin);
    f(std::forward<Args>(args)...);
    clock_gettime(CLOCK_MONOTONIC, &end);

    uint64 diff = clock_diff(&end, &begin);
    return diff;
}

template<class Synchronizer, class F, class... Args>
static uint64 measure_synchronizer_time(Synchronizer& synchronizer, F&& f, Args&&... args) {
    // synchronizer.run(f, std::forward<Args>(args)...);
    uint64 diff = measure_time([&]() {
        synchronizer.run(f, args...);
    });

    synchronizer.assert_okay();

    return diff;
}

class TimeLog {
public:
    TimeLog(std::string const& synchronizer, std::string const& function) {
        _json["synchronizer"] = synchronizer;
        _json["function"] = function;
        _json["extras"] = json::object();
    }

    void add_time(unsigned int iteration, double time) {
        _json["times"][iteration] = time;
    }

    void add_time(unsigned int iteration, double time, json debug_data) {
        json data;
        data["time"] = time;
        data["debug"] = debug_data;

        _json["times"][iteration] = data;
    }

    void add_time_for_iteration(unsigned int iteration, unsigned int local_iteration, unsigned int thread_id, double time) {
        _json["times"]["iterations"][iteration]["local_iterations"][local_iteration]["threads"][thread_id] = time;
    }

    template<typename T>
    void add_extra_arg(const std::string& key, T const& value) {
        _json["extras"][key] = value;
    }

    const json& get_json() const {
        return _json;
    }

protected:
    json _json;
};

class SynchronizationTimeCollector {
public:
    void add_time(TimeLog& log, unsigned int iteration, uint64 time) {
        log.add_time(iteration, double(time) / BILLION);
    }

    void add_time(TimeLog& log, unsigned int iteration, uint64 time, json debug_data) {
        log.add_time(iteration, double(time) / BILLION, debug_data);
    }

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
        Matrix matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z]);

        for (unsigned int i = 0; i < nb_iterations; ++i) {
            SequentialSynchronizer seq(matrix);
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
        Matrix matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z]);

        for (unsigned int i = 0; i < nb_iterations; ++i) {
            AltBitSynchronizer altBit(matrix, nb_threads);
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
        Matrix matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z]);

        for (unsigned int i = 0; i < nb_iterations; ++i) {
            CounterSynchronizer iterationSync(matrix, nb_threads);
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
        Matrix matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z]);
         
        StaticStepPromiseBuilder<void> builder(Globals::DIM_Y, step, nb_threads);

        for (unsigned int i = 0; i < nb_iterations; ++i) {
            printf("StaticStep: iteration %d\n", i);
            PromisePlusSynchronizer<void> promisePlusSynchronizer(matrix, nb_threads, builder);

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
        Matrix matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z]);

        for (unsigned int i = 0; i < nb_iterations; ++i) {
            ArrayOfPromisesSynchronizer arrayOfPromisesSynchronizer(matrix, nb_threads);
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
        Matrix matrix(boost::extents[g::DIM_W][g::DIM_X][g::DIM_Y][g::DIM_Z]);

        for (unsigned int i = 0; i < nb_iterations; ++i) {
            PromiseOfArraySynchronizer promiseOfArraySynchronizer(matrix, nb_threads);
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
    std::vector<TimeLog> _times;
    std::vector<TimeLog> _iterations_times;
};

namespace JSON {
    namespace Top {
        static const std::string iterations("iterations");
        static const std::string runs("runs");
    }

    namespace Run {
        static const std::string synchronizer("synchronizer");
        static const std::string extras("extras");

        namespace Synchronizers {
            static const std::string sequential("sequential");
            static const std::string alt_bit("alt-bit");
            static const std::string counter("counter");
            static const std::string static_step_promise_plus("static-step-plus");
            static const std::string array_of_promises("array-of-promises");
            static const std::string promise_of_array("promise-of-array");
        }

        static const std::vector<std::string> authorized_synchronizers = {
            Synchronizers::sequential,
            Synchronizers::alt_bit,
            Synchronizers::counter,
            Synchronizers::static_step_promise_plus,
            Synchronizers::array_of_promises,
            Synchronizers::promise_of_array
        };

        namespace Extras {
            static const std::string step("step");
        }
    }
}

class Runner {
public:
    Runner(std::string const& filename) {
        init_from_file(filename);
        validate();
    }

    void run() {
        unsigned int iterations = _data[JSON::Top::iterations].get<unsigned int>();
        json runs = _data[JSON::Top::runs];

        for (json run: runs) {
            process_run(iterations, run);
        }
    }

    void dump() {
        _collector.print_times();
        _collector.print_iterations_times();
    }

private:
    void init_from_file(std::string const& filename) {
        std::ifstream stream(filename);
        stream >> _data;
    }

    void validate() {
        validate_top();
        validate_runs();
    }

    void validate_top() {
        throw_if_not_present(JSON::Top::iterations);
        throw_if_not_present(JSON::Top::runs);
    }

    void validate_runs() {
        json runs = _data[JSON::Top::runs];

        for (json run: runs) {
            validate_run(run);
        }
    }

    void validate_run(json run) {
        throw_if_not_present_subobject(run, JSON::Run::synchronizer);

        validate_synchronizer(run[JSON::Run::synchronizer]);
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

    void throw_if_not_present(std::string const& key) {
        throw_if_not_present_subobject(_data, key);
    }

    void throw_if_not_present_subobject(json const& subobject, std::string const& key) {
        if (!subobject.contains(key)) {
            std::ostringstream stream;
            stream << "Key " << key << " not found in JSON" << std::endl;
            throw std::runtime_error(stream.str());
        }
    }

    void process_run(unsigned int iterations, json run) {
        namespace Sync = JSON::Run::Synchronizers;

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

    json _data;
    SynchronizationTimeCollector _collector;
};

void log_general_data(std::ostream& out) {
    namespace g = Globals;

    json data;
    data["w"] = g::DIM_W;
    data["x"] = g::DIM_X;
    data["y"] = g::DIM_Y;
    data["z"] = g::DIM_Z;
    
    std::ifstream stream(sDynamicConfigFiles.get_simulations_filename());
    json simu;
    stream >> simu;
    stream.close();

    data["file"] = sDynamicConfigFiles.get_simulations_filename();
    data["iterations"] = simu["iterations"];
#ifdef ACTIVE_PROMISES
    data["active"] = true;
#else
    data["active"] = false;
#endif

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

    Runner runner(sDynamicConfigFiles.get_simulations_filename());

    init_start_matrix_once();
    init_from_start_matrix(g_expected_matrix);
    // assert_matrix_equals(g_start_matrix, g_expected_matrix->get_matrix());

    // assert_matrix_equals(g_reordered_start_matrix, g_expected_reordered_matrix->get_matrix());

    init_expected_matrix_once();

    // assert_okay_reordered_compute();

    runner.run();
    runner.dump();

    log_general_data(DynamicConfig::_instance()._files.parameters_file());

    return 0;
}
