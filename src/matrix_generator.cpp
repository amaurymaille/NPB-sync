#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#include <boost/program_options.hpp>
#include "nlohmann/json.hpp"
#include <sys/time.h>

#include "matrix_core.h"

using json = nlohmann::json;

struct Args {
    std::string start_matrix_filename;
    std::string compute_matrix_filename;
    std::string parameters_filename;
};

namespace Options {
    static const char* start_matrix_file  = "start-matrix-file";
    static const char* compute_matrix_file = "compute-matrix-file";
    static const char* parameters_file = "parameters-file";
}

namespace po = boost::program_options;
namespace o = Options;

static void parse_command_line(int argc, char** argv, Args& args);
static void process_options(po::variables_map vm, Args& args);
static void generate_matrices(Args& args);
static void generate_start_matrix(Matrix& result, uint64 nb_elements, const std::string& output_file);
static void generate_computed_matrix(Matrix& start, uint64 nb_elements, int dimw, int dimx, int dimy, int dimz, const std::string& output_file);
// static void 
static std::ofstream open_out_file(const std::string& output_file);
static uint64 clock_diff(const struct timespec& end, const struct timespec& begin);

void parse_command_line(int argc, char** argv, Args& args) {
    po::options_description base_options("Base options");
    base_options.add_options()
        ("help,h", "Display this help and exit")
        (o::start_matrix_file, po::value<std::string>(), "Path to the output file for the start matrix")
        (o::compute_matrix_file, po::value<std::string>(), "Path to the output file for the computed matrix")
        (o::parameters_file, po::value<std::string>(), "Path to the JSON file that contains the parameters of the matrix")
        ;

    po::options_description programs("Program choice");
    programs.add_options()
        ("base", "heat_cpu program")
        ("lu", "LU solver")
        ;

    po::options_description options("All options");
    options.add(base_options).add(programs);

    po::variables_map vm;
    po::command_line_parser parser(argc, argv);
    parser.options(options);
    po::store(parser.run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << options << std::endl;
        exit(1);
    }

    process_options(vm, args);
} 

void process_options(po::variables_map vm, Args& args) {
    if (!vm.count(o::start_matrix_file) || !vm.count(o::compute_matrix_file) || !vm.count(o::parameters_file)) {
        std::ostringstream str;
        str << o::start_matrix_file << " and " << o::compute_matrix_file << " are both required !" << std::endl;
        throw std::runtime_error(str.str());
    }

    args.start_matrix_filename = vm[o::start_matrix_file].as<std::string>();
    args.compute_matrix_filename = vm[o::compute_matrix_file].as<std::string>();
    args.parameters_filename = vm[o::parameters_file].as<std::string>();
}

