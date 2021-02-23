#include <cstdlib>
#include <ctime>

#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>

#include <boost/program_options.hpp>
#include <generator_core.h>
#include <nlohmann/json.hpp>
#include <sys/time.h>
#include <utils.h>

#include "lu/matrix_core.h"

using json = nlohmann::json;

static void generate_lu_matrices_and_vectors(Args const& args);
// Generate a random set of Bi from random Xi. 
// Start matrix file will contain the Bi
static void generate_lu_start_matrice_and_vectors(json const& data, Matrix2D& result, std::vector<Vector1D>& xs, std::vector<Vector1D>& bs, Args const& args);
// Write the exepcted value of the different Xi
static void generate_lu_compute_matrice_and_vectors(json const& data, Matrix2D const& start, Matrix2D& result, std::vector<Vector1D> const& xs, Args const& args);
static void assert_start_okay(json const& data, Matrix2D const& start_matrix, std::vector<Vector1D> const& bs, Args const& args);
static void assert_compute_okay(json const& data, Matrix2D const& compute_matrix, std::vector<Vector1D> const& xs, Args const& args);

void generate_lu_matrices_and_vectors(Args const& args) {
    std::ifstream in = open_in_file(args.parameters_filename);

    json data;
    in >> data;

    std::cout << data << std::endl;

    Matrix2D matrix(boost::extents[data["dim"]][data["dim"]]);
    unsigned int nb_vectors = data["nb_vectors"].get<unsigned int>();
    std::vector<Vector1D> xs(nb_vectors);
    std::vector<Vector1D> bs(nb_vectors);

    generate_lu_start_matrice_and_vectors(data, matrix, xs, bs, args);
    assert_start_okay(data, matrix, bs, args);
    
    Matrix2D result(boost::extents[data["dim"]][data["dim"]]);
    generate_lu_compute_matrice_and_vectors(data, matrix, result, xs, args);
    assert_compute_okay(data, result, xs, args);
}

void assert_start_okay(json const& data, Matrix2D const& start_matrix, std::vector<Vector1D> const& bs, Args const& args) {
    Matrix2D expected_start(boost::extents[data["dim"]][data["dim"]]);

    std::ifstream start = open_in_file(args.start_matrix_filename);
    std::string matrix_data_str;
    std::getline(start, matrix_data_str);
    std::istringstream matrix_data_stream(matrix_data_str); 
    std::copy(std::istream_iterator<Matrix2D::element>(matrix_data_stream), std::istream_iterator<Matrix2D::element>(), expected_start.data());

    std::ostringstream assert_matrix_data_stream;
    for (int i = 0; i < data["nb_elements"]; ++i) {
        if (start_matrix.data()[i] != expected_start.data()[i]) {
            assert_matrix_data_stream << "[ERROR] Start matrix discrepancy at " << i << ": expected " << start_matrix.data()[i] << ", got " << expected_start.data()[i] << std::endl;
        }
    }

    if (assert_matrix_data_stream.tellp() != 0)
        throw std::runtime_error(assert_matrix_data_stream.str());

    unsigned int expected_nb_vectors;
    start >> expected_nb_vectors;
    start.ignore();

    unsigned int nb_vectors = data["nb_vectors"].get<unsigned int>();
    
    if (expected_nb_vectors != nb_vectors) {
        std::ostringstream err;
        err << "[FATAL] Provided number of vectors (" << nb_vectors << ") is not equal to written number of vectors (" << expected_nb_vectors << ") !" << std::endl;
        throw std::runtime_error(err.str());
    }

    std::ostringstream assert_bs_vectors;
    for (int i = 0; i < expected_nb_vectors; ++i) {
        std::string vector_data_str;
        std::getline(start, vector_data_str);
        std::istringstream vector_data_stream(vector_data_str);
        Vector1D vector;
        vector.resize(start_matrix.size()); // Fuck C++
        std::copy(std::istream_iterator<Vector1D::value_type>(vector_data_stream), std::istream_iterator<Vector1D::value_type>(), vector.data());

        if (vector.size() != bs[i].size() || memcmp(vector.data(), bs[i].data(), vector.size() * sizeof(Vector1D::value_type)) != 0) {
            assert_bs_vectors << "[ERROR] " << i << "-th BS vector discrepancy. Expected / got: " << std::endl;
            std::copy(bs[i].begin(), bs[i].end(), std::ostream_iterator<Vector1D::value_type>(assert_bs_vectors, " "));
            assert_bs_vectors << std::endl;
            std::copy(vector.begin(), vector.end(), std::ostream_iterator<Vector1D::value_type>(assert_bs_vectors, " "));
        }
    }

    if (assert_bs_vectors.tellp() != 0)
        throw std::runtime_error(assert_bs_vectors.str());
}

