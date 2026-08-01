#ifndef LUMA_STDLIB_STATISTICS_MODULE_HPP
#define LUMA_STDLIB_STATISTICS_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

// Descriptive statistics over numeric arrays: central tendency (mean, median,
// mode) and dispersion (variance, standard_deviation).
void register_statistics_ns(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_STATISTICS_MODULE_HPP
