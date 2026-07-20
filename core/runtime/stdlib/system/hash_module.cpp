// hash_module.cpp — Hash module registration entry point.
//
// Delegates to sub-registration functions in hash_digest.cpp and hash_file.cpp.

#include "runtime/stdlib/system/hash_module.hpp"

namespace luma {

void register_hash_ns(const EnvPtr& env, bool sandbox) {
    register_hash_digest(env);

    if (!sandbox) {
        register_hash_file(env);
    }
}

} // namespace luma
