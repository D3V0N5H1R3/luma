// Math module — statistics: central tendency, dispersion, sum, and
// correlation.  Registered via register_math_statistics().

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
#include <optional>
#include <string_view>

#include "analysis/source/source_location.hpp"
#include "common/overflow.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/numeric_helpers.hpp"
#include "runtime/stdlib/math/math_module.hpp"

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
// standard_deviation, percentile), sum, and correlation.
void register_math_statistics(const EnvPtr& env) {
    ModuleBuilder{"Math", env}
        .func("mean", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Math.mean", loc)->elements;

            if (auto fail = check_not_empty(elems, "Math.mean")) {
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
            const auto& elems = *expect_array(args[0], "Math.median", loc)->elements;

            if (auto fail = check_not_empty(elems, "Math.median")) {
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
        .func("sum", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Math.sum", loc)->elements;

            bool all_int{true};
            std::int64_t int_sum{0};
            double dbl_sum{0.0};

            for (const auto& elem : elems) {
                if (elem.is_integer()) {
                    if (all_int && would_overflow_add(int_sum, elem.as_integer())) {
                        all_int = false;
                    }
                    if (all_int) {
                        int_sum += elem.as_integer();
                    }
                    dbl_sum += static_cast<double>(elem.as_integer());
                } else if (elem.is_number()) {
                    all_int = false;

                    dbl_sum += elem.as_number();
                } else {
                    return make_failure_value(error_msg("Math", "sum", "non-numeric element"));
                }
            }

            return make_success_value(all_int ? Value{int_sum} : Value{dbl_sum});
        })
        .func("mode", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Math.mode", loc)->elements;

            if (auto fail = check_not_empty(elems, "Math.mode")) {
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
            const auto& elems = *expect_array(args[0], "Math.variance", loc)->elements;

            if (auto fail = check_not_empty(elems, "Math.variance")) {
                return *std::move(fail);
            }

            return make_success_value(Value{compute_variance(elems)});
        })
        .func("standard_deviation", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Math.standard_deviation", loc)->elements;

            if (auto fail = check_not_empty(elems, "Math.standard_deviation")) {
                return *std::move(fail);
            }

            return make_success_value(Value{std::sqrt(compute_variance(elems))});
        })
        .func("summarize", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Math.summarize", loc)->elements;

            if (auto fail = check_not_empty(elems, "Math.summarize")) {
                return *std::move(fail);
            }

            // One pass: sort once for min / max / median, then reuse the sorted
            // values for the mean.  Variance is computed over the original
            // elements (order-independent), matching Math.standard_deviation.
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
        .func("percentile", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Math.percentile", loc)->elements;

            if (auto fail = check_not_empty(elems, "Math.percentile")) {
                return *std::move(fail);
            }

            auto p = expect_numeric(args[1], "Math.percentile", loc);

            if (!std::isfinite(p) || p < 0.0 || p > 100.0) {
                return make_failure_value(
                    error_msg("Math", "percentile", "p must be between 0 and 100"));
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
            const auto& xs = *expect_array(args[0], "Math.correlation", loc)->elements;
            const auto& ys = *expect_array(args[1], "Math.correlation", loc)->elements;

            if (xs.size() != ys.size()) {
                return make_failure_value(
                    error_msg("Math", "correlation", "arrays must have equal length"));
            }

            if (xs.size() < 2) {
                return make_failure_value(
                    error_msg("Math", "correlation", "need at least 2 data points"));
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
        });
}

} // namespace luma
