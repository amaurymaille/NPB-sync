#ifndef MATRIX_CORE_H
#define MATRIX_CORE_H

template<typename Matrix>
/* abstract */ class IReferenceMatrix {
public:
    typedef Matrix MatrixT;
    typedef typename MatrixT::element MatrixTValue;

    // If a filename is provided on the CLI, init the matrix from this file
    // Otherwise, init with default values
    virtual void init() = 0;
    // Assert that the matrix other was properly initialized by comparing it
    // with our own initialized matrix
    virtual void assert_okay_init(const Matrix& other) const = 0;

    // If a filename is provided on the CLI, init the expected matrix from this
    // file. Otherwise, init with default values and compute the expected
    // matrix from these values
    virtual void init_expected() = 0;
    // Assert that the matrix other was properly computed by comparing it
    // with our own computed matrix
    virtual void assert_okay_expected(const Matrix& other) const = 0;

    // Init the initial matrix from other
    virtual void init_from(Matrix const& other) = 0;
    // Init the expected matrix from other
    virtual void init_expected_from(Matrix const& other) = 0;

    // Assert that the matrix other is equal to the initialized matrix
    virtual void assert_equals(Matrix const& other) const = 0;
    // Assert that the matrix other is equal to the computed matrix
    virtual void assert_expected_equals(Matrix const& other) const = 0;

    // Read-access to the initialized matrix
    const Matrix& get_matrix() const { return _matrix; }
    // Read-access to the computed matrix
    const Matrix& get_expected() const { return _expected; }

protected:
    // The initial matrix
    Matrix _matrix;
    // The expected matrix
    Matrix _expected;

    // Init the initial matrix with default values
    virtual void _init() = 0;
    // Init the expected matrix with default values and then compute the
    // expected matrix from these values
    virtual void _init_expected() = 0;

    // Init the initial matrix from the content of the file filaname
    virtual void init_from_file(std::string const& filename) = 0;
    // Init the expected matrix from the content of the file filename
    virtual void init_expected_from_file(std::string const& filename) = 0;
};

#endif /* MATRIX_CORE_H */
