#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <cstdint>
#include <cstdlib>
#include <ctime>

#include <atomic>
#include <chrono>
#include <future>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#include "naive_queue.hpp"
#include "smart_fifo.h"

#include <boost/program_options.hpp>
#include "nlohmann/json.hpp"
#include <sol/sol.hpp>

namespace po = boost::program_options;

using json = nlohmann::json;

static std::atomic<unsigned int> CPU_ID;

void producer(SmartFIFO<int>*, int, int);
void consumer(SmartFIFO<int>*, int, int);

void producer(NaiveQueue<int>*, Ringbuffer<int>*, int, int);
void consumer(NaiveQueue<int>*, Ringbuffer<int>*, int, int);

enum RunType {
    SMART_FIFO,
    NAIVE,
    MASTER,
    MASTER_RECONFIGURE,
    MASTER_AUTO_RECONFIGURE,
};

struct Args {
    std::string _filename;
    std::string _output;
    std::vector<RunType> _run_types;
};

struct SmartFIFOConfig {
    SmartFIFOConfig(unsigned int start, unsigned int change, unsigned int new_) : 
        _start_step(start), _change_after(change), _new_step(new_) { }
    unsigned int _start_step;
    unsigned int _change_after;
    unsigned int _new_step;
};

struct StupidObject {
    int32_t v1;
    int8_t v2;
    int64_t v3;
    int32_t v4[10];
    int8_t v5;
    int16_t v6;
    void* v7;
    int64_t v8[12];
    void* v9[13];
    char v10[50];
    int16_t v11[30];
};

inline void MakeStupidObject(StupidObject& obj) __attribute__((always_inline));

void producer(NaiveQueueImpl<StupidObject>*, int, int);
void consumer(NaiveQueueImpl<StupidObject>*, int, int);

void producer(NaiveQueueImpl<StupidObject>*, Observer<StupidObject>*, int, int, int);
void consumer(NaiveQueueImpl<StupidObject>*, Observer<StupidObject>*, int, int, int);

void producer(NaiveQueueImpl<int>*, Observer<int>*, int, int, int);
void consumer(NaiveQueueImpl<int>*, Observer<int>*, int, int, int);

static int second_sync_size = 50;

class LuaRun {
    public:
        LuaRun() { }

        void add_producer(int glob_loops, int work_loops, SmartFIFOConfig const& config) {
            _producers_loops.push_back({glob_loops, work_loops, config});
        }

        void add_consumer(int glob_loops, int work_loops, SmartFIFOConfig const& config) {
            _consumers_loops.push_back({glob_loops, work_loops, config});
        }

        unsigned long long run() {
            if (_producers_loops.empty() || _consumers_loops.empty()) {
                throw std::runtime_error("Must have at least one producer and one consumer");
            }

            using fn = void(*)(SmartFIFO<int>*, int, int);
            SmartFIFOImpl<int> fifo;

            TP begin = SteadyClock::now();
            for (auto const& [glob_loops, work_loops, config]: _producers_loops) {
                SmartFIFO<int>* view = fifo.view(true, config._start_step, config._change_after != 0, config._change_after, config._new_step);
                _threads.push_back(std::thread((fn)producer, view, glob_loops, work_loops));
            }

            for (auto const& [glob_loops, work_loops, config]: _consumers_loops) {
                SmartFIFO<int>* view = fifo.view(false, config._start_step, config._change_after != 0, config._change_after, config._new_step);
                _threads.push_back(std::thread((fn)consumer, view, glob_loops, work_loops));
            }

            for (std::thread& thread: _threads) {
                thread.join();
            }

            TP end = SteadyClock::now();
            _threads.clear();

            // log_time("SmartFIFO", diff(begin, end) / 1000000000.f);
            // write(sock, stream.str().c_str(), stream.str().size());
            // return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
            return diff(begin, end);
        }

