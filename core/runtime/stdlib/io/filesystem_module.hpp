#ifndef LUMA_STDLIB_FILESYSTEM_MODULE_HPP
#define LUMA_STDLIB_FILESYSTEM_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_filesystem_ns(const EnvPtr& env);

// Internal sub-registration functions (split for readability).
void register_filesystem_operations(const EnvPtr& env);
void register_filesystem_paths(const EnvPtr& env);
void register_filesystem_content(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_FILESYSTEM_MODULE_HPP
