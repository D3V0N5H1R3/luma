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

// Resolves a rounding-mode argument that may be either a `Decimal.RoundingMode`
// choice variant or one of the legacy lowercase mode strings, mirroring the dual
// string/choice acceptance of Log.set_level.  Throws a RuntimeError on an
// unrecognised value: an invalid string is a programmer typo, and an invalid
// choice variant is impossible because the type checker guarantees it — so this
// preserves today's "bad mode is a runtime error, not a domain failure" contract
// of Decimal.round.
[[nodiscard]] RoundingMode resolve_rounding_mode(const Value& mode_arg, std::string_view fn,
                                                 const SourceLocation& loc) {
    if (mode_arg.is_choice()) {
        const auto& variant = mode_arg.as_choice()->variant;
        if (auto mode = rounding_mode_from_variant(variant)) {
            return *mode;
        }
        throw RuntimeError{
            std::format("{}: unknown rounding mode 'Decimal.RoundingMode.{}'", fn, variant), loc,
            "use a Decimal.RoundingMode variant: HalfUp, HalfDown, HalfEven, Up, Down, "
            "Ceiling, Floor"};
    }
    if (mode_arg.is_string()) {
        const auto& name = mode_arg.as_string();
        if (auto mode = parse_rounding_mode(name)) {
            return *mode;
        }
        throw RuntimeError{std::format("{}: unknown rounding mode '{}'", fn, name), loc,
                           "use one of: half_up, half_even, half_down, up, down, ceiling, floor"};
    }
    throw RuntimeError{
        std::format("{}: mode must be a Decimal.RoundingMode or a mode string", fn), loc,
        "pass a Decimal.RoundingMode variant (e.g. Decimal.RoundingMode.HalfUp) or a string "
        "(e.g. \"half_up\")"};
}

