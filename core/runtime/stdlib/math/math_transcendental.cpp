// Math module — transcendental functions (atan2, hypot, log, roots, tanh)
// and interpolation.  Registered via register_math_transcendental().

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iterator>
#include <limits>
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

// Transcendental functions (atan2, hypot, log, roots, tanh) and interpolation.
void register_math_transcendental(const EnvPtr& env) {
    ModuleBuilder{"Math", env} // Math.atan2(y, x) -> result<number>
        .func("atan2", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto y = expect_numeric(args[0], "Math.atan2", loc);
            const auto x = expect_numeric(args[1], "Math.atan2", loc);
            const auto result = std::atan2(y, x);

            if (!stdlib::is_valid_numeric(result)) {
                return make_failure_value(error_msg("Math", "atan2", "result is not a number"));
            }

            return make_success_value(Value{result});
        })
        // Math.hypot(x, y) -> number
        .func("hypot", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto x = expect_numeric(args[0], "Math.hypot", loc);
            const auto y = expect_numeric(args[1], "Math.hypot", loc);

            return Value{std::hypot(x, y)};
        })
        // Math.log(base, value) -> result<number>
        .func("log", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto base = expect_numeric(args[0], "Math.log", loc);
            const auto value = expect_numeric(args[1], "Math.log", loc);

            if (base <= 0.0 || base == 1.0) {
                return make_failure_value(
                    error_msg("Math", "log", "base must be positive and not 1"));
            }

            if (value <= 0.0) {
                return make_failure_value(error_msg("Math", "log", "value must be positive"));
            }

            const auto result = std::log(value) / std::log(base);

            if (!stdlib::is_valid_numeric(result)) {
                return make_failure_value(
                    error_msg("Math", "log", "result is not a finite number"));
            }

            return make_success_value(Value{result});
        })
        // Math.cube_root(value) -> number
        .func("cube_root", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto value = expect_numeric(args[0], "Math.cube_root", loc);

            return Value{std::cbrt(value)};
        })
        // Math.hyperbolic_sine(value) -> result<number>
        .checked_unary("hyperbolic_sine", [](double x) { return std::sinh(x); })
        .checked_unary("hyperbolic_cosine", [](double x) { return std::cosh(x); })
        // Math.hyperbolic_tangent(value) -> number
        .func("hyperbolic_tangent", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto value = expect_numeric(args[0], "Math.hyperbolic_tangent", loc);

            return Value{std::tanh(value)};
        })
        // Math.remap(value, in_min, in_max, out_min, out_max) -> result<number>
        .func("remap", 5)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto value = expect_numeric(args[0], "Math.remap", loc);
            const auto in_min = expect_numeric(args[1], "Math.remap", loc);
            const auto in_max = expect_numeric(args[2], "Math.remap", loc);
            const auto out_min = expect_numeric(args[3], "Math.remap", loc);
            const auto out_max = expect_numeric(args[4], "Math.remap", loc);

            if (in_min == in_max) {
                return make_failure_value(error_msg("Math", "remap", "input range is zero"));
            }

            const auto t = (value - in_min) / (in_max - in_min);
            const auto result = out_min + (t * (out_max - out_min));

            return make_success_value(Value{result});
        })
        // Math.smooth_step(edge0, edge1, x) -> result<number>
        .func("smooth_step", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto edge0 = expect_numeric(args[0], "Math.smooth_step", loc);
            const auto edge1 = expect_numeric(args[1], "Math.smooth_step", loc);
            const auto x = expect_numeric(args[2], "Math.smooth_step", loc);

            if (edge0 == edge1) {
                return make_failure_value(
                    error_msg("Math", "smooth_step", "edge0 must not equal edge1"));
            }

            const auto t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);

            return make_success_value(Value{t * t * (3.0 - (2.0 * t))});
        })
        // Math.log_base(value, base) -> result<number>
        .func("log_base", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto value = expect_numeric(args[0], "Math.log_base", loc);
            const auto base = expect_numeric(args[1], "Math.log_base", loc);

            if (base <= 0.0 || base == 1.0) {
                return make_failure_value(
                    error_msg("Math", "log_base", "base must be positive and not 1"));
            }

            if (value <= 0.0) {
                return make_failure_value(error_msg("Math", "log_base", "value must be positive"));
            }

            const auto result = std::log(value) / std::log(base);

            if (!stdlib::is_valid_numeric(result)) {
                return make_failure_value(
                    error_msg("Math", "log_base", "result is not a finite number"));
            }

            return make_success_value(Value{result});
        })
        // Math.nth_root(value, n) -> result<number>
        .func("nth_root", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto value = expect_numeric(args[0], "Math.nth_root", loc);
            const auto n = expect_numeric(args[1], "Math.nth_root", loc);

            if (n == 0.0) {
                return make_failure_value(error_msg("Math", "nth_root", "root degree must not be 0"));
            }

            double result{0.0};

            if (value < 0.0) {
                // A negative radicand only has a real root for an odd-integer degree.
                const bool odd_integer = (std::floor(n) == n) && (std::fmod(n, 2.0) != 0.0);

                if (!odd_integer) {
                    return make_failure_value(
                        error_msg("Math", "nth_root", "even or fractional root of a negative value"));
                }

                result = -std::pow(-value, 1.0 / n);
            } else {
                result = std::pow(value, 1.0 / n);
            }

            if (!stdlib::is_valid_numeric(result)) {
                return make_failure_value(
                    error_msg("Math", "nth_root", "result is not a finite number"));
            }

            return make_success_value(Value{result});
        })
        // Math.wrap(x, lo, hi) -> number
        .func("wrap", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto x = expect_numeric(args[0], "Math.wrap", loc);
            const auto lo = expect_numeric(args[1], "Math.wrap", loc);
            const auto hi = expect_numeric(args[2], "Math.wrap", loc);

            const auto range = hi - lo;

            if (range <= 0.0) {
                return Value{lo};
            }

            auto wrapped = std::fmod(x - lo, range);

            if (wrapped < 0.0) {
                wrapped += range;
            }

            return Value{lo + wrapped};
        })
        // Math.normalize_angle(radians) -> number
        .func("normalize_angle", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto radians = expect_numeric(args[0], "Math.normalize_angle", loc);
            constexpr double two_pi = 2.0 * std::numbers::pi;

            auto r = std::fmod(radians + std::numbers::pi, two_pi);

            if (r < 0.0) {
                r += two_pi;
            }

            return Value{r - std::numbers::pi};
        })
        // Math.angle_difference(a, b) -> number
        .func("angle_difference", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto a = expect_numeric(args[0], "Math.angle_difference", loc);
            const auto b = expect_numeric(args[1], "Math.angle_difference", loc);
            constexpr double two_pi = 2.0 * std::numbers::pi;

            auto r = std::fmod((a - b) + std::numbers::pi, two_pi);

            if (r < 0.0) {
                r += two_pi;
            }

            return Value{r - std::numbers::pi};
        })
        // Math.sigmoid(x) -> number
        .func("sigmoid", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto x = expect_numeric(args[0], "Math.sigmoid", loc);

            return Value{1.0 / (1.0 + std::exp(-x))};
        })
        // Math.gamma(x) -> result<number>
        .func("gamma", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto x = expect_numeric(args[0], "Math.gamma", loc);

            // The gamma function has poles at zero and the negative integers.
            if (x <= 0.0 && std::floor(x) == x) {
                return make_failure_value(
                    error_msg("Math", "gamma", "gamma is undefined at non-positive integers"));
            }

            const auto result = std::tgamma(x);

            if (!stdlib::is_valid_numeric(result)) {
                return make_failure_value(
                    error_msg("Math", "gamma", "result is not a finite number"));
            }

            return make_success_value(Value{result});
        })
        // Math.erf(x) -> number
        .func("erf", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto x = expect_numeric(args[0], "Math.erf", loc);

            return Value{std::erf(x)};
        });
}

} // namespace luma