        unsigned long long run_queue() {
            if (_producers_loops.empty() || _consumers_loops.empty()) {
                throw std::runtime_error("Must have at least one producer and one consume");
            }

            using fn = void(*)(NaiveQueue<int>*, Ringbuffer<int>*, int, int);

            NaiveQueue<int> queue(10000000000ULL, _producers_loops.size());
            std::vector<Ringbuffer<int>*> ringbuffers;

            TP begin = SteadyClock::now();
            for (auto const& [glob_loops, work_loops, config]: _producers_loops) {
                Ringbuffer<int>* buffer = new Ringbuffer<int>(config._start_step);
                _threads.push_back(std::thread((fn)producer, &queue, buffer, glob_loops, work_loops));
                ringbuffers.push_back(buffer);
            }

            for (auto const& [glob_loops, work_loops, config]: _consumers_loops) {
                Ringbuffer<int>* buffer = new Ringbuffer<int>(config._start_step);
                _threads.push_back(std::thread((fn)consumer, &queue, buffer, glob_loops, work_loops));
                ringbuffers.push_back(buffer);
            }

            for (std::thread& thread: _threads) {
                thread.join();
            }

            TP end = SteadyClock::now();

            for (Ringbuffer<int>* buffer: ringbuffers) {
                delete buffer;
            }

            _threads.clear();

            return diff(begin, end);
        }

        unsigned long long run_naive_master() {
            if (_producers_loops.empty() || _consumers_loops.empty()) {
                throw std::runtime_error("Must have at least one producer and one consumer");
            }

            // using fn = void(*)(NaiveQueueImpl<int>*, int, int);
            using fn = void(*)(NaiveQueueImpl<StupidObject>*, int, int);

            // NaiveQueueMaster<int> queue(10000000000ULL, _producers_loops.size());
            NaiveQueueMaster<StupidObject> queue(100000000ULL, _producers_loops.size());
            // std::vector<NaiveQueueImpl<int>*> queues;
            std::vector<NaiveQueueImpl<StupidObject>*> queues;

            TP begin = SteadyClock::now();
            for (auto const& [glob_loops, work_loops, config]: _producers_loops) {
                // NaiveQueueImpl<int>* impl = queue.view(config._start_step, false, 0, 0);
                NaiveQueueImpl<StupidObject>* impl = queue.view(config._start_step, false, 0, 0, 0, 0);
                _threads.push_back(std::thread((fn)producer, impl, glob_loops, work_loops));
                queues.push_back(impl);
            }

            for (auto const& [glob_loops, work_loops, config]: _consumers_loops) {
                // NaiveQueueImpl<int>* impl = queue.view(config._start_step, false, 0, 0);
                NaiveQueueImpl<StupidObject>* impl = queue.view(config._start_step, false, 0, 0, 0, 0);
                _threads.push_back(std::thread((fn)consumer, impl, glob_loops, work_loops));
                queues.push_back(impl);
            }

            for (std::thread& thread: _threads) {
                thread.join();
            }

            TP end = SteadyClock::now();

            // for (NaiveQueueImpl<int>* impl: queues) {
            for (NaiveQueueImpl<StupidObject>* impl: queues) {
                delete impl;
            }

            _threads.clear();

            return diff(begin, end);
        }

        unsigned long long run_naive_master_reconfigure() {
            if (_producers_loops.empty() || _consumers_loops.empty()) {
                throw std::runtime_error("Must have at least one producer and one consumer");
            }

            // using fn = void(*)(NaiveQueueImpl<int>*, int, int);
            using fn = void(*)(NaiveQueueImpl<StupidObject>*, int, int);

            // NaiveQueueMaster<int> queue(10000000000ULL, _producers_loops.size());
            NaiveQueueMaster<StupidObject> queue(100000000ULL, _producers_loops.size());
            // std::vector<NaiveQueueImpl<int>*> queues;
            std::vector<NaiveQueueImpl<StupidObject>*> queues;
            std::vector<std::packaged_task<void()>> tasks;

            TP begin = SteadyClock::now();
            for (auto const& [glob_loops, work_loops, config]: _producers_loops) {
                // NaiveQueueImpl<int>* impl = queue.view(config._start_step, true, config._change_after, config._new_step);
                NaiveQueueImpl<StupidObject>* impl = queue.view(config._start_step, true, config._change_after, config._new_step, 0, 0);
                // _threads.push_back(std::thread((fn)producer, impl, glob_loops, work_loops));
                tasks.push_back(std::packaged_task<void()>(std::bind((fn)producer, impl, glob_loops, work_loops)));
                queues.push_back(impl);
            }

            for (auto const& [glob_loops, work_loops, config]: _consumers_loops) {
                // NaiveQueueImpl<int>* impl = queue.view(config._start_step, true, config._change_after, config._new_step);
                NaiveQueueImpl<StupidObject>* impl = queue.view(config._start_step, true, config._change_after, config._new_step, 0, 0);
                // _threads.push_back(std::thread((fn)consumer, impl, glob_loops, work_loops));
                tasks.push_back(std::packaged_task<void()>(std::bind((fn)consumer, impl, glob_loops, work_loops)));
                queues.push_back(impl);
            }

            for (std::packaged_task<void()>& task: tasks) {
                _threads.push_back(std::thread(std::move(task)));
            }

            for (std::thread& thread: _threads) {
                thread.join();
            }

            TP end = SteadyClock::now();

            // for (NaiveQueueImpl<int>* impl: queues) {
            for (NaiveQueueImpl<StupidObject>* impl: queues) {
                delete impl;
            }

            _threads.clear();

            return diff(begin, end);

        }