void assert_compute_okay(json const& data, Matrix2D const& compute_matrix, std::vector<Vector1D> const& xs, Args const& args) {
    std::ifstream stream = open_in_file(args.compute_matrix_filename);
    std::string matrix_str;
    std::getline(stream, matrix_str);
    std::istringstream matrix_stream(matrix_str);

    Matrix2D content(boost::extents[data["dim"]][data["dim"]]);
    std::copy(std::istream_iterator<Matrix2D::element>(matrix_stream), std::istream_iterator<Matrix2D::element>(), content.data());

    std::ostringstream assert_matrix_stream;
    for (int i = 0; i < data["nb_elements"].get<unsigned int>(); ++i) {
        if (compute_matrix.data()[i] != content.data()[i]) {
            assert_matrix_stream << "[ERROR] Compute matrix discrepancy at index " << i << ", expected " << compute_matrix.data()[i] << ", got " << content.data()[i] << std::endl;
        }
    }

    if (assert_matrix_stream.tellp() != 0)
        throw std::runtime_error(assert_matrix_stream.str());

    unsigned int nb_vectors;
    stream >> nb_vectors;
    stream.ignore();

    if (nb_vectors != xs.size()) {
        std::ostringstream err;
        err << "[FATAL] Provided number of vectors (" << xs.size() << ") is not equal to written number of vectors (" << nb_vectors << ")" << std::endl;
        throw std::runtime_error(err.str());
    }

    std::ostringstream assert_vectors;
    for (int i = 0; i < nb_vectors; ++i) {
        std::string vector_data_str;
        std::getline(stream, vector_data_str);
        std::istringstream vector_stream(vector_data_str);

        Vector1D v;
        v.resize(xs[i].size());
        std::copy(std::istream_iterator<Vector1D::value_type>(vector_stream), std::istream_iterator<Vector1D::value_type>(), v.begin());

        if (memcmp(xs[i].data(), v.data(), sizeof(Vector1D::value_type) * v.size()) != 0) {
            assert_vectors << "[ERROR] " << i << "-th XS vector discrepancy. Expected / got :" << std::endl;
            std::copy(xs[i].begin(), xs[i].end(), std::ostream_iterator<Vector1D::value_type>(assert_vectors, " "));
            assert_vectors << std::endl;
            std::copy(v.begin(), v.end(), std::ostream_iterator<Vector1D::value_type>(assert_vectors, " "));
            assert_vectors << std::endl;
        }
    }

    if (assert_vectors.tellp() != 0) {
        throw std::runtime_error(assert_vectors.str());
    }
}

void generate_lu_start_matrice_and_vectors(json const& data, Matrix2D& matrix, std::vector<Vector1D>& xs, std::vector<Vector1D>& bs, Args const& args) {
    std::ofstream out = open_out_file(args.start_matrix_filename);
    for (Vector1D& b: bs)
        b.resize(matrix.size());

    for (Vector1D& x: xs)
        x.resize(matrix.size());

    LUSolver::init_matrix(matrix, data["nb_elements"]);
    LUSolver::generate_vectors(matrix, xs, bs);

    std::copy(matrix.data(), matrix.data() + data["nb_elements"].get<unsigned int>(), std::ostream_iterator<Matrix2D::element>(out, " "));
    out << std::endl;
    out << xs.size() << std::endl;
    for (Vector1D const& v: bs) {
        std::copy(v.begin(), v.end(), std::ostream_iterator<Vector1D::value_type>(out, " "));
        out << std::endl;
    }
}

void generate_lu_compute_matrice_and_vectors(json const& data, Matrix2D const& start, Matrix2D& result, std::vector<Vector1D> const& xs, Args const& args) {
    std::ofstream out = open_out_file(args.compute_matrix_filename);
    LUSolver::compute_matrix(start, result, data["dim"].get<unsigned int>());
    unsigned int nb_elements = data["nb_elements"].get<unsigned int>();    

    std::copy(result.data(), result.data() + nb_elements, std::ostream_iterator<Matrix2D::element>(out, " "));
    out << std::endl << xs.size() << std::endl;
    for (int i = 0; i < xs.size(); ++i) {
        Vector1D const& v = xs[i];
        std::copy(v.begin(), v.end(), std::ostream_iterator<Vector1D::value_type>(out, " "));
        out << std::endl;
    }
}

int main(int argc, char** argv) {
    srand((unsigned)time(nullptr));

    Args args;
    parse_command_line(argc, argv, args, std::nullopt, std::nullopt);
    generate_lu_matrices_and_vectors(args);

    return 0;
}
