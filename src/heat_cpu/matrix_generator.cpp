#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#include <boost/program_options.hpp>
#include "nlohmann/json.hpp"
#include <generator_core.h>
#include <sys/time.h>
#include <utils.h>

#include "heat_cpu/matrix_core.h"

using json = nlohmann::json;

static void generate_heat_cpu_matrices(Args& args);
static void generate_heat_cpu_start_matrix(Matrix4D& result, uint64 nb_elements, const std::string& output_file);
static void generate_heat_cpu_computed_matrix(Matrix4D& start, uint64 nb_elements, int dimw, int dimx, int dimy, int dimz, const std::string& output_file);

void generate_heat_cpu_matrices(Args& args) {
    std::ifstream in(args.parameters_filename);
    if (!in) {
        std::ostringstream err;
        err << "Error while opening " << args.parameters_filename << std::endl;
        throw std::runtime_error(err.str());
    }
    
    json data;
    in >> data;

    std::cout << data << std::endl;

    Matrix4D matrix(boost::extents[data["w"]][data["x"]][data["y"]][data["z"]]);
    Matrix4D expected_start(boost::extents[data["w"]][data["x"]][data["y"]][data["z"]]);
    Matrix4D expected_compute(boost::extents[data["w"]][data["x"]][data["y"]][data["z"]]);

    struct timespec generate_start_begin, generate_start_end,
           load_start_begin, load_start_end,
           copy_start_begin, copy_start_end,
           check_start_begin, check_start_end,
           generate_compute_begin, generate_compute_end,
           load_compute_begin, load_compute_end,
           copy_compute_begin, copy_compute_end,
           check_compute_begin, check_compute_end;

    clock_gettime(CLOCK_MONOTONIC, &generate_start_begin);
    generate_heat_cpu_start_matrix(matrix, data["nb_elements"], args.start_matrix_filename);
    clock_gettime(CLOCK_MONOTONIC, &generate_start_end);

    clock_gettime(CLOCK_MONOTONIC, &load_start_begin);
    std::ifstream start(args.start_matrix_filename);
    clock_gettime(CLOCK_MONOTONIC, &load_start_end);

    clock_gettime(CLOCK_MONOTONIC, &copy_start_begin);
    std::copy(std::istream_iterator<Matrix4D::element>(start), std::istream_iterator<Matrix4D::element>(), expected_start.data());
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
    generate_heat_cpu_computed_matrix(matrix, data["nb_elements"], data["w"], data["x"], data["y"], data["z"], args.compute_matrix_filename);
    clock_gettime(CLOCK_MONOTONIC, &generate_compute_end);

    clock_gettime(CLOCK_MONOTONIC, &load_compute_begin);
    std::ifstream compute(args.compute_matrix_filename);
    clock_gettime(CLOCK_MONOTONIC, &load_compute_end);

    clock_gettime(CLOCK_MONOTONIC, &copy_compute_begin);
    std::copy(std::istream_iterator<Matrix4D::element>(compute), std::istream_iterator<Matrix4D::element>(), expected_compute.data());
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
        std::cout << message << ": " << clock_diff(&end, &begin) << std::endl;
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

void generate_heat_cpu_start_matrix(Matrix4D& matrix, uint64 nb_elements, const std::string& output_file) {
    std::ofstream out = open_out_file(output_file);
    Matrix4D::element* ptr = matrix.data();

    HeatCPUMatrix::init_matrix(matrix, nb_elements);

    std::copy(ptr, ptr + nb_elements, std::ostream_iterator<Matrix4D::element>(out, " "));
}

void generate_heat_cpu_computed_matrix(Matrix4D& start, uint64 nb_elements, int dimw, int dimx, int dimy, int dimz, const std::string& output_file) {
    std::ofstream out = open_out_file(output_file);
    Matrix4D::element* ptr = start.data();

    HeatCPUMatrix::compute_matrix(start, dimw, dimx, dimy, dimz);

    std::copy(ptr, ptr + nb_elements, std::ostream_iterator<Matrix4D::element>(out, " "));
}

uint64 clock_diff(const struct timespec& end, const struct timespec& begin) {
    uint64 diff = (end.tv_sec - begin.tv_sec) * 1000000000 + (end.tv_nsec - begin.tv_nsec);
    return diff;
}

int main(int argc, char** argv) {
    Args args;
    parse_command_line(argc, argv, args, std::nullopt, std::nullopt);
    generate_heat_cpu_matrices(args);
    return 0;
}