        std::tuple<unsigned long long, json> run_master_auto_reconfigure() {
            if (_producers_loops.empty() || _consumers_loops.empty()) {
                throw std::runtime_error("Must have at least one producer and one consumer");
            }

            // using fn = void(*)(NaiveQueueImpl<StupidObject>*, Observer<StupidObject>*, int, int, int);
            using fn = void(*)(NaiveQueueImpl<int>*, Observer<int>*, int, int, int);

            std::vector<std::packaged_task<void()>> tasks;
            /* NaiveQueueMaster<StupidObject> queue(100000000ULL, _producers_loops.size());
            Observer<StupidObject> observer(400, std::get<0>(_producers_loops[0]), _producers_loops.size() + _consumers_loops.size());
            std::vector<NaiveQueueImpl<StupidObject>*> queues; */

            NaiveQueueMaster<int> queue(100000000ULL, _producers_loops.size());
            Observer<int> observer(std::get<0>(_producers_loops[0]), _producers_loops.size() + _consumers_loops.size());
            std::vector<NaiveQueueImpl<int>*> queues;

            TP begin = SteadyClock::now();
            for (auto const& [glob_loops, work_loops, config]: _producers_loops) {
                // NaiveQueueImpl<StupidObject>* impl = queue.view(config._start_step, false, config._change_after, config._new_step);
                NaiveQueueImpl<int>* impl = queue.view(config._start_step, false, config._change_after, config._new_step, _n_samples, second_sync_size);
                observer.add_producer(impl);
                tasks.push_back(std::packaged_task<void()>(std::bind((fn)producer, impl, &observer, glob_loops, work_loops, _n_samples)));
                queues.push_back(impl);
            }

            for (auto const& [glob_loops, work_loops, config]: _consumers_loops) {
                // NaiveQueueImpl<StupidObject>* impl = queue.view(config._start_step, false, config._change_after, config._new_step);
                NaiveQueueImpl<int>* impl = queue.view(config._start_step, false, config._change_after, config._new_step, _n_samples, second_sync_size);
                observer.add_consumer(impl);
                tasks.push_back(std::packaged_task<void()>(std::bind((fn)consumer, impl, &observer, glob_loops, work_loops, _n_samples)));
                queues.push_back(impl);
            }

            observer.set_prod_size(_n_samples);
            observer.set_cons_size(_n_samples);
            observer.set_cost_p_cost_s_size(_n_samples);
            observer.set_cost_s_size(second_sync_size);

            for (auto& task: tasks) {
                _threads.push_back(std::thread(std::move(task)));
            }

            for (std::thread& thread: _threads) {
                thread.join();
            }

            TP end = SteadyClock::now();

            for (auto impl: queues) {
                delete impl;
            }

            _threads.clear();

            return { diff(begin, end), observer.serialize() } ;
        }

        void set_n_samples(unsigned int samples) {
            _n_samples = samples;
        }

    private:
        std::vector<std::tuple<int, int, SmartFIFOConfig>> _producers_loops;
        std::vector<std::tuple<int, int, SmartFIFOConfig>> _consumers_loops;
        std::vector<std::thread> _threads;
        unsigned int _n_samples;
};

