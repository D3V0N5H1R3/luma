#ifndef LUMA_STDLIB_LOG_MODULE_HPP
#define LUMA_STDLIB_LOG_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_log_ns(const EnvPtr& env, bool sandbox = false);

} // namespace luma

#endif // LUMA_STDLIB_LOG_MODULE_HPP
