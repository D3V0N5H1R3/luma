#ifndef LUMA_STDLIB_BINARYTREE_MODULE_HPP
#define LUMA_STDLIB_BINARYTREE_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_binarytree_ns(const EnvPtr& env);

// Internal sub-registration functions (split for readability).
void register_binarytree_core(const EnvPtr& env);
void register_binarytree_advanced(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_BINARYTREE_MODULE_HPP