void parse_args(int argc, char** argv, Args& args) {
    po::options_description options("All options");
    options.add_options()
        ("help,h", "Display this help and exit")
        ("file,f", po::value<std::string>(), "Name of the JSON file to process")
        ("output,o", po::value<std::string>(), "Name of the output file to which results will be written")
        ("smart", "Run SmartFIFO")
        ("naive", "Run original naive version")
        ("master", "Run original version with integrated local buffer")
        ("reconfigure", "Run original version with integrated local buffer and reconfiguration")
        ("auto-reconfigure", "Run original version with integrated local buffer and auto reconfiguration at runtime");

    po::variables_map vm;
    po::command_line_parser parser(argc, argv);
    parser.options(options);
    po::store(parser.run(), vm);

    if (vm.count("help")) {
        std::cout << options << std::endl;
        exit(0);
    }

    if (!vm.count("file")) {
        std::cout << "-f or --file required" << std::endl;
        exit(1);
    }

    if (!vm.count("output")) {
        std::cout << "No output file specified, will write to stdout" << std::endl;
    } else {
        args._output = vm["output"].as<std::string>();
    }

    if (vm.count("smart")) {
        args._run_types.push_back(SMART_FIFO);
    } 

    if (vm.count("naive")) {
        args._run_types.push_back(NAIVE);
    } 

    if (vm.count("master")) {
        args._run_types.push_back(MASTER);
    }

    if (vm.count("reconfigure")) {
        args._run_types.push_back(MASTER_RECONFIGURE);
    }

    if (vm.count("auto-reconfigure")) {
        args._run_types.push_back(MASTER_AUTO_RECONFIGURE);
    }

    args._filename = vm["file"].as<std::string>();
}

void init_lua(sol::state& lua) {
    lua.open_libraries(sol::lib::base, sol::lib::io, sol::lib::math, sol::lib::table, sol::lib::package);

    sol::usertype<SmartFIFOConfig> smart_fifo_config_dt = 
        lua.new_usertype<SmartFIFOConfig>("SmartFIFOConfig", sol::constructors<SmartFIFOConfig(int, int, int)>());

    sol::usertype<LuaRun> lua_run_datatype = lua.new_usertype<LuaRun>("LuaRun");
    lua_run_datatype["add_producer"] = &LuaRun::add_producer;
    lua_run_datatype["add_consumer"] = &LuaRun::add_consumer;
    lua_run_datatype["run"] = &LuaRun::run;
    lua_run_datatype["run_queue"] = &LuaRun::run_queue;
    lua_run_datatype["run_naive_master"] = &LuaRun::run_naive_master;
}

void parse_json(std::string const& filename, LuaRun& run) {
    std::ifstream stream(filename);
    json j;
    stream >> j;
    stream.close();

    auto producers = j["producers"];
    auto consumers = j["consumers"];

    for (auto const& producer: producers) {
        run.add_producer(producer["iterations"], producer["work"], SmartFIFOConfig(producer["start"], producer["threshold"], producer["new"]));
    }

    for (auto const& consumer: consumers) {
        run.add_consumer(consumer["iterations"], consumer["work"], SmartFIFOConfig(consumer["start"], consumer["threshold"], consumer["new"]));
    }

    if (j.contains("samples")) {
        run.set_n_samples(j["samples"]);
    }
}

