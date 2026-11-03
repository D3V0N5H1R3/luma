#ifndef LUMA_STDLIB_LINEARALGEBRA_INTERNAL_HPP
#define LUMA_STDLIB_LINEARALGEBRA_INTERNAL_HPP

// Numeric tolerances and allocation guards shared between the vector
// operations (linearalgebra_vectors.cpp) and the matrix operations
// (linearalgebra_matrices.cpp).  A single definition of each lives here so both
// translation units agree on the thresholds; k_singularity_threshold in
// particular gates both LU pivoting and vector normalisation.

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "common/resource_limits.hpp"
#include "runtime/stdlib/common/error_messages.hpp"

namespace luma::linalg_detail {

constexpr double k_singularity_threshold = 1e-14;
constexpr double k_orthogonality_tolerance = 1e-10;
constexpr double k_comparison_tolerance = 1e-10;

// Guard a matrix allocation of rows × cols elements against the array-size
// limit.  The comparison divides rather than multiplies so the product can
// never overflow.  from_mat requires callers to validate the element count
// first, so every matrix constructor — multiply, whose result is
// rows(a) × cols(b), and outer, whose result is len(a) × len(b) — routes
// through this one guard.
inline void require_matrix_within_limit(std::uint64_t rows, std::uint64_t cols,
                                        std::string_view function, std::string_view subject,
                                        std::string_view hint, const SourceLocation& loc) {
    if (rows != 0 && cols > ResourceLimits::max_array_size / rows) {
        throw RuntimeError{error_msg("LinearAlgebra", function,
                                     std::format("{} exceeds maximum matrix element count ({})",
                                                 subject, ResourceLimits::max_array_size)),
                           loc, std::string{hint}};
    }
}

} // namespace luma::linalg_detail

#endif // LUMA_STDLIB_LINEARALGEBRA_INTERNAL_HPP
