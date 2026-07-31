// Statistics module — descriptive and inferential statistics: central tendency,
// dispersion, one-pass summaries, and correlation.  Registered via
// register_statistics_ns().

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <span>
#include <string_view>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/overflow.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/numeric_helpers.hpp"
#include "runtime/stdlib/math/statistics_module.hpp"

namespace luma {

namespace {

// Extract numeric values from an array and sort them.
[[nodiscard]] std::vector<double> sorted_doubles(std::span<const Value> elems) {
    std::vector<double> vals{};
    vals.reserve(elems.size());

    std::ranges::transform(elems, std::back_inserter(vals),
                           [](const Value& e) { return e.to_numeric(); });

    // A user array may contain NaN (e.g. Math.infinity - Math.infinity), for
    // which the default operator< is not a strict weak ordering — undefined
    // behaviour for std::ranges::sort.  Order NaN last so the comparator stays a
    // valid strict weak ordering; a statistic computed over NaN data is then
    // deterministic (if meaningless) rather than UB.
    std::ranges::sort(vals, [](double lhs, double rhs) {
        if (std::isnan(lhs)) {
            return false;
        }
        if (std::isnan(rhs)) {
            return true;
        }
        return lhs < rhs;
    });

    return vals;
}

// Compute population variance of a numeric array.
[[nodiscard]] double compute_variance(std::span<const Value> elems) {
    const double sum =
        std::accumulate(elems.begin(), elems.end(), 0.0,
                        [](double acc, const Value& e) { return acc + e.to_numeric(); });

    const double mean = sum / static_cast<double>(elems.size());

    const double sq_sum =
        std::accumulate(elems.begin(), elems.end(), 0.0, [mean](double acc, const Value& e) {
            const double diff = e.to_numeric() - mean;
            return acc + (diff * diff);
        });

    return sq_sum / static_cast<double>(elems.size());
}

} // namespace

// Statistics: central tendency (mean, median, mode), dispersion (variance,
// standard_deviation, percentile), one-pass summaries, and correlation.
void register_statistics_ns(const EnvPtr& env) {
    ModuleBuilder{"Statistics", env}
        .func("mean", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Statistics.mean", loc)->elements;

            if (auto fail = check_not_empty(elems, "Statistics.mean")) {
                return *std::move(fail);
            }

            double sum{0};

            for (const auto& elem : elems) {
                sum += elem.to_numeric();
            }

            return make_success_value(Value{sum / static_cast<double>(elems.size())});
        })
        .func("median", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Statistics.median", loc)->elements;

            if (auto fail = check_not_empty(elems, "Statistics.median")) {
                return *std::move(fail);
            }

            auto vals = sorted_doubles(elems);
            auto n = vals.size();

            if (n % 2 == 0) {
                auto median = (vals[(n / 2) - 1] + vals[n / 2]) / 2.0;

                return make_success_value(Value{median});
            }

            return make_success_value(Value{vals[n / 2]});
        })
        .func("mode", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Statistics.mode", loc)->elements;

            if (auto fail = check_not_empty(elems, "Statistics.mode")) {
                return *std::move(fail);
            }

            auto vals = sorted_doubles(elems);

            // Count occurrences (vals is already sorted by sorted_doubles).
            double best_val{vals[0]};
            std::size_t best_count{1};
            double cur_val{vals[0]};
            std::size_t cur_count{1};

            for (std::size_t i{1}; i < vals.size(); ++i) {
                if (vals[i] == cur_val) {
                    ++cur_count;
                } else {
                    if (cur_count > best_count) {
                        best_count = cur_count;
                        best_val = cur_val;
                    }

                    cur_val = vals[i];
                    cur_count = 1;
                }
            }

            if (cur_count > best_count) {
                best_count = cur_count;
                best_val = cur_val;
            }

            return make_success_value(Value{best_val});
        })
        .func("variance", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Statistics.variance", loc)->elements;

            if (auto fail = check_not_empty(elems, "Statistics.variance")) {
                return *std::move(fail);
            }

            return make_success_value(Value{compute_variance(elems)});
        })
        .func("standard_deviation", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems =
                *expect_array(args[0], "Statistics.standard_deviation", loc)->elements;

            if (auto fail = check_not_empty(elems, "Statistics.standard_deviation")) {
                return *std::move(fail);
            }

            return make_success_value(Value{std::sqrt(compute_variance(elems))});
        })
        .func("summarize", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Statistics.summarize", loc)->elements;

            if (auto fail = check_not_empty(elems, "Statistics.summarize")) {
                return *std::move(fail);
            }

            // One pass: sort once for min / max / median, then reuse the sorted
            // values for the mean.  Variance is computed over the original
            // elements (order-independent), matching Statistics.standard_deviation.
            const auto vals = sorted_doubles(elems);
            const auto n = vals.size();

            double sum{0.0};
            for (const double v : vals) {
                sum += v;
            }
            const double mean = sum / static_cast<double>(n);

            const double median =
                (n % 2 == 0) ? (vals[(n / 2) - 1] + vals[n / 2]) / 2.0 : vals[n / 2];

            const double std_dev = std::sqrt(compute_variance(elems));

            auto rec = std::make_shared<RecordValue>();
            rec->type_name = "Summary";
            rec->fields.emplace_back("count", Value{static_cast<std::int64_t>(n)});
            rec->fields.emplace_back("minimum", Value{vals.front()});
            rec->fields.emplace_back("maximum", Value{vals.back()});
            rec->fields.emplace_back("mean", Value{mean});
            rec->fields.emplace_back("median", Value{median});
            rec->fields.emplace_back("standard_deviation", Value{std_dev});

            return make_success_value(Value{std::move(rec)});
        })
        .func("five_number_summary", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems =
                *expect_array(args[0], "Statistics.five_number_summary", loc)->elements;

            if (auto fail = check_not_empty(elems, "Statistics.five_number_summary")) {
                return *std::move(fail);
            }

            const auto vals = sorted_doubles(elems);

            // Linear-interpolation quantile — the same method Statistics.percentile
            // uses, so five_number_summary(v) agrees with percentile(v, 25/50/75).
            const auto quantile = [&vals](double p) -> double {
                const double rank = (p / 100.0) * static_cast<double>(vals.size() - 1);
                const auto lower = static_cast<std::size_t>(std::floor(rank));
                const auto upper = static_cast<std::size_t>(std::ceil(rank));

                if (lower == upper) {
                    return vals[lower];
                }

                const double frac = rank - static_cast<double>(lower);

                return vals[lower] + (frac * (vals[upper] - vals[lower]));
            };

            auto rec = std::make_shared<RecordValue>();
            rec->type_name = "FiveNumberSummary";
            rec->fields.emplace_back("minimum", Value{vals.front()});
            rec->fields.emplace_back("q1", Value{quantile(25.0)});
            rec->fields.emplace_back("median", Value{quantile(50.0)});
            rec->fields.emplace_back("q3", Value{quantile(75.0)});
            rec->fields.emplace_back("maximum", Value{vals.back()});

            return make_success_value(Value{std::move(rec)});
        })
        .func("histogram", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Statistics.histogram", loc)->elements;

            if (auto fail = check_not_empty(elems, "Statistics.histogram")) {
                return *std::move(fail);
            }

            const auto bins = expect_integer(args[1], "Statistics.histogram", loc);

            if (bins < 1) {
                return make_failure_value(
                    error_msg("Statistics", "histogram", "bins must be at least 1"));
            }
            const auto bin_count = static_cast<std::size_t>(bins);

            // A histogram builds bin_edges/counts arrays, so an enormous bin
            // count would allocate past the array-size contract every other
            // stdlib path honours (and could exhaust memory).
            if (bin_count > ResourceLimits::max_array_size) {
                return make_failure_value(
                    error_msg("Statistics", "histogram", "bins exceeds the maximum array size"));
            }

            // One pass for the data range over the finite samples only —
            // folding a non-finite value (Math.infinity / NaN) into min/max
            // would poison bin_width and the sample positions below.
            double minimum = std::numeric_limits<double>::infinity();
            double maximum = -std::numeric_limits<double>::infinity();
            bool any_finite = false;
            for (const auto& elem : elems) {
                const double v = elem.to_numeric();
                if (!std::isfinite(v)) {
                    continue;
                }
                minimum = std::min(minimum, v);
                maximum = std::max(maximum, v);
                any_finite = true;
            }

            if (!any_finite) {
                return make_failure_value(
                    error_msg("Statistics", "histogram", "no finite values to bin"));
            }

            // A zero-width range (every value equal) has no natural bin width;
            // widen it to [min - 0.5, max + 0.5] so bins stay positive-width and
            // every sample lands in the middle bin — matching numpy.histogram.
            if (minimum == maximum) {
                minimum -= 0.5;
                maximum += 0.5;
            }

            const double bin_width = (maximum - minimum) / static_cast<double>(bin_count);

            std::vector<std::int64_t> counts(bin_count, 0);
            for (const auto& elem : elems) {
                const double v = elem.to_numeric();

                // Skip non-finite samples rather than risk UB casting NaN/inf.
                if (!std::isfinite(v)) {
                    continue;
                }

                double position = (v - minimum) / bin_width;
                if (position < 0.0) {
                    position = 0.0;
                }

                auto index = static_cast<std::size_t>(std::floor(position));

                // The final bin is closed on the right so the maximum sample (and
                // any floating-point overshoot) is counted in the last bin.
                if (index >= bin_count) {
                    index = bin_count - 1;
                }

                ++counts[index];
            }

            auto edges = std::make_shared<ArrayValue>();
            edges->elements->reserve(bin_count + 1);
            for (std::size_t i = 0; i < bin_count; ++i) {
                edges->elements->emplace_back(minimum + (static_cast<double>(i) * bin_width));
            }
            // Anchor the last edge exactly at the maximum to avoid float drift.
            edges->elements->emplace_back(maximum);

            auto count_arr = std::make_shared<ArrayValue>();
            count_arr->elements->reserve(bin_count);
            for (const std::int64_t c : counts) {
                count_arr->elements->emplace_back(c);
            }

            auto rec = std::make_shared<RecordValue>();
            rec->type_name = "Histogram";
            rec->fields.emplace_back("bin_edges", Value{std::move(edges)});
            rec->fields.emplace_back("counts", Value{std::move(count_arr)});
            rec->fields.emplace_back("bin_width", Value{bin_width});

            return make_success_value(Value{std::move(rec)});
        })
        .func("percentile", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Statistics.percentile", loc)->elements;
            if (auto fail = check_not_empty(elems, "Statistics.percentile")) {
                return *std::move(fail);
            }

            auto p = expect_numeric(args[1], "Statistics.percentile", loc);

            if (!std::isfinite(p) || p < 0.0 || p > 100.0) {
                return make_failure_value(
                    error_msg("Statistics", "percentile", "p must be between 0 and 100"));
            }

            auto vals = sorted_doubles(elems);

            const double rank = (p / 100.0) * static_cast<double>(vals.size() - 1);
            auto lower = static_cast<std::size_t>(std::floor(rank));
            auto upper = static_cast<std::size_t>(std::ceil(rank));

            if (lower == upper) {
                return make_success_value(Value{vals[lower]});
            }

            const double frac = rank - static_cast<double>(lower);

            return make_success_value(Value{vals[lower] + (frac * (vals[upper] - vals[lower]))});
        })
        .func("correlation", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& xs = *expect_array(args[0], "Statistics.correlation", loc)->elements;
            const auto& ys = *expect_array(args[1], "Statistics.correlation", loc)->elements;

            if (xs.size() != ys.size()) {
                return make_failure_value(
                    error_msg("Statistics", "correlation", "arrays must have equal length"));
            }

            if (xs.size() < 2) {
                return make_failure_value(
                    error_msg("Statistics", "correlation", "need at least 2 data points"));
            }

            auto n = static_cast<double>(xs.size());

            double sum_x{0};
            double sum_y{0};

            for (std::size_t i{0}; i < xs.size(); ++i) {
                sum_x += xs[i].to_numeric();
                sum_y += ys[i].to_numeric();
            }

            const double mean_x = sum_x / n;
            const double mean_y = sum_y / n;

            double cov{0};
            double var_x{0};
            double var_y{0};

            for (std::size_t i{0}; i < xs.size(); ++i) {
                const double dx = xs[i].to_numeric() - mean_x;
                const double dy = ys[i].to_numeric() - mean_y;

                cov += dx * dy;
                var_x += dx * dx;
                var_y += dy * dy;
            }

            const double denom = std::sqrt(var_x * var_y);

            if (denom == 0.0) {
                return make_success_value(Value{0.0});
            }

            return make_success_value(Value{cov / denom});
        })
        .func("linear_fit", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& xs = *expect_array(args[0], "Statistics.linear_fit", loc)->elements;
            const auto& ys = *expect_array(args[1], "Statistics.linear_fit", loc)->elements;

            if (xs.size() != ys.size()) {
                return make_failure_value(
                    error_msg("Statistics", "linear_fit", "arrays must have equal length"));
            }

            if (xs.size() < 2) {
                return make_failure_value(
                    error_msg("Statistics", "linear_fit", "need at least 2 data points"));
            }

            const auto n = static_cast<double>(xs.size());

            double sum_x{0};
            double sum_y{0};
            for (std::size_t i{0}; i < xs.size(); ++i) {
                sum_x += xs[i].to_numeric();
                sum_y += ys[i].to_numeric();
            }

            const double mean_x = sum_x / n;
            const double mean_y = sum_y / n;

            double cov{0};
            double var_x{0};
            double var_y{0};
            for (std::size_t i{0}; i < xs.size(); ++i) {
                const double dx = xs[i].to_numeric() - mean_x;
                const double dy = ys[i].to_numeric() - mean_y;

                cov += dx * dy;
                var_x += dx * dx;
                var_y += dy * dy;
            }

            // A zero x-variance means every x is identical — the points form a
            // vertical line with no ordinary least-squares slope.
            if (var_x == 0.0) {
                return make_failure_value(error_msg(
                    "Statistics", "linear_fit", "x values have zero variance (a vertical line)"));
            }

            const double slope = cov / var_x;
            const double intercept = mean_y - (slope * mean_x);

            // R² = cov² / (var_x · var_y).  When the y values are all equal
            // (var_y == 0) the fitted horizontal line matches every point, so
            // the fit is perfect: R² = 1.
            const double r_squared = (var_y == 0.0) ? 1.0 : (cov * cov) / (var_x * var_y);

            auto rec = std::make_shared<RecordValue>();
            rec->type_name = "LineFit";
            rec->fields.emplace_back("slope", Value{slope});
            rec->fields.emplace_back("intercept", Value{intercept});
            rec->fields.emplace_back("r_squared", Value{r_squared});

            return make_success_value(Value{std::move(rec)});
        });
}

} // namespace luma
