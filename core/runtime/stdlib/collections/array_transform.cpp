// Array module — transform and query operations.
// Split from array_module.cpp for readability.  Registered by
// register_array_transform() called from register_array_ns().
//
// The registrations are grouped into three cohesive sub-functions, each in
// its own translation unit: reordering, aggregate/slice, and query.

#include "runtime/stdlib/collections/array_module.hpp"

namespace luma {

void register_array_transform(const EnvPtr& env) {
    register_array_transform_reordering(env);
    register_array_transform_aggregate(env);
    register_array_transform_query(env);
}

} // namespace luma