int main(int argc, char** argv) {
    // std::cout << sizeof(StupidObject) << std::endl;

    /* NaiveQueueMaster<StupidObject> master(10000L, 1);
    NaiveQueueImpl<StupidObject>* queue = master.view(2000, false, 0, 0);

    unsigned long long sum = 0;
    for (int i = 0; i < 1000; ++i) {
        StupidObject obj;
        MakeStupidObject(obj);
        TP begin = SteadyClock::now();
        queue->push(obj);
        sum += diff(begin, SteadyClock::now());
    }
    std::cout << "Average is " << sum / 1000 << std::endl;

    return 0; */
    Args args;
    parse_args(argc, argv, args);

    CPU_ID.store(1, std::memory_order_relaxed);

    /* sol::state lua;
    init_lua(lua);

    lua.script_file(args._filename); */

    LuaRun lrun;
    parse_json(args._filename, lrun);
    /* run.add_producer(4000000, 200, SmartFIFOConfig(1, 0, 0));
    run.add_consumer(4000000, 200, SmartFIFOConfig(1, 0, 0)); */

    std::ofstream stream(args._output, std::ios::app);
    std::vector<json> runs;
    for (RunType run_type: args._run_types) {
        unsigned long long time = 0;
        json run;
        std::string type;

        switch (run_type) {
            case SMART_FIFO:
                time = lrun.run();
                type = "SmartFIFO";
                break;

            case NAIVE:
                time = lrun.run_queue();
                type = "Naive";
                break;

            case MASTER:
                time = lrun.run_naive_master();
                type = "Master";
                break;

            case MASTER_RECONFIGURE:
                time = lrun.run_naive_master_reconfigure();
                type = "MasterReconfigure";
                break;

            case MASTER_AUTO_RECONFIGURE: {
                json observer;
                std::tie(time, observer) = lrun.run_master_auto_reconfigure();
                run["observer"] = observer;
                type = "MasterAutoReconfigure";
                break;
            }

            default:
                throw std::runtime_error("What ?");
        }

        run["type"] = type;
        run["time"] = time / 1000000000.f;
        runs.push_back(run);
        // stream << type << " " << time / 1000000000.f << std::endl;
    }

    json runs_json;
    runs_json["runs"] = runs;
    stream << runs_json << std::endl;

    return 0;
}

static std::mt19937_64 gen(12);

#define RAND_INIT(val) val = randv<decltype(val)>()

template<typename T>
std::decay_t<T> randv() {
    return gen() % std::numeric_limits<std::decay_t<T>>::max();
}

void MakeStupidObject(StupidObject& obj) {
    // RAND_INIT(obj.v1);
    // RAND_INIT(obj.v2);
    // RAND_INIT(obj.v3);
    obj.v1 = obj.v2 = obj.v3 = 0;
    for (int i = 0; i < 10; ++i) {
        // RAND_INIT(obj.v4[i]);
        obj.v4[i] = 0;
    }
    // RAND_INIT(obj.v5);
    // RAND_INIT(obj.v6);
    obj.v5 = obj.v6 = 0;
    // obj.v7 = nullptr;
    obj.v7 = nullptr;
    for (int i = 0; i < 12; ++i) {
        // RAND_INIT(obj.v8[i]);
        obj.v8[i] = 0;
    }
    for (int i = 0; i < 13; ++i) {
        // obj.v9[i] = nullptr;
        obj.v9[i] = nullptr;
    }
    for (int i = 0; i < 50; ++i) {
        // RAND_INIT(obj.v10[i]);
        obj.v10[i] = 0;
    }
    for (int i = 0; i < 30; ++i) {
        // RAND_INIT(obj.v11[i]);
        obj.v11[i] = 0;
    }
}

void producer(SmartFIFO<int>* fifo, int global_loops, int work_loops) {
    // unsigned long long work = 0;
    // TP out_begin = SteadyClock::now();

    for (int i = 0; i < global_loops; ++i) {
        // TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        // TP end = SteadyClock::now();
        // work += std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();

        /* std::ostringstream stream;
        stream << "FIFO " << fifo << " pushing value " << i << std::endl;
        std::cout << stream.str(); */

        // TP begin = SteadyClock::now(), end;
        fifo->push(i);
        // end = SteadyClock::now();

        // std::cout << diff(begin, end) << std::endl;
    }
    
    // std::ostringstream stream;
    // stream << "[Smart][Prod] " << diff(out_begin, SteadyClock::now()) << ", " << work << std::endl;
    // std::cout << stream.str();

    fifo->terminate_producer();
}

