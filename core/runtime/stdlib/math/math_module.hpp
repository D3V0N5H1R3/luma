#ifndef LUMA_STDLIB_MATH_MODULE_HPP
#define LUMA_STDLIB_MATH_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_math_ns(const EnvPtr& env);

// Internal sub-registration functions (split for readability).
void register_math_analysis(const EnvPtr& env);
void register_math_transcendental(const EnvPtr& env);
void register_math_fraction(const EnvPtr& env);
void register_math_complex(const EnvPtr& env);
void register_math_vectors(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_MATH_MODULE_HPP