void generate_matrices(Args& args) {
    std::ifstream in(args.parameters_filename);
    if (!in) {
        std::ostringstream err;
        err << "Error while opening " << args.parameters_filename << std::endl;
        throw std::runtime_error(err.str());
    }
    
    json data;
    in >> data;

    std::cout << data << std::endl;

    Matrix matrix(boost::extents[data["w"]][data["x"]][data["y"]][data["z"]]);
    Matrix expected_start(boost::extents[data["w"]][data["x"]][data["y"]][data["z"]]);
    Matrix expected_compute(boost::extents[data["w"]][data["x"]][data["y"]][data["z"]]);

    struct timespec generate_start_begin, generate_start_end,
           load_start_begin, load_start_end,
           copy_start_begin, copy_start_end,
           check_start_begin, check_start_end,
           generate_compute_begin, generate_compute_end,
           load_compute_begin, load_compute_end,
           copy_compute_begin, copy_compute_end,
           check_compute_begin, check_compute_end;

    clock_gettime(CLOCK_MONOTONIC, &generate_start_begin);
    generate_start_matrix(matrix, data["nb_elements"], args.start_matrix_filename);
    clock_gettime(CLOCK_MONOTONIC, &generate_start_end);

    clock_gettime(CLOCK_MONOTONIC, &load_start_begin);
    std::ifstream start(args.start_matrix_filename);
    clock_gettime(CLOCK_MONOTONIC, &load_start_end);

    clock_gettime(CLOCK_MONOTONIC, &copy_start_begin);
    std::copy(std::istream_iterator<Matrix::element>(start), std::istream_iterator<Matrix::element>(), expected_start.data());
    clock_gettime(CLOCK_MONOTONIC, &copy_start_end);

    std::cout << "Assert start" << std::endl;
    clock_gettime(CLOCK_MONOTONIC, &check_start_begin);
    for (int i = 0; i < data["nb_elements"]; ++i) {
        if (matrix.data()[i] != expected_start.data()[i]) {
            std::cout << i << ", " << matrix.data()[i] << ", " << expected_start.data()[i] << std::endl;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &check_start_end);

    std::cout << std::endl << std::endl;

    clock_gettime(CLOCK_MONOTONIC, &generate_compute_begin);
    generate_computed_matrix(matrix, data["nb_elements"], data["w"], data["x"], data["y"], data["z"], args.compute_matrix_filename);
    clock_gettime(CLOCK_MONOTONIC, &generate_compute_end);

    clock_gettime(CLOCK_MONOTONIC, &load_compute_begin);
    std::ifstream compute(args.compute_matrix_filename);
    clock_gettime(CLOCK_MONOTONIC, &load_compute_end);

    clock_gettime(CLOCK_MONOTONIC, &copy_compute_begin);
    std::copy(std::istream_iterator<Matrix::element>(compute), std::istream_iterator<Matrix::element>(), expected_compute.data());
    clock_gettime(CLOCK_MONOTONIC, &copy_compute_end);

    std::cout << "Assert expected" << std::endl;
    clock_gettime(CLOCK_MONOTONIC, &check_compute_begin);
    for (int i = 0; i < data["nb_elements"]; ++i) {
        if (matrix.data()[i] != expected_compute.data()[i]) {
            std::cout << i << ", " << matrix.data()[i] << ", " << expected_compute.data()[i] << std::endl;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &check_compute_end);

    auto fn = [](const std::string& message, const struct timespec& end, const struct timespec& begin) -> void {
        std::cout << message << ": " << clock_diff(end, begin) << std::endl;
    };

    fn("Generate start", generate_start_end, generate_start_end);
    fn("Load start", load_start_end, load_start_begin);
    fn("Copy start", copy_start_end, copy_start_begin);
    fn("Check start", check_start_end, check_start_begin);
    fn("Generate compute", generate_compute_end, generate_compute_begin);
    fn("Load compute", load_compute_end, load_compute_begin);
    fn("Copy compute", copy_compute_end, copy_compute_begin);
    fn("Check compute", check_compute_end, check_compute_begin);
}

void generate_start_matrix(Matrix& matrix, uint64 nb_elements, const std::string& output_file) {
    std::ofstream out = open_out_file(output_file);
    Matrix::element* ptr = matrix.data();

    init_matrix(matrix, nb_elements);

    std::copy(ptr, ptr + nb_elements, std::ostream_iterator<Matrix::element>(out, " "));
}

void generate_computed_matrix(Matrix& start, uint64 nb_elements, int dimw, int dimx, int dimy, int dimz, const std::string& output_file) {
    std::ofstream out = open_out_file(output_file);
    Matrix::element* ptr = start.data();

    compute_matrix(start, dimw, dimx, dimy, dimz);

    std::copy(ptr, ptr + nb_elements, std::ostream_iterator<Matrix::element>(out, " "));
}

std::ofstream open_out_file(const std::string& output_file) {
    std::ofstream out(output_file);
    if (!out) {
        std::ostringstream err;
        err << "Error while opening " << output_file << std::endl;
        throw std::runtime_error(err.str());
    }

    return out;
}

uint64 clock_diff(const struct timespec& end, const struct timespec& begin) {
    uint64 diff = (end.tv_sec - begin.tv_sec) * 1000000000 + (end.tv_nsec - begin.tv_nsec);
    return diff;
}

int main(int argc, char** argv) {
    Args args;
    parse_command_line(argc, argv, args);
    generate_matrices(args);
    return 0;
}
