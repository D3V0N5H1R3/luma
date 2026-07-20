#ifndef LUMA_STDLIB_HASH_MODULE_HPP
#define LUMA_STDLIB_HASH_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_hash_ns(const EnvPtr& env, bool sandbox = false);

// Internal sub-registration functions (split for readability).
void register_hash_digest(const EnvPtr& env);
void register_hash_file(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_HASH_MODULE_HPP
