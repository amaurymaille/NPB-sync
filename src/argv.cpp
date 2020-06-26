#include <cstdio>

#include <iostream>
#include <string>

#include <boost/program_options.hpp>

#include "argv.h"
#include "dynamic_config.h"

namespace Options {
    namespace Synchronizers {
        static const char* sequential = "sequential";
        static const char* alt_bit = "alt_bit";
        static const char* counter = "counter";
        static const char* block = "block";
        static const char* block_plus = "block_plus";
        static const char* jline = "jline";
        static const char* jline_plus = "jline_plus";
        static const char* increasing_jline = "increasing_jline";
        static const char* increasing_jline_plus = "increasing_jline_plus";
        static const char* kline = "kline";
        static const char* kline_plus = "kline_plus";
        static const char* increasing_kline = "increasing_kline";
        static const char* increasing_kline_plus = "increasing_kline_plus";

        namespace Extra {
            static const char* increasing_jline_step = "increasing-jline-step";
            static const char* static_step_jline_plus = "static-step-jline-plus";
        }
    }

    namespace Files {
        static const char* runs_times_file = "runs-times-file";
        static const char* iterations_times_file = "iterations-times-file";
        static const char* simulations_file = "simulations-file";
    }
}

namespace po = boost::program_options;
namespace s = Options::Synchronizers;
namespace se = s::Extra;
namespace f = Options::Files;

static void process_patterns(DynamicConfig::SynchronizationPatterns& authorized,
                             po::variables_map const& vm);
static void populate_options(po::options_description& options);
static void process_files(DynamicConfig::Files& files,
                          po::variables_map const& vm);
static void init_outfile_if(const char* option, po::variables_map const& vm, std::function<void(std::ostream&)> f);
static void process_extra(DynamicConfig::Extra& extra, po::variables_map const& vm);

void parse_command_line(int argc, char** argv) {
    DynamicConfig& config = DynamicConfig::_instance();
    
    po::options_description options("All options");
    populate_options(options);

    po::variables_map vm;
    po::command_line_parser parser(argc, argv);
    parser.options(options);
    po::store(parser.run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << options << std::endl;
        exit(1);
    }

    process_files(config._files, vm);
    // process_patterns(config._patterns, vm);
    // process_extra(config._extra, vm);
}

void process_patterns(DynamicConfig::SynchronizationPatterns& authorized,
                      po::variables_map const& vm) {
    std::map<const char*, bool*> assoc;
    assoc[s::sequential] = &authorized._sequential;
    assoc[s::alt_bit] = &authorized._alt_bit;
    assoc[s::counter] = &authorized._counter;
    assoc[s::block] = &authorized._block;
    assoc[s::block_plus] = &authorized._block_plus;
    assoc[s::jline] = &authorized._jline;
    assoc[s::jline_plus] = &authorized._jline_plus;
    assoc[s::increasing_jline] = &authorized._increasing_jline;
    assoc[s::increasing_jline_plus] = &authorized._increasing_jline_plus;
    assoc[s::kline] = &authorized._kline;
    assoc[s::kline_plus] = &authorized._kline_plus;
    assoc[s::increasing_kline] = &authorized._increasing_kline;
    assoc[s::increasing_kline_plus] = &authorized._increasing_kline_plus;

    std::vector<const char*> names = {
        s::sequential, s::alt_bit, s::counter, s::block, s::block_plus, 
        s::jline, s::jline_plus, s::increasing_jline, s::increasing_jline_plus,
        s::kline, s::kline_plus, s::increasing_kline, s::increasing_kline_plus
    };

    for (const char* name: names) {
        if (vm.count(name)) {
            *(assoc[name]) = true;
        }
    }
}

void populate_options(po::options_description& options) {
    po::options_description generic("Generic options");
    generic.add_options()
        ("help,h", "Display this help and exit")
        (f::runs_times_file, po::value<std::string>(), "Path to the file in which the time for each run will be written")
        (f::iterations_times_file, po::value<std::string>(), "Path to the file in which the time for each iteration of each run will be written")
        (f::simulations_file, po::value<std::string>(), "Path to the file that contains the data for the runs");

    /* po::options_description synchro("Synchronization patterns without promises");
    synchro.add_options()
        (s::sequential, "Sequential (no synchronization at all)")
        (s::alt_bit, "Alternate bit synchronization (aka Fortran synchronization)")
        (s::counter, "Atomic counter synchronization");

    po::options_description promises("Synchronization patterns with promises");
    promises.add_options()
        (s::block, "Block synchronization")
        (s::block_plus, "Block synchronization (PromisePlus version)")
        (s::jline, "JLine synchronization")
        (s::jline_plus, "JLine synchronization (PromisePlus version)")
        (s::kline, "KLine synchronization")
        (s::kline_plus, "KLine synchronization (PromisePlus version)")
        (s::increasing_jline, "Increasing JLine synchronization")
        (s::increasing_jline_plus, "Increasing JLine synchronization (PromisePlus version)")
        (s::increasing_kline, "Increasing KLine synchronization")
        (s::increasing_kline_plus, "Increasing KLine synchronization (PromisePlus version)");

    po::options_description promises_extra("Arguments for synchronization patterns");
    promises_extra.add_options()
        (se::increasing_jline_step, po::value<unsigned int>()->default_value(1), "Minimum number of lines that need to be ready before synchronization occurs")
        (se::static_step_jline_plus, po::value<unsigned int>()->default_value(1), "Minimum number of lines that need to be ready before synchronization occurs (PromisePlus version)");

    options.add(synchro);
    options.add(promises);
    options.add(promises_extra); */
    options.add(generic);
}

void process_files(DynamicConfig::Files& files,
                   po::variables_map const& vm) {
    init_outfile_if(f::runs_times_file, vm, std::bind(&DynamicConfig::Files::set_runs_times_file, &files, std::placeholders::_1));
    init_outfile_if(f::iterations_times_file, vm, std::bind(&DynamicConfig::Files::set_iterations_times_file, &files, std::placeholders::_1));

    files.set_simulations_filename(vm[f::simulations_file].as<std::string>());
}

void init_outfile_if(const char* option, po::variables_map const& vm, std::function<void(std::ostream&)> f) {
    if (vm.count(option)) {
        f(*(new std::ofstream(vm[option].as<std::string>(), std::ios::out)));
    } else {
        f(std::cout);
    }
}

void process_extra(DynamicConfig::Extra& extra, po::variables_map const& vm) {
    if (vm.count(se::increasing_jline_step)) {
        extra._increasing_jline_step = vm[se::increasing_jline_step].as<unsigned int>();
    }

    if (vm.count(se::static_step_jline_plus)) {
        extra._static_step_jline_plus = vm[se::static_step_jline_plus].as<unsigned int>();
    }
}
