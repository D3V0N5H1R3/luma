// FileSystem module registration.
//
// The registrations are grouped into three cohesive sub-functions, each in its
// own translation unit: entry operations, path queries, and file content I/O.
// Shared helper primitives live in filesystem_internal.hpp.

#include "runtime/stdlib/io/filesystem_module.hpp"

namespace luma {

void register_filesystem_ns(const EnvPtr& env) {
    register_filesystem_operations(env);
    register_filesystem_paths(env);
    register_filesystem_content(env);
}

} // namespace luma
