#ifndef LU_H
#define LU_H

#include "lu/defines.h"
#include "promises/dynamic_step_promise.h"

/** 
 * @brief Compute the LU decomposition of @a matrix
 *
 * The result is put in matrix @a out. The strictly lower part of @a out
 * contains the L matrix (the effective L matrix has a unary diagonal), and the
 * diagonal and strictly upper part of @a out contains the U matrix.
 *
 * This algorithm is sequential.
 */
void kernel_lu(Matrix2D const& matrix, Matrix2D& out);

/**
 * @brief OpenMP parallel version of LU
 */
void kernel_lu_omp(Matrix2D const& matrix, Matrix2D& out);

/**
 * @brief OpenMP parallel version of LU with PromisePlus
 *
 * Result is stored in the promises in @a promises.
 */
template<DynamicStepPromiseMode mode>
void kernel_lu_omp_pp(Matrix2D const& matrix,
                   std::vector<DynamicStepPromise<Matrix2DValue, mode>*>& promises);

/**
 * @brief Compute the solution of Ax = b
 *
 * This solver assumes the matrix @a lu contains the factorization of the
 * A matrix.
 */
void kernel_lu_solve(Matrix2D const& lu, Vector1D const& b, Vector1D& x);

/**
 * @brief Compute the solution of Ax = b for N different b.
 * 
 * This solver assumes the matrix @a lu contains the factorization of the
 * A matrix;
 */
void kernel_lu_solve_n(Matrix2D const& lu, std::vector<Vector1D> const& b,
                                           std::vector<Vector1D>& x);

/**
 * @brief Compute the solution of Ax = b with PromisePlus
 *
 * This solver assumes the PromisePlus in @a lu contain the factorization of
 * the A matrix.
 */
template<DynamicStepPromiseMode mode>
void kernel_lu_solve_pp(std::vector<DynamicStepPromise<Matrix2DValue, mode>*>& lu,
                        Vector1D const& b, Vector1D& x);

/**
 * @brief Compute the solution of Ax = b for N different b with PromisePlus
 *
 * This solver assumes the PromisePlus in @a lu contain the factorization of
 * the A matrix.
 */
template<DynamicStepPromiseMode mode>
void kernel_lu_solve_n_pp(std::vector<DynamicStepPromise<Matrix2DValue, mode>*>& lu,
                          std::vector<Vector1D> const& b,
                          std::vector<Vector1D>& x);

// Sequential
void kernel_lu_combine(Matrix2D& a, Vector1D const& b, Vector1D& x);
// N Sequential
void kernel_lu_combine_n(Matrix2D& a, std::vector<Vector1D> const& b,
                                      std::vector<Vector1D>& x);

// OpenMP
void kernel_lu_combine_omp(Matrix2D& a, Vector1D const& b, Vector1D& x);
// N OpenMP
void kernel_lu_combine_n_omp(Matrix2D& a, std::vector<Vector1D> const& b,
                                          std::vector<Vector1D>& x);
/**
 * @brief Compute the solution of Ax = b through LU factorization
 *
 * This solver computes the LU factorization of A and streams it into a
 * triangular solver.
 */
template<DynamicStepPromiseMode mode>
void kernel_lu_combine_pp(Matrix2D& a, Vector1D const& b, Vector1D& x,
                          DynamicStepPromiseBuilder<Matrix2DValue, mode> const& builder);

/**
 * @brief Compute the solution of Ax = b for N different b through LU 
 * factorization
 *
 * This solver compute the LU factorization of A and streams it into N
 * different triangular solvers.
 */
template<DynamicStepPromiseMode mode>
void kernel_lu_combine_n_pp(Matrix2D& a, std::vector<Vector1D> const& b, 
                                         std::vector<Vector1D>& x,
                                         DynamicStepPromiseBuilder<Matrix2DValue, mode> const& builder);

#include "lu/lu.tpp"

#endif // LU_H