void consumer(SmartFIFO<int>* fifo, int global_loops, int work_loops) {
    // TP out_begin = SteadyClock::now();
    // unsigned long long work = 0;

    for (int i = 0; i < global_loops; ++i) {
        std::optional<int> value;
        // TP begin = SteadyClock::now(), end;
        fifo->pop_copy(value);
        // end = SteadyClock::now();
        if (!value) {
            break;
        }

        // std::cout << diff(begin, end) << std::endl;

        /* std::ostringstream stream;
        stream << "FIFO " << fifo << " poped value " << *value << std::endl;
        std::cout << stream.str(); */

        // TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        // work += diff(begin, SteadyClock::now());
    }

    // std::ostringstream stream;
    // stream << "[Smart][Cons] " << diff(out_begin, SteadyClock::now()) << ", " << work << std::endl;
    // std::cout << stream.str();
}

void producer(NaiveQueue<int>* queue, Ringbuffer<int>* buffer, int glob_loops, int work_loops) {
    // unsigned long long work = 0;
    // TP out_begin = SteadyClock::now();
    for (int i = 0; i < glob_loops; ++i) {
        // TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        // work += diff(begin, SteadyClock::now());

        buffer->push(i);
        if (buffer->full()) {
            queue->enqueue(buffer, buffer->size());
        }
    }

    while (!buffer->empty()) {
        queue->enqueue(buffer, buffer->size());
    }
    
    /* std::ostringstream stream;
    stream << "[Naive][Prod] " << diff(out_begin, SteadyClock::now()) << ", " << work << std::endl;
    std::cout << stream.str(); */

    queue->terminate();
}

void consumer(NaiveQueue<int>* queue, Ringbuffer<int>* buffer, int, int work_loops) {
    // unsigned long long work = 0;
    // TP out_begin = SteadyClock::now();
    while (true) {
        int res = 0;
        if (buffer->empty()) {
            res = queue->dequeue(buffer, buffer->size());
        }

        if (res < 0) {
            break;
        }

        buffer->pop();

        // TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        // work += diff(begin, SteadyClock::now());
    }

    // std::ostringstream stream;
    // stream << "[Naive][Cons] " << diff(out_begin, SteadyClock::now()) << ", " << work << std::endl;
    // std::cout << stream.str();
}

// void producer(NaiveQueueImpl<int>* queue, int glob_loops, int work_loops) {
void producer(NaiveQueueImpl<StupidObject>* queue, int glob_loops, int work_loops) {
    int i = 0;
    // TP out_begin = SteadyClock::now();
    // unsigned long long sum = 0;
    for (; i < glob_loops; ++i) {
        // TP work_begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        // sum += diff(work_begin, SteadyClock::now());

        StupidObject obj;
        MakeStupidObject(obj);
        queue->push(obj);

        // queue->push(i);
    }

    // unsigned long long f = diff(out_begin, SteadyClock::now());
    queue->terminate();
    // printf("Total = %llu, work = %llu, diff = %llu", f, sum, f - sum);
    // printf("Total = %llu\n", f);
    // printf("Producer finished after %d iterations, expected %d\n", i, glob_loops);
}

// void consumer(NaiveQueueImpl<int>* queue, int glob_loops, int work_loops) {
void consumer(NaiveQueueImpl<StupidObject>* queue, int, int work_loops) {
    // TP out_begin = SteadyClock::now();
    // unsigned long long sum = 0;
    while (true) {
        // std::optional<int> result = queue->pop();
        auto [result, lock, critical, unlock] = queue->pop();
        if (!result) {
            break;
        }

        // printf("Consumer received %d\n", *result);
        // TP work_begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        // sum += diff(work_begin, SteadyClock::now());
    }
    // unsigned long long f = diff(out_begin, SteadyClock::now());

    // printf("Total = %llu, work = %llu, diff = %llu", f, sum, f - sum);
    // printf("Consumer finished after %d iterations, expected %d\n", i, glob_loops);
}

