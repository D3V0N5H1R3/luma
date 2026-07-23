// Decimal module — exact base-10 decimal arithmetic.
//
// Unlike `number` (IEEE-754 binary floating point, where 0.1 + 0.2 != 0.3),
// the `decimal` type stores base-10 digits exactly, so currency and other
// human-facing arithmetic is correct.  The API is function-based (no operator
// overloading); parsing and division return `result` because they can fail.
// Registered via register_decimal_ns().

#include "runtime/stdlib/math/decimal_module.hpp"

#include <cstdint>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "analysis/source/source_location.hpp"
#include "common/decimal.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/stdlib/common/native_function_containers.hpp"
#include "runtime/stdlib/common/native_function_validation.hpp"

namespace luma {

namespace {

// Wraps a Decimal in an opaque `decimal` Value.
[[nodiscard]] Value make_decimal(Decimal value) {
    return Value{std::make_shared<DecimalValue>(std::move(value))};
}

// Extracts the Decimal payload from a `decimal` argument.  The returned
// reference is owned by `v`, which outlives every call site here.
[[nodiscard]] const Decimal& arg_decimal(const Value& v, std::string_view name,
                                         const SourceLocation& loc) {
    return expect_decimal(v, name, loc)->value;
}

// Clamps a caller-supplied fractional-digit count to the representable range
// [0, k_max_digits] in 64-bit space, so the later narrowing to `int` can never
// overflow (a raw static_cast<int> of a huge integer would wrap to a bogus,
// possibly negative, value).
[[nodiscard]] int clamp_places(std::int64_t places) {
    constexpr auto max_places = static_cast<std::int64_t>(Decimal::k_max_digits);
    if (places < 0) {
        return 0;
    }
    if (places > max_places) {
        return static_cast<int>(Decimal::k_max_digits);
    }
    return static_cast<int>(places);
}

} // namespace

void register_decimal_ns(const EnvPtr& env) {
    ModuleBuilder{"Decimal", env} // ─── Construction ───
        .func("from_string", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& text = expect_string(args[0], "Decimal.from_string", loc);
            auto parsed = Decimal::parse(text);
            if (!parsed) {
                return failure_msg("Decimal", "from_string",
                                   std::format("'{}' is not a valid decimal", text),
                                   error_codes::parse_error);
            }
            return make_success_value(make_decimal(std::move(*parsed)));
        })
        .func("from_number", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto number = expect_numeric(args[0], "Decimal.from_number", loc);
            auto converted = Decimal::from_double(number);
            if (!converted) {
                throw RuntimeError{"Decimal.from_number: cannot convert a non-finite number "
                                   "(NaN or infinity) to a decimal",
                                   loc, "check the value is finite before converting"};
            }
            return make_decimal(std::move(*converted));
        })
        .func("from_integer", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto integer = expect_integer(args[0], "Decimal.from_integer", loc);
            return make_decimal(Decimal{integer});
        })
        // ─── Arithmetic ───
        .func("add", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& lhs = arg_decimal(args[0], "Decimal.add", loc);
            const auto& rhs = arg_decimal(args[1], "Decimal.add", loc);
            return make_decimal(lhs.add(rhs));
        })
        .func("subtract", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& lhs = arg_decimal(args[0], "Decimal.subtract", loc);
            const auto& rhs = arg_decimal(args[1], "Decimal.subtract", loc);
            return make_decimal(lhs.subtract(rhs));
        })
        .func("multiply", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& lhs = arg_decimal(args[0], "Decimal.multiply", loc);
            const auto& rhs = arg_decimal(args[1], "Decimal.multiply", loc);
            auto product = lhs.multiply(rhs);
            if (!product) {
                throw RuntimeError{"Decimal.multiply: result is too large to represent "
                                   "(exceeds the maximum decimal size)",
                                   loc, "reduce the magnitude or precision of the operands"};
            }
            return make_decimal(std::move(*product));
        })
        .func("divide", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& dividend = arg_decimal(args[0], "Decimal.divide", loc);
            const auto& divisor = arg_decimal(args[1], "Decimal.divide", loc);
            const auto scale = expect_integer(args[2], "Decimal.divide", loc);
            if (scale < 0) {
                return failure_msg("Decimal", "divide", "scale must be zero or greater",
                                   error_codes::invalid_argument);
            }
            if (scale > static_cast<std::int64_t>(Decimal::k_max_digits)) {
                return failure_msg("Decimal", "divide", "scale is too large",
                                   error_codes::size_limit_exceeded);
            }
            if (divisor.is_zero()) {
                return failure_msg("Decimal", "divide", "division by zero",
                                   error_codes::division_by_zero);
            }
            // HalfUp is the most intuitive default for beginners (commercial
            // rounding); callers wanting another mode round the result.
            auto quotient = dividend.divide(divisor, static_cast<int>(scale), RoundingMode::HalfUp);
            if (!quotient) {
                return failure_msg("Decimal", "divide", "result is too large to represent",
                                   error_codes::size_limit_exceeded);
            }
            return make_success_value(make_decimal(std::move(*quotient)));
        })
        // ─── Rounding ───
        .func("round", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& value = arg_decimal(args[0], "Decimal.round", loc);
            const auto places = expect_integer(args[1], "Decimal.round", loc);
            const auto& mode_name = expect_string(args[2], "Decimal.round", loc);
            const auto mode = parse_rounding_mode(mode_name);
            if (!mode) {
                throw RuntimeError{
                    std::format("Decimal.round: unknown rounding mode '{}'", mode_name), loc,
                    "use one of: half_up, half_even, half_down, up, down, ceiling, floor"};
            }
            return make_decimal(value.round(clamp_places(places), *mode));
        })
        // ─── Comparison ───
        .func("compare", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& lhs = arg_decimal(args[0], "Decimal.compare", loc);
            const auto& rhs = arg_decimal(args[1], "Decimal.compare", loc);
            return Value{static_cast<std::int64_t>(lhs.compare(rhs))};
        })
        .func("equals", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& lhs = arg_decimal(args[0], "Decimal.equals", loc);
            const auto& rhs = arg_decimal(args[1], "Decimal.equals", loc);
            return Value{lhs.equals(rhs)};
        })
        // ─── Predicates & sign ───
        .func("is_zero", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{arg_decimal(args[0], "Decimal.is_zero", loc).is_zero()};
        })
        .func("is_negative", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{arg_decimal(args[0], "Decimal.is_negative", loc).is_negative()};
        })
        .func("negate", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return make_decimal(arg_decimal(args[0], "Decimal.negate", loc).negate());
        })
        .func("absolute", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return make_decimal(arg_decimal(args[0], "Decimal.absolute", loc).absolute());
        })
        .func("scale", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{
                static_cast<std::int64_t>(arg_decimal(args[0], "Decimal.scale", loc).scale())};
        })
        // ─── Conversion ───
        .func("to_string", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{arg_decimal(args[0], "Decimal.to_string", loc).to_string()};
        })
        .func("to_number", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{arg_decimal(args[0], "Decimal.to_number", loc).to_double()};
        });
}

} // namespace luma
