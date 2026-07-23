#ifndef LUMA_STDLIB_DECIMAL_MODULE_HPP
#define LUMA_STDLIB_DECIMAL_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

// Registers the `Decimal` namespace: exact base-10 arithmetic backed by the
// first-class opaque `decimal` value type.  Always available (no OS access).
void register_decimal_ns(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_DECIMAL_MODULE_HPP