// ─── Decimal.Error choice support ────────────────────────────────────────────
// Builds a result<decimal, Decimal.Error> failure whose error value is the
// Decimal.Error choice for `variant` (runtime short name "Error", matching the
// postamble registration).  The variant names must match the Decimal.Error
// choice declared in stdlib_type_arities.cpp exactly.
[[nodiscard]] Value make_decimal_error_failure(std::string_view variant) {
    auto cv = std::make_shared<ChoiceValue>();
    cv->type_name = "Error";
    cv->variant = std::string{variant};

    return Value{ResultValue::failure(Value{std::move(cv)})};
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
        .func("from_string_typed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            // Opt-in typed-error variant of from_string: an unparseable string
            // fails with a Decimal.Error choice (result<decimal, Decimal.Error>)
            // instead of a string message.  Parsing has exactly one failure mode,
            // so it always classifies as InvalidFormat.
            const auto& text = expect_string(args[0], "Decimal.from_string_typed", loc);
            auto parsed = Decimal::parse(text);
            if (!parsed) {
                return make_decimal_error_failure("InvalidFormat");
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
        .func("divide_typed", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            // Opt-in typed-error variant of divide: failures surface a
            // Decimal.Error choice (result<decimal, Decimal.Error>) so a caller
            // can distinguish DivisionByZero from an unrepresentable scale
            // (PrecisionExceeded) or an oversized result (Overflow).
            const auto& dividend = arg_decimal(args[0], "Decimal.divide_typed", loc);
            const auto& divisor = arg_decimal(args[1], "Decimal.divide_typed", loc);
            const auto scale = expect_integer(args[2], "Decimal.divide_typed", loc);
            if (scale < 0 || scale > static_cast<std::int64_t>(Decimal::k_max_digits)) {
                return make_decimal_error_failure("PrecisionExceeded");
            }
            if (divisor.is_zero()) {
                return make_decimal_error_failure("DivisionByZero");
            }
            auto quotient = dividend.divide(divisor, static_cast<int>(scale), RoundingMode::HalfUp);
            if (!quotient) {
                return make_decimal_error_failure("Overflow");
            }
            return make_success_value(make_decimal(std::move(*quotient)));
        })
        .func("divide_with", 4)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& dividend = arg_decimal(args[0], "Decimal.divide_with", loc);
            const auto& divisor = arg_decimal(args[1], "Decimal.divide_with", loc);
            const auto scale = expect_integer(args[2], "Decimal.divide_with", loc);
            // Resolve the mode before the domain checks so a mode typo (a
            // programmer error) surfaces even when the divisor is zero.
            const auto mode = resolve_rounding_mode(args[3], "Decimal.divide_with", loc);
            if (scale < 0) {
                return failure_msg("Decimal", "divide_with", "scale must be zero or greater",
                                   error_codes::invalid_argument);
            }
            if (scale > static_cast<std::int64_t>(Decimal::k_max_digits)) {
                return failure_msg("Decimal", "divide_with", "scale is too large",
                                   error_codes::size_limit_exceeded);
            }
            if (divisor.is_zero()) {
                return failure_msg("Decimal", "divide_with", "division by zero",
                                   error_codes::division_by_zero);
            }
            auto quotient = dividend.divide(divisor, static_cast<int>(scale), mode);
            if (!quotient) {
                return failure_msg("Decimal", "divide_with", "result is too large to represent",
                                   error_codes::size_limit_exceeded);
            }
            return make_success_value(make_decimal(std::move(*quotient)));
        })
        .func("power", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& base = arg_decimal(args[0], "Decimal.power", loc);
            const auto exponent = expect_integer(args[1], "Decimal.power", loc);
            if (exponent < 0) {
                return failure_msg("Decimal", "power", "exponent must be zero or greater",
                                   error_codes::invalid_argument);
            }

            // Bound the iteration count.  Repeated multiplication normally
            // terminates early via overflow for any |base| > 1, but degenerate
            // bases (0, 1, -1) never grow the coefficient, so without this cap a
            // huge exponent would spin in a tight native loop (a DoS).  Any
            // non-degenerate base overflows well before this bound, so the cap
            // never rejects a computation that would otherwise succeed.
            constexpr std::int64_t k_max_power_exponent = 1'000'000;
            if (exponent > k_max_power_exponent) {
                return failure_msg("Decimal", "power", "exponent is too large (maximum is 1000000)",
                                   error_codes::size_limit_exceeded);
            }

            Decimal result{static_cast<std::int64_t>(1)};
            for (std::int64_t i{0}; i < exponent; ++i) {
                auto next = result.multiply(base);
                if (!next) {
                    return failure_msg("Decimal", "power", "result is too large to represent",
                                       error_codes::size_limit_exceeded);
                }
                result = std::move(*next);
            }
            return make_success_value(make_decimal(std::move(result)));
        })
        .func("remainder", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& dividend = arg_decimal(args[0], "Decimal.remainder", loc);
            const auto& divisor = arg_decimal(args[1], "Decimal.remainder", loc);
            if (divisor.is_zero()) {
                return failure_msg("Decimal", "remainder", "division by zero",
                                   error_codes::division_by_zero);
            }
            // Truncate the quotient toward zero (Down) so the remainder takes the
            // sign of the dividend, then r = dividend - trunc(dividend / divisor) * divisor.
            auto quotient = dividend.divide(divisor, 0, RoundingMode::Down);
            if (!quotient) {
                return failure_msg("Decimal", "remainder", "result is too large to represent",
                                   error_codes::size_limit_exceeded);
            }
            auto product = quotient->multiply(divisor);
            if (!product) {
                return failure_msg("Decimal", "remainder", "result is too large to represent",
                                   error_codes::size_limit_exceeded);
            }
            return make_success_value(make_decimal(dividend.subtract(*product)));
        })
        .func("sum", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& arr = expect_array(args[0], "Decimal.sum", loc);
            Decimal acc{static_cast<std::int64_t>(0)};
            for (const auto& element : *arr->elements) {
                acc = acc.add(arg_decimal(element, "Decimal.sum", loc));
            }
            return make_decimal(std::move(acc));
        })
        .func("product", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& arr = expect_array(args[0], "Decimal.product", loc);
            Decimal acc{static_cast<std::int64_t>(1)};
            for (const auto& element : *arr->elements) {
                auto next = acc.multiply(arg_decimal(element, "Decimal.product", loc));
                if (!next) {
                    throw RuntimeError{"Decimal.product: result is too large to represent "
                                       "(exceeds the maximum decimal size)",
                                       loc, "reduce the magnitude or precision of the operands"};
                }
                acc = std::move(*next);
            }
            return make_decimal(std::move(acc));
        })
        // ─── Rounding ───
        .func("round", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& value = arg_decimal(args[0], "Decimal.round", loc);
            const auto places = expect_integer(args[1], "Decimal.round", loc);
            const auto mode = resolve_rounding_mode(args[2], "Decimal.round", loc);
            return make_decimal(value.round(clamp_places(places), mode));
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
        .func("less_than", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& lhs = arg_decimal(args[0], "Decimal.less_than", loc);
            const auto& rhs = arg_decimal(args[1], "Decimal.less_than", loc);
            return Value{lhs.compare(rhs) < 0};
        })
        .func("greater_than", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& lhs = arg_decimal(args[0], "Decimal.greater_than", loc);
            const auto& rhs = arg_decimal(args[1], "Decimal.greater_than", loc);
            return Value{lhs.compare(rhs) > 0};
        })
        .func("less_or_equal", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& lhs = arg_decimal(args[0], "Decimal.less_or_equal", loc);
            const auto& rhs = arg_decimal(args[1], "Decimal.less_or_equal", loc);
            return Value{lhs.compare(rhs) <= 0};
        })
        .func("greater_or_equal", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& lhs = arg_decimal(args[0], "Decimal.greater_or_equal", loc);
            const auto& rhs = arg_decimal(args[1], "Decimal.greater_or_equal", loc);
            return Value{lhs.compare(rhs) >= 0};
        })
        .func("min", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& lhs = arg_decimal(args[0], "Decimal.min", loc);
            const auto& rhs = arg_decimal(args[1], "Decimal.min", loc);
            return make_decimal(lhs.compare(rhs) <= 0 ? lhs : rhs);
        })
        .func("max", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& lhs = arg_decimal(args[0], "Decimal.max", loc);
            const auto& rhs = arg_decimal(args[1], "Decimal.max", loc);
            return make_decimal(lhs.compare(rhs) >= 0 ? lhs : rhs);
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
        .func("is_positive", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{arg_decimal(args[0], "Decimal.is_positive", loc).sign() > 0};
        })
        .func("sign", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            return Value{
                static_cast<std::int64_t>(arg_decimal(args[0], "Decimal.sign", loc).sign())};
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
        })
        .func("to_integer", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& value = arg_decimal(args[0], "Decimal.to_integer", loc);
            const auto canonical = value.canonical();
            if (canonical.scale() > 0) {
                return failure_msg("Decimal", "to_integer",
                                   "value has a fractional part; round it first "
                                   "(e.g. Decimal.round(d, 0, Decimal.RoundingMode.Down))",
                                   error_codes::invalid_argument);
            }
            const auto text = canonical.to_string();
            try {
                std::size_t pos{0};
                const auto parsed = std::stoll(text, &pos);
                if (pos == text.size()) {
                    return make_success_value(Value{static_cast<std::int64_t>(parsed)});
                }
            } catch (const std::exception&) { // NOLINT(bugprone-empty-catch)
                // Fall through to the out-of-range failure below.
            }
            return failure_msg("Decimal", "to_integer", "value is out of integer range",
                               error_codes::overflow);
        });
}

} // namespace luma
