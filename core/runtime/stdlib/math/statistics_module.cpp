// Statistics module — core descriptive statistics: central tendency (mean,
// median, mode) and dispersion (variance, standard_deviation).  Registered via
// register_statistics_ns().

#include "runtime/stdlib/math/statistics_module.hpp"

#include <algorithm>
#include <cmath>
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
        });
}

} // namespace luma
