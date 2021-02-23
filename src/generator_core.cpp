#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "generator_core.h"

namespace Options {
    static const char* start_matrix_file = "start-matrix-file";
    static const char* compute_matrix_file = "compute-matrix-file";
    static const char* parameters_file = "parameters-file";
}

namespace o = Options;

static void process_options(po::variables_map& vm, Args& args, std::optional<ArgsCallbacks> const& callbacks);

void parse_command_line(int argc, char** argv, Args& args, 
                        std::optional<po::options_description> const& additional_args, 
                        std::optional<ArgsCallbacks> const& callbacks) {
    po::options_description base_options("Base options");
    base_options.add_options()
        ("help,h", "Display this help and exit")
        (o::start_matrix_file, po::value<std::string>(), "Path to the output file for the start matrix")
        (o::compute_matrix_file, po::value<std::string>(), "Path to the output file for the computed matrix")
        (o::parameters_file, po::value<std::string>(), "Path to the JSON file that contains the parameters of the matrix")
        ;

    po::options_description options("All options");
    options.add(base_options);
    
    if (additional_args)
        options.add(*additional_args);

    po::variables_map vm;
    po::command_line_parser parser(argc, argv);
    parser.options(options);
    po::store(parser.run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << options << std::endl;
        exit(1);
    }

    process_options(vm, args, callbacks);
} 

void process_options(po::variables_map& vm, Args& args, std::optional<ArgsCallbacks> const& callbacks) {
    if (!vm.count(o::start_matrix_file) || !vm.count(o::compute_matrix_file) || !vm.count(o::parameters_file)) {
        std::ostringstream str;
        str << o::start_matrix_file << " and " << o::compute_matrix_file << " are both required !" << std::endl;
        throw std::runtime_error(str.str());
    }

    args.start_matrix_filename = vm[o::start_matrix_file].as<std::string>();
    args.compute_matrix_filename = vm[o::compute_matrix_file].as<std::string>();
    args.parameters_filename = vm[o::parameters_file].as<std::string>();

    if (callbacks) {
        for (auto const& p: *callbacks) {
            if (vm.count(p.first)) {
                p.second(vm);
            }
        }
    }
}