void producer(NaiveQueueImpl<StupidObject>* queue, Observer<StupidObject>* observer, int glob_loops, int work_loops, int timed_loops) {
    // int cpu_id = sched_getcpu();
    /* cpu_set_t set;
    CPU_ZERO(&set);
    int cpu_id = (CPU_ID += 2);
    CPU_SET(cpu_id, &set);
    sched_setaffinity(0, sizeof(set), &set); */
    int i = 0;

    // First batch
    int limit = queue->get_step() * timed_loops;
    for (; i < limit && i < glob_loops; ++i) {
        TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        auto d = diff(begin, SteadyClock::now());

        StupidObject obj;
        MakeStupidObject(obj);
        auto [push_time, enqueue_time, lock, critical, unlock] = queue->timed_push(observer, obj);
        if (enqueue_time) {
            // observer->add_cost_p_cost_s_time(queue, push_time, lock, critical, unlock); 
            observer->add_producer_time(queue, d);
        }
    }

    bool reconfigured = false;
    while (!reconfigured && i < glob_loops) {
        for (volatile int k = 0; k < work_loops; ++k) {
            ;
        }

        StupidObject obj;
        MakeStupidObject(obj);
        // auto [push_time, enqueue_time, lock, critical, unlock] =;
        queue->timed_push(observer, obj);
        /* if (enqueue_time) {
            reconfigured = observer->add_cost_s_time(queue, lock, critical, unlock);
        } */
        ++i;
    }

    for (; i < glob_loops; ++i) {
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }

        StupidObject obj;
        MakeStupidObject(obj);
        /* auto [push_time, enqueue_time, lock, critical, unlock] = queue->timed_push(obj);
        if (enqueue_time != 0) {
            str << enqueue_time << " ";
        } */
        queue->push(obj);
    }

    // str << std::endl;

    queue->terminate();

    /* int now = sched_getcpu();
    if (now != cpu_id) {
        printf("CPU discrepancy: e = %d, f = %d\n", cpu_id, now);
    } else {
        printf("CPU %d\n", now);
    } */
    // printf("%s\n", str.str().c_str());
    // int cpu_id2 = sched_getcpu();
    // printf("Producer: cpu_id = %d, cpu_id2 = %d\n", cpu_id, cpu_id2);
}

void consumer(NaiveQueueImpl<StupidObject>* queue, Observer<StupidObject>* observer, int glob_loops, int work_loops, int timed_loops) {
    // int cpu_id = sched_getcpu();
    /* cpu_set_t set;
    CPU_ZERO(&set);
    int cpu_id = (CPU_ID += 2);
    CPU_SET(cpu_id, &set);
    sched_setaffinity(0, sizeof(set), &set); */
    int i = 0;
    int limit = queue->get_step() * timed_loops ;
    for (; i < limit && i < glob_loops; ++i) {
        auto [result, lock, critical, unlock] = queue->pop();
        if (!result) {
            break;
        }

        TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        observer->add_consumer_time(queue, diff(begin, SteadyClock::now()));
    }

    while (true) {
        auto [result, lock, critical, unlock] = queue->pop();
        if (!result) {
            break;
        }

        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
    }

    /* int now = sched_getcpu();
    if (now != cpu_id) {
        printf("CPU discrepancy: e = %d, f = %d\n", cpu_id, now);
    } else {
        printf("CPU %d\n", now);
    } */
    // int cpu_id2 = sched_getcpu();
    // printf("Consumer: cpu_id = %d, cpu_id2 = %d\n", cpu_id, cpu_id2);
}

void producer(NaiveQueueImpl<int>* queue, Observer<int>* observer, int glob_loops, int work_loops, int timed_loops) {
    int i = 0;
    // std::ostringstream stream;

    // First batch
    int limit = queue->get_step() * timed_loops;
    int count = 0;
    // queue->set_sync_limit(100);
    for (; i < glob_loops; ++i) {
        TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        auto d = diff(begin, SteadyClock::now());

        /* StupidObject obj;
        MakeStupidObject(obj); */
        auto sync = queue->generic_push(observer, i);
        if (sync && count < limit) {
            // stream << push_time << "," << enqueue_time << "," << lock << "," << critical << "," << unlock << std::endl;
            // observer->add_cost_p_cost_s_time(queue, push_time, lock, critical, unlock);
            ++count;
            observer->add_producer_time(queue, d);
            // observer->add_critical_section_data(queue, lock, critical, unlock);
        }
    }

    // stream << std::endl;
    
    // queue->reset_sync_count();

    // bool reconfigured = false;
    // int j = 0;
    // while (i < glob_loops) {
    //    for (volatile int k = 0; k < work_loops; ++k) {
    //        ;
    //    }

        /* StupidObject obj;
        MakeStupidObject(obj); */
        /* auto [push_time, enqueue_time, lock, critical, unlock] = */ 
    //    queue->generic_push(observer, i);
        /* if (enqueue_time) {
            // stream << push_time << "," << enqueue_time << "," << lock << "," << critical << "," << unlock << std::endl;
            reconfigured = observer->add_cost_s_time(queue, lock, critical, unlock);
        } */
    //    ++i;
    //}

    // for (; i < glob_loops; ++i) {
    //    for (volatile int j = 0; j < work_loops; ++j) {
    //        ;
    //    }

        /* StupidObject obj;
        MakeStupidObject(obj); */
    //    queue->push(i);
    // }

    // std::cout << stream.str() << std::endl;
    queue->terminate();
}

