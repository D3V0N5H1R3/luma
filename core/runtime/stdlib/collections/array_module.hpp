#ifndef LUMA_STDLIB_ARRAY_MODULE_HPP
#define LUMA_STDLIB_ARRAY_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_array_ns(const EnvPtr& env);

// Internal sub-registration functions (split for readability).
void register_array_functional(const EnvPtr& env);
void register_array_transform(const EnvPtr& env);
void register_array_transform_reordering(const EnvPtr& env);
void register_array_transform_aggregate(const EnvPtr& env);
void register_array_transform_query(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_ARRAY_MODULE_HPP
