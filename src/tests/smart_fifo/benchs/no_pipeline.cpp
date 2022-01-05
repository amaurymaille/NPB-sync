#include <chrono>
#include <sstream>
#include <thread>
#include <vector>

#include "naive_queue.hpp"
#include "smart_fifo.h"

#include <boost/program_options.hpp>
#include <sol/sol.hpp>

namespace po = boost::program_options;

using SteadyClock = std::chrono::steady_clock;
using TP = std::chrono::time_point<SteadyClock>;

void producer(SmartFIFO<int>*, int, int);
void consumer(SmartFIFO<int>*, int, int);

void producer(NaiveQueue<int>*, Ringbuffer<int>*, int, int);
void consumer(NaiveQueue<int>*, Ringbuffer<int>*, int, int);

unsigned long long diff(TP const& begin, TP const& end) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
}

struct Args {
    std::string _filename;
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
            return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
        }

        unsigned long long run_queue() {
            if (_producers_loops.empty() || _consumers_loops.empty()) {
                throw std::runtime_error("Must have at least one producer and one consume");
            }

            using fn = void(*)(NaiveQueue<int>*, Ringbuffer<int>*, int, int);

            NaiveQueue<int> queue(1000000, _producers_loops.size());
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

            return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
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
        ("file,f", po::value<std::string>(), "Name of the Lua file to run");

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
}

int main(int argc, char** argv) {
    Args args;
    parse_args(argc, argv, args);

    sol::state lua;
    init_lua(lua);

    lua.script_file(args._filename);
    return 0;
}

void producer(SmartFIFO<int>* fifo, int global_loops, int work_loops) {
    unsigned long long work = 0;
    TP out_begin = SteadyClock::now();

    for (int i = 0; i < global_loops; ++i) {
        TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        TP end = SteadyClock::now();
        work += std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();

        /* std::ostringstream stream;
        stream << "FIFO " << fifo << " pushing value " << i << std::endl;
        std::cout << stream.str(); */

        // TP begin = SteadyClock::now(), end;
        fifo->push(i);
        // end = SteadyClock::now();

        // std::cout << diff(begin, end) << std::endl;
    }
    
    std::ostringstream stream;
    stream << "[Smart][Prod] " << diff(out_begin, SteadyClock::now()) << ", " << work << std::endl;
    std::cout << stream.str();

    fifo->terminate_producer();
}

void consumer(SmartFIFO<int>* fifo, int global_loops, int work_loops) {
    TP out_begin = SteadyClock::now();
    unsigned long long work = 0;

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

        TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        work += diff(begin, SteadyClock::now());
    }

    std::ostringstream stream;
    stream << "[Smart][Cons] " << diff(out_begin, SteadyClock::now()) << ", " << work << std::endl;
    std::cout << stream.str();
}

void producer(NaiveQueue<int>* queue, Ringbuffer<int>* buffer, int glob_loops, int work_loops) {
    unsigned long long work = 0;
    TP out_begin = SteadyClock::now();
    for (int i = 0; i < glob_loops; ++i) {
        TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        work += diff(begin, SteadyClock::now());

        buffer->push(i);
        if (buffer->full()) {
            queue->enqueue(buffer, buffer->size());
        }
    }

    while (!buffer->empty()) {
        queue->enqueue(buffer, buffer->size());
    }
    
    std::ostringstream stream;
    stream << "[Naive][Prod] " << diff(out_begin, SteadyClock::now()) << ", " << work << std::endl;
    std::cout << stream.str();

    queue->terminate();
}

void consumer(NaiveQueue<int>* queue, Ringbuffer<int>* buffer, int glob_loops, int work_loops) {
    unsigned long long work = 0;
    TP out_begin = SteadyClock::now();
    for (int i = 0; i < glob_loops; ++i) {
        int res = 0;
        if (buffer->empty()) {
            res = queue->dequeue(buffer, buffer->size());
        }

        if (res < 0) {
            break;
        }

        buffer->pop();

        TP begin = SteadyClock::now();
        for (volatile int j = 0; j < work_loops; ++j) {
            ;
        }
        work += diff(begin, SteadyClock::now());
    }

    std::ostringstream stream;
    stream << "[Naive][Cons] " << diff(out_begin, SteadyClock::now()) << ", " << work << std::endl;
    std::cout << stream.str();
}