void consumer(NaiveQueueImpl<int>* queue, Observer<int>* observer, int glob_loops, int work_loops, int timed_loops) {
    int i = 0;
    int limit = queue->get_step() * timed_loops ;
    for (; i < limit && i < glob_loops; ++i) {
        auto [result, lock, critical, unlock] = queue->pop();
        if (!result) {
            break;
        }

        if (critical) {
            observer->add_critical_section_data(queue, lock, critical, unlock);
        }
        TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        observer->add_consumer_time(queue, diff(begin, SteadyClock::now()));
    }

    // printf("Producer: first part OK\n");
    while (true) {
        auto [result, lock, critical, unlock] = queue->pop();
        if (!result) {
            break;
        }

        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
    }
}

void producer(NaiveQueueImpl<int64_t>* queue, Observer<int64_t>* observer, int glob_loops, int work_loops, int timed_loops) {
    int i = 0;
    // std::ostringstream stream;

    // First batch
    int limit = queue->get_step() * timed_loops;
    int count = 0;
    // queue->set_sync_limit(100);
    for (; i < glob_loops; ++i) {
        TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        auto d = diff(begin, SteadyClock::now());

        /* StupidObject obj;
        MakeStupidObject(obj); */
        auto sync = queue->generic_push(observer, (int64_t)i);
        if (sync && count < limit) {
            // stream << push_time << "," << enqueue_time << "," << lock << "," << critical << "," << unlock << std::endl;
            // observer->add_cost_p_cost_s_time(queue, push_time, lock, critical, unlock);
            observer->add_producer_time(queue, d);
            ++count;
            // observer->add_critical_section_data(queue, lock, critical, unlock);
        }
    }

    // stream << std::endl;

    // queue->reset_sync_count();

    // bool reconfigured = false;
    // int j = 0;
    // while (i < glob_loops) {
    //    for (volatile int k = 0; k < work_loops; ++k) {
    //        ;
    //    }

        /* StupidObject obj;
        MakeStupidObject(obj); */
        /* auto [push_time, enqueue_time, lock, critical, unlock] = */ 
    //    queue->generic_push(observer, i);
        /* if (enqueue_time) {
            // stream << push_time << "," << enqueue_time << "," << lock << "," << critical << "," << unlock << std::endl;
            reconfigured = observer->add_cost_s_time(queue, lock, critical, unlock);
        } */
    //    ++i;
    // }

    // for (; i < glob_loops; ++i) {
    //    for (volatile int j = 0; j < work_loops; ++j) {
    //        ;
    //    }

        /* StupidObject obj;
        MakeStupidObject(obj); */
    //     queue->push((int64_t)i);
    // }

    // std::cout << stream.str() << std::endl;
    queue->terminate();
}

void consumer(NaiveQueueImpl<int64_t>* queue, Observer<int64_t>* observer, int glob_loops, int work_loops, int timed_loops) {
    int i = 0;
    int limit = queue->get_step() * timed_loops ;
    for (; i < limit && i < glob_loops; ++i) {
        auto [result, lock, critical, unlock] = queue->pop();
        if (!result) {
            break;
        }

        if (critical) {
            observer->add_critical_section_data(queue, lock, critical, unlock);
        }
        TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        observer->add_consumer_time(queue, diff(begin, SteadyClock::now()));
    }

    // printf("Producer: first part OK\n");
    while (true) {
        auto [result, lock, critical, unlock] = queue->pop();
        if (!result) {
            break;
        }

        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
    }
}

