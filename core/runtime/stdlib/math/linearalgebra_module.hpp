#ifndef LUMA_STDLIB_LINEARALGEBRA_MODULE_HPP
#define LUMA_STDLIB_LINEARALGEBRA_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_linearalgebra_ns(const EnvPtr& env);

// Internal sub-registration functions (split into linearalgebra_vectors.cpp and
// linearalgebra_matrices.cpp for readability).
void register_linearalgebra_vectors(const EnvPtr& env);
void register_linearalgebra_matrices(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_LINEARALGEBRA_MODULE_HPP
