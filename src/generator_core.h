#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/program_options.hpp>

namespace po = boost::program_options;

struct AdditionalArgs { };

struct Args {
    std::string start_matrix_filename;
    std::string compute_matrix_filename;
    std::string parameters_filename;

    std::unique_ptr<AdditionalArgs> additional_args;
};

typedef std::map<std::string, std::function<void(po::variables_map&)>> ArgsCallbacks;

void parse_command_line(int argc, char** argv, Args& args, 
                        std::optional<po::options_description> const& additional_args, 
                        std::optional<ArgsCallbacks> const& callbacks);
