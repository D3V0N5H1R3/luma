// Statistics module — core descriptive statistics: central tendency (mean,
// median, mode) and dispersion (variance, standard_deviation).  Registered via
// register_statistics_ns().

#include "runtime/stdlib/math/statistics_module.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <numeric>
#include <span>
#include <string_view>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/numeric_helpers.hpp"

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

// Compute the arithmetic mean.  The plain sum divided by the count is the
// correctly rounded-as-possible answer users expect (mean([0, 1, 6, 1]) is
// exactly 2), so use it whenever the sum is finite.  Only when the sum
// overflows — finite inputs near DBL_MAX such as [1e308, 1e308] — fall back to
// Σ (x / n): each term is at most DBL_MAX / n, so the sum cannot overflow.  A
// non-finite input keeps the result non-finite, which the caller reports as a
// failure.
[[nodiscard]] double compute_mean(std::span<const Value> elems) {
    const auto count = static_cast<double>(elems.size());
    double sum{0.0};

    for (const auto& elem : elems) {
        sum += elem.to_numeric();
    }

    if (std::isfinite(sum)) {
        return sum / count;
    }

    double scaled_sum{0.0};

    for (const auto& elem : elems) {
        scaled_sum += elem.to_numeric() / count;
    }

    return scaled_sum;
}

// Population variance expressed as `normalized * scale * scale`, so the standard
// deviation can be taken as `sqrt(normalized) * scale` without squaring an
// overflowing variance first.  scale is 1 unless the direct computation
// overflowed.
struct ScaledVariance {
    double normalized;
    double scale;
};

// Two-pass population variance: the mean first, then the mean of the squared
// deviations from it (no catastrophic cancellation of a separate sum of
// squares).  When a squared deviation or their sum overflows, every value is
// divided by the largest magnitude first, so the scaled values lie in
// [-1, 1] and their sum of squared deviations is at most 4n.
[[nodiscard]] ScaledVariance compute_variance(std::span<const Value> elems) {
    const auto count = static_cast<double>(elems.size());
    const double mean = compute_mean(elems);

    if (!std::isfinite(mean)) {
        return {.normalized = mean, .scale = 1.0};
    }

    double squares{0.0};

    for (const auto& elem : elems) {
        const double deviation = elem.to_numeric() - mean;
        squares += deviation * deviation;
    }

    if (std::isfinite(squares)) {
        return {.normalized = squares / count, .scale = 1.0};
    }

    // A finite mean implies finite inputs, so the scale is finite and non-zero
    // (an all-zero array cannot overflow).
    double scale{0.0};

    for (const auto& elem : elems) {
        scale = std::max(scale, std::abs(elem.to_numeric()));
    }

    double scaled_sum{0.0};

    for (const auto& elem : elems) {
        scaled_sum += elem.to_numeric() / scale;
    }

    const double scaled_mean = scaled_sum / count;
    double scaled_squares{0.0};

    for (const auto& elem : elems) {
        const double deviation = (elem.to_numeric() / scale) - scaled_mean;
        scaled_squares += deviation * deviation;
    }

    return {.normalized = scaled_squares / count, .scale = scale};
}

[[nodiscard]] double population_variance(std::span<const Value> elems) {
    const auto [normalized, scale] = compute_variance(elems);

    // Overflows to +inf (reported as a failure) when the variance itself is
    // not representable.
    return normalized * scale * scale;
}

[[nodiscard]] double population_standard_deviation(std::span<const Value> elems) {
    const auto [normalized, scale] = compute_variance(elems);

    return std::sqrt(normalized) * scale;
}

// Wrap a computed statistic as success, or failure when it is NaN/±inf (from
// non-finite inputs or a genuinely unrepresentable result) — consistent with
// Math.power / Math.round_to, which never report a non-real number as success.
[[nodiscard]] Value finite_statistic(double value, std::string_view qualified_name) {
    if (!stdlib::is_valid_numeric(value)) {
        return make_failure_value(std::format("{}: result is not a finite number", qualified_name));
    }

    return make_success_value(Value{value});
}

} // namespace

// Statistics: central tendency (mean, median, mode) and dispersion (variance,
// standard_deviation).
void register_statistics_ns(const EnvPtr& env) {
    ModuleBuilder{"Statistics", env}
        .func("mean", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Statistics.mean", loc)->elements;

            if (auto fail = check_not_empty(elems, "Statistics.mean")) {
                return *std::move(fail);
            }

            return finite_statistic(compute_mean(elems), "Statistics.mean");
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
                // std::midpoint cannot overflow, unlike (a + b) / 2 for
                // values near DBL_MAX.
                return finite_statistic(std::midpoint(vals[(n / 2) - 1], vals[n / 2]),
                                        "Statistics.median");
            }

            return finite_statistic(vals[n / 2], "Statistics.median");
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

            return finite_statistic(best_val, "Statistics.mode");
        })
        .func("variance", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems = *expect_array(args[0], "Statistics.variance", loc)->elements;

            if (auto fail = check_not_empty(elems, "Statistics.variance")) {
                return *std::move(fail);
            }

            return finite_statistic(population_variance(elems), "Statistics.variance");
        })
        .func("standard_deviation", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& elems =
                *expect_array(args[0], "Statistics.standard_deviation", loc)->elements;

            if (auto fail = check_not_empty(elems, "Statistics.standard_deviation")) {
                return *std::move(fail);
            }

            return finite_statistic(population_standard_deviation(elems),
                                    "Statistics.standard_deviation");
        });
}

} // namespace luma
