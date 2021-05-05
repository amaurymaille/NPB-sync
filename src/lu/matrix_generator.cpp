#include <cstdlib>
#include <ctime>

#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
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
static void dump_different_floats(std::ostringstream& out, float lhs, float rhs);

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

    for (int i = 0; i < data["nb_elements"].get<unsigned int>(); ++i) {
        deserialize_float(matrix_data_stream, *(expected_start.data() + i));
    }
    // std::copy(std::istream_iterator<Matrix2D::element>(matrix_data_stream), std::istream_iterator<Matrix2D::element>(), expected_start.data());

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
        for (int j = 0; j < expected_start.size(); ++j) {
            deserialize_float(vector_data_stream, vector[j]);
        }
        // std::copy(std::istream_iterator<Vector1D::value_type>(vector_data_stream), std::istream_iterator<Vector1D::value_type>(), vector.data());

        if (memcmp(bs[i].data(), vector.data(), sizeof(Vector1D::value_type) * vector.size()) != 0) {
            assert_bs_vectors << "[ERROR] " << i << "-th BS vector discrepancy. Expected / got :" << std::endl;
            // std::copy(xs[i].begin(), xs[i].end(), std::ostream_iterator<Vector1D::value_type>(assert_vectors, " "));
            // assert_vectors << std::endl;
            // std::copy(v.begin(), v.end(), std::ostream_iterator<Vector1D::value_type>(assert_vectors, " "));
            // assert_vectors << std::endl;
            for (int j = 0; j < bs[i].size(); ++j) {
                if (bs[i][j] == vector[j]) {
                    assert_bs_vectors << "OK " << j << ": " << bs[i][j] << " " << vector[j] << std::endl;;
                } else {
                    assert_bs_vectors << "KO " << j << ": " << bs[i][j] << " " << vector[j] << std::endl;;
                }
                dump_different_floats(assert_bs_vectors, bs[i][j], vector[j]);
            }
            assert_bs_vectors << std::endl;
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
    for (int i = 0; i < data["nb_elements"]; ++i) {
        deserialize_float(matrix_stream, *(content.data() + i));
    }
    // std::copy(std::istream_iterator<Matrix2D::element>(matrix_stream), std::istream_iterator<Matrix2D::element>(), content.data());

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
        for (int j = 0; j < v.size(); ++j) {
            deserialize_float(vector_stream, v[j]);
        }
        // std::copy(std::istream_iterator<Vector1D::value_type>(vector_stream), std::istream_iterator<Vector1D::value_type>(), v.begin());

        if (memcmp(xs[i].data(), v.data(), sizeof(Vector1D::value_type) * v.size()) != 0) {
            assert_vectors << "[ERROR] " << i << "-th XS vector discrepancy. Expected / got :" << std::endl;
            // std::copy(xs[i].begin(), xs[i].end(), std::ostream_iterator<Vector1D::value_type>(assert_vectors, " "));
            // assert_vectors << std::endl;
            // std::copy(v.begin(), v.end(), std::ostream_iterator<Vector1D::value_type>(assert_vectors, " "));
            // assert_vectors << std::endl;
            for (int j = 0; j < xs[i].size(); ++j) {
                if (xs[i][j] == v[j]) {
                    assert_vectors << "OK " << j << ": " << xs[i][j] << " " << v[j] << std::endl;;
                } else {
                    assert_vectors << "KO " << j << ": " << xs[i][j] << " " << v[j] << std::endl;;
                }
                dump_different_floats(assert_vectors, xs[i][j], v[j]);
            }
            assert_vectors << std::endl;
        }
    }

    if (assert_vectors.tellp() != 0) {
        throw std::runtime_error(assert_vectors.str());
    }
}

void generate_lu_start_matrice_and_vectors(json const& data, Matrix2D& matrix, std::vector<Vector1D>& xs, std::vector<Vector1D>& bs, Args const& args) {
    std::cout << "Starting generation of matrix in file " << args.start_matrix_filename << std::endl;
    std::ofstream out = open_out_file(args.start_matrix_filename);

    for (Vector1D& b: bs)
        b.resize(matrix.size());

    for (Vector1D& x: xs)
        x.resize(matrix.size());

    LUSolver::init_matrix(matrix);
    LUSolver::generate_vectors(matrix, xs, bs);

    std::for_each(matrix.data(), matrix.data() + data["nb_elements"].get<unsigned int>(), [&](Matrix2D::element const& elem) { 
        serialize_float(out, elem) << " ";
    });
    // std::copy(matrix.data(), matrix.data() + data["nb_elements"].get<unsigned int>(), std::ostream_iterator<Matrix2D::element>(out, " "));
    out << std::endl;
    out << xs.size() << std::endl;
    for (Vector1D const& v: bs) {
        std::for_each(v.begin(), v.end(), [&](Vector1D::value_type const& elem) {
            serialize_float(out, elem) << " ";
        });
        // std::copy(v.begin(), v.end(), std::ostream_iterator<Vector1D::value_type>(out, " "));
        out << std::endl;
    }
    std::cout << "Ended generation of matrix in file " << args.start_matrix_filename << std::endl;
}

void generate_lu_compute_matrice_and_vectors(json const& data, Matrix2D const& start, Matrix2D& result, std::vector<Vector1D> const& xs, Args const& args) {
    std::ofstream out = open_out_file(args.compute_matrix_filename);
    std::cout << "Started generation of compute matrix in " << args.compute_matrix_filename << std::endl;

    LUSolver::compute_matrix(start, result);
    unsigned int nb_elements = data["nb_elements"].get<unsigned int>();    

    std::for_each(result.data(), result.data() + nb_elements, [&](Matrix2D::element const& elem) {
        serialize_float(out, elem) << " ";
    });

    std::cout << "Done writing compute matrix" << std::endl;
    // std::copy(result.data(), result.data() + nb_elements, std::ostream_iterator<Matrix2D::element>(out, " "));
    out << std::endl << xs.size() << std::endl;
    for (int i = 0; i < xs.size(); ++i) {
        Vector1D const& v = xs[i];
        std::for_each(v.begin(), v.end(), [&](Vector1D::value_type const& elem) {
            serialize_float(out, elem) << " ";
        });
        // std::copy(v.begin(), v.end(), std::ostream_iterator<Vector1D::value_type>(out, " "));
        out << std::endl;
    }

    std::cout << "Ended generation of compute matrix in " << args.compute_matrix_filename << std::endl;
}

int main(int argc, char** argv) {
    srand((unsigned)time(nullptr));

    Args args;
    parse_command_line(argc, argv, args, std::nullopt, std::nullopt);
    generate_lu_matrices_and_vectors(args);

    return 0;
}

void dump_different_floats(std::ostringstream& out, float lhs, float rhs) {
    const unsigned char* lhs_begin = reinterpret_cast<const unsigned char*>(&lhs);
    const unsigned char* rhs_begin = reinterpret_cast<const unsigned char*>(&rhs);

    for (unsigned int i = 0; i < 4; ++i) {
        for (unsigned int j = 0; j < 8; ++j) {
            unsigned int lb = (unsigned)*lhs_begin & (1 << (7 - j));
            unsigned int rb = (unsigned)*rhs_begin & (1 << (7 - j));

            if (lb == rb) {
                out << "BOK ";
            } else {
                out << "BKO ";
            }

            out << i * 8 + j << ": " << lb << " " << rb << std::endl;

        }
        lhs_begin++;
        rhs_begin++;
    }

    out << std::endl;
}
