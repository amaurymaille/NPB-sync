#include <cstdio>

#include <iostream>
#include <string>

#include <boost/program_options.hpp>

#include "argv.h"
#include "dynamic_config.h"

namespace Options {
    namespace Files {
        static const char* runs_times_file = "runs-times-file";
        static const char* iterations_times_file = "iterations-times-file";
        static const char* simulations_file = "simulations-file";
        static const char* parameters_file = "parameters-file";
        static const char* input_matrix_file = "input-matrix-file";
        static const char* start_matrix_file = "start-matrix-file";
    }

    namespace Standard {
        static const char* description = "description";
    }

    namespace Programs {
        static const char* heat_cpu = "heat-cpu";
        static const char* lu = "lu";
    }
}

namespace po = boost::program_options;
namespace f = Options::Files;
namespace ostd = Options::Standard;
namespace prg = Options::Programs;

static void process_standard(DynamicConfig& config, po::variables_map const& vm);
static void process_program(DynamicConfig& config, po::variables_map const& vm);
static void populate_options(po::options_description& options);
static void process_files(DynamicConfig::Files& files,
                          po::variables_map const& vm);
static void init_outfile_if(const char* option, po::variables_map const& vm, std::function<void(std::ostream&)> f);

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

    process_program(config, vm);
    process_files(config._files, vm);
    process_standard(config, vm);
}

void populate_options(po::options_description& options) {
    po::options_description generic("Generic options");
    generic.add_options()
        ("help,h", "Display this help and exit")
        (f::runs_times_file, po::value<std::string>(), "Path to the file in which the time for each run will be written")
        (f::iterations_times_file, po::value<std::string>(), "Path to the file in which the time for each iteration of each run will be written")
        (f::simulations_file, po::value<std::string>(), "Path to the file that contains the data for the runs")
        (f::parameters_file, po::value<std::string>(), "Path to the file in which the general data of all simulations will be written")
        (f::start_matrix_file, po::value<std::string>(), "Path to the file in which the data of the start matrix is stored")
        (f::input_matrix_file, po::value<std::string>(), "Path to the file in which the expected output matrix is stored")
        (ostd::description, po::value<std::string>(), "Description of the simulation")
        ;

    po::options_descriptions programs("Programs");
    programs.add_options()
        (prg::heat_cpu, "Heat CPU program")
        (prg::lu, "LU program")
        ;

    options.add(generic).add(programs);
}

void process_files(DynamicConfig::Files& files,
                   po::variables_map const& vm) {
    init_outfile_if(f::runs_times_file, vm, std::bind(&DynamicConfig::Files::set_runs_times_file, &files, std::placeholders::_1));
    init_outfile_if(f::iterations_times_file, vm, std::bind(&DynamicConfig::Files::set_iterations_times_file, &files, std::placeholders::_1));
    init_outfile_if(f::parameters_file, vm, std::bind(&DynamicConfig::Files::set_parameters_file, &files, std::placeholders::_1));

    files.set_simulations_filename(vm[f::simulations_file].as<std::string>());
    if (vm.count(f::input_matrix_file)) {
        files.set_input_matrix_filename(vm[f::input_matrix_file].as<std::string>());
    }

    if (vm.count(f::start_matrix_file)) {
        files.set_start_matrix_filename(vm[f::start_matrix_file].as<std::string>());
    }
}

void init_outfile_if(const char* option, po::variables_map const& vm, std::function<void(std::ostream&)> f) {
    if (vm.count(option)) {
        f(*(new std::ofstream(vm[option].as<std::string>(), std::ios::out)));
    } else {
        f(std::cout);
    }
}

void process_standard(DynamicConfig& config, po::variables_map const& vm) {
    if (vm.count(ostd::description)) {
        config._std._description = vm[ostd::description].as<std::string>();
    } else {
        throw std::runtime_error("[Argument Processing] Description parameter required");
    }
}

void process_program(DynamicConfig& config, po::variables_map const& vm) {
    if (vm.count(prg::base)) {
        config._program = 
    }
}
