#ifndef LUMA_STDLIB_STATISTICS_MODULE_HPP
#define LUMA_STDLIB_STATISTICS_MODULE_HPP

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

// Descriptive and inferential statistics over numeric arrays: central tendency
// (mean, median, mode), dispersion (variance, standard_deviation, percentile),
// one-pass summaries (summarize, five_number_summary, histogram), and
// correlation / least-squares line fitting.
void register_statistics_ns(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_STATISTICS_MODULE_HPP
