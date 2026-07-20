#ifndef LUMA_STDLIB_DATETIME_MODULE_HPP
#define LUMA_STDLIB_DATETIME_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_datetime_ns(const EnvPtr& env);

// Internal sub-registration function (split for readability).
void register_datetime_arithmetic(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_DATETIME_MODULE_HPP
