#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <chrono>
#include <fstream>
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
using SteadyClock = std::chrono::steady_clock;
using TP = std::chrono::time_point<SteadyClock>;

void producer(SmartFIFO<int>*, int, int);
void consumer(SmartFIFO<int>*, int, int);

void producer(NaiveQueue<int>*, Ringbuffer<int>*, int, int);
void consumer(NaiveQueue<int>*, Ringbuffer<int>*, int, int);

void producer(NaiveQueueImpl<int>*, int, int);
void consumer(NaiveQueueImpl<int>*, int, int);

unsigned long long diff(TP const& begin, TP const& end) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
}

enum RunType {
    SMART_FIFO,
    NAIVE,
    MASTER,
    MASTER_RECONFIGURE,
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

            using fn = void(*)(NaiveQueueImpl<int>*, int, int);

            NaiveQueueMaster<int> queue(10000000000ULL, _producers_loops.size());
            std::vector<NaiveQueueImpl<int>*> queues;

            TP begin = SteadyClock::now();
            for (auto const& [glob_loops, work_loops, config]: _producers_loops) {
                NaiveQueueImpl<int>* impl = queue.view(config._start_step, false, 0, 0);
                _threads.push_back(std::thread((fn)producer, impl, glob_loops, work_loops));
                queues.push_back(impl);
            }

            for (auto const& [glob_loops, work_loops, config]: _consumers_loops) {
                NaiveQueueImpl<int>* impl = queue.view(config._start_step, false, 0, 0);
                _threads.push_back(std::thread((fn)consumer, impl, glob_loops, work_loops));
                queues.push_back(impl);
            }

            for (std::thread& thread: _threads) {
                thread.join();
            }

            TP end = SteadyClock::now();

            for (NaiveQueueImpl<int>* impl: queues) {
                delete impl;
            }

            _threads.clear();

            return diff(begin, end);
        }

        unsigned long long run_naive_master_reconfigure() {
            if (_producers_loops.empty() || _consumers_loops.empty()) {
                throw std::runtime_error("Must have at least one producer and one consumer");
            }

            using fn = void(*)(NaiveQueueImpl<int>*, int, int);

            NaiveQueueMaster<int> queue(10000000000ULL, _producers_loops.size());
            std::vector<NaiveQueueImpl<int>*> queues;

            TP begin = SteadyClock::now();
            for (auto const& [glob_loops, work_loops, config]: _producers_loops) {
                NaiveQueueImpl<int>* impl = queue.view(config._start_step, true, config._change_after, config._new_step);
                _threads.push_back(std::thread((fn)producer, impl, glob_loops, work_loops));
                queues.push_back(impl);
            }

            for (auto const& [glob_loops, work_loops, config]: _consumers_loops) {
                NaiveQueueImpl<int>* impl = queue.view(config._start_step, true, config._change_after, config._new_step);
                _threads.push_back(std::thread((fn)consumer, impl, glob_loops, work_loops));
                queues.push_back(impl);
            }

            for (std::thread& thread: _threads) {
                thread.join();
            }

            TP end = SteadyClock::now();

            for (NaiveQueueImpl<int>* impl: queues) {
                delete impl;
            }

            _threads.clear();

            return diff(begin, end);

        }

    private:
        std::vector<std::tuple<int, int, SmartFIFOConfig>> _producers_loops;
        std::vector<std::tuple<int, int, SmartFIFOConfig>> _consumers_loops;
        std::vector<std::thread> _threads;
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
        ("reconfigure", "Run original version with integrated local buffer and reconfiguration");

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
}

int main(int argc, char** argv) {
    Args args;
    parse_args(argc, argv, args);

    /* sol::state lua;
    init_lua(lua);

    lua.script_file(args._filename); */

    LuaRun run;
    parse_json(args._filename, run);
    /* run.add_producer(4000000, 200, SmartFIFOConfig(1, 0, 0));
    run.add_consumer(4000000, 200, SmartFIFOConfig(1, 0, 0)); */

    std::ofstream stream(args._output, std::ios::app);
    for (RunType run_type: args._run_types) {
        unsigned long long time = 0;
        std::string type;

        switch (run_type) {
            case SMART_FIFO:
                time = run.run();
                type = "SmartFIFO";
                break;

            case NAIVE:
                time = run.run_queue();
                type = "Naive";
                break;

            case MASTER:
                time = run.run_naive_master();
                type = "Master";
                break;

            case MASTER_RECONFIGURE:
                time = run.run_naive_master_reconfigure();
                type = "MasterReconfigure";
                break;

            default:
                throw std::runtime_error("What ?");
        }

        stream << type << " " << time / 1000000000.f << std::endl;
    }

    return 0;
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

void consumer(NaiveQueue<int>* queue, Ringbuffer<int>* buffer, int glob_loops, int work_loops) {
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

void producer(NaiveQueueImpl<int>* queue, int glob_loops, int work_loops) {
    int i = 0;
    // TP out_begin = SteadyClock::now();
    // unsigned long long sum = 0;
    for (; i < glob_loops; ++i) {
        // TP work_begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        // sum += diff(work_begin, SteadyClock::now());

        queue->push(i);
    }

    // unsigned long long f = diff(out_begin, SteadyClock::now());
    queue->terminate();
    // printf("Total = %llu, work = %llu, diff = %llu", f, sum, f - sum);
    // printf("Total = %llu\n", f);
    // printf("Producer finished after %d iterations, expected %d\n", i, glob_loops);
}

void consumer(NaiveQueueImpl<int>* queue, int glob_loops, int work_loops) {
    int i = 0;
    // TP out_begin = SteadyClock::now();
    // unsigned long long sum = 0;
    while (true) {
        std::optional<int> result = queue->pop();
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
