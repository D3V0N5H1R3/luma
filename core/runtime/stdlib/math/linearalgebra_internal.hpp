#ifndef LUMA_STDLIB_LINEARALGEBRA_INTERNAL_HPP
#define LUMA_STDLIB_LINEARALGEBRA_INTERNAL_HPP

// Numeric tolerances shared between the vector operations
// (linearalgebra_vectors.cpp) and the matrix operations
// (linearalgebra_matrices.cpp).  A single definition of each lives here so both
// translation units agree on the thresholds; k_singularity_threshold in
// particular gates both LU pivoting and vector normalisation.

namespace luma::linalg_detail {

constexpr double k_singularity_threshold = 1e-14;
constexpr double k_orthogonality_tolerance = 1e-10;
constexpr double k_comparison_tolerance = 1e-10;

} // namespace luma::linalg_detail

#endif // LUMA_STDLIB_LINEARALGEBRA_INTERNAL_HPP
