// vm_dispatch_arithmetic.cpp — Arithmetic, comparison, and logical
// opcode handler methods.
//
// Extracted from vm_helpers.cpp as part of the VM dispatch split.
// Contains:
//   - numeric_binary_op, compare_values
//   - handle_divide, handle_int_divide, handle_modulo
//   - validate_nonzero_divisor
//   - handle_concatenate

#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <string>

#include "common/comparison_ops.hpp"
#include "common/overflow.hpp"
#include "common/resource_limits.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_error_messages.hpp"

namespace luma {

// ─────────── Anonymous helpers ───────────

namespace {

// Overflow-checked integer +, -, *: std::nullopt when the exact result does
// not fit in an int64.  The VM raises "integer overflow in <operation>" for
// that case instead of widening to `number`, so an `integer` stays an integer.
[[nodiscard]] std::optional<std::int64_t> checked_integer_op(Op op, std::int64_t l,
                                                             std::int64_t r) noexcept {
    switch (op) {
        case Op::Add:
            if (would_overflow_add(l, r)) {
                return std::nullopt;
            }
            return l + r;
        case Op::Subtract:
            if (would_overflow_sub(l, r)) {
                return std::nullopt;
            }
            return l - r;
        case Op::Multiply:
            if (would_overflow_mul(l, r)) {
                return std::nullopt;
            }
            return l * r;
        default:
            return std::nullopt;
    }
}

} // namespace

// ─────────── Per-operation numeric helpers ───────────

namespace {

/// Mixed-numeric binary operation (at least one `number` operand): promotes
/// operands to double, applies DblOp, and checks for overflow (finite inputs
/// producing a non-finite result).  Integer/integer operands are handled by
/// the caller (checked_integer_op) before reaching here.
template <typename DblOp>
[[nodiscard]] Value numeric_binary(const Value& a, const Value& b, DblOp dbl_op) {
    if ((a.is_integer() || a.is_number()) && (b.is_integer() || b.is_number())) {
        const auto l = a.is_integer() ? static_cast<double>(a.as_integer()) : a.as_number();
        const auto r = b.is_integer() ? static_cast<double>(b.as_integer()) : b.as_number();
        const auto result = dbl_op(l, r);

        // NOTE: Luma uses IEEE 754 exact equality; no epsilon-based comparison
        // is provided. Users should use Math.approximately_equal() for fuzzy
        // comparisons.
        if (std::isfinite(l) && std::isfinite(r) && !std::isfinite(result)) {
            return Value{}; // Sentinel — caller detects and reports overflow.
        }

        return Value{result};
    }

    return Value{}; // Unsupported combination.
}

/// Handles addition for mixed numeric type combinations plus string concatenation.
[[nodiscard]] Value numeric_add(const Value& a, const Value& b) {
    auto result = numeric_binary(a, b, std::plus<double>{});

    if (result.is_some()) {
        return result;
    }

    if (a.is_string() && b.is_string()) {
        if (!check_string_concat_size(a.as_string().size(), b.as_string().size())) {
            return Value{}; // Sentinel — caller detects and reports.
        }

        return Value{a.as_string() + b.as_string()};
    }

    return Value{}; // Unsupported combination.
}

/// Handles subtraction for mixed numeric type combinations.
[[nodiscard]] Value numeric_subtract(const Value& a, const Value& b) {
    return numeric_binary(a, b, std::minus<double>{});
}

/// Handles multiplication for mixed numeric type combinations.
/// String repetition is handled by the caller.
[[nodiscard]] Value numeric_multiply(const Value& a, const Value& b) {
    return numeric_binary(a, b, std::multiplies<double>{});
}

/// Returns true if the operand is numeric (integer or number).
[[nodiscard]] bool is_numeric(const Value& v) {
    return v.is_integer() || v.is_number();
}

/// Returns true if both operands are numeric (integer or float).
[[nodiscard]] bool both_numeric(const Value& a, const Value& b) {
    return is_numeric(a) && is_numeric(b);
}

/// Handles string repetition: "abc" * 3 or 3 * "abc".
/// Returns nullopt if the operands are not a string/integer pair.
[[nodiscard]] std::optional<Value> try_string_repeat(const Value& a, const Value& b) {
    const std::string* str = nullptr;
    std::int64_t count = 0;

    if (a.is_string() && b.is_integer()) {
        str = &a.as_string();
        count = b.as_integer();
    } else if (a.is_integer() && b.is_string()) {
        str = &b.as_string();
        count = a.as_integer();
    }

    if (str == nullptr) {
        return std::nullopt;
    }

    if (count <= 0) {
        return Value{std::string{}};
    }

    if (!str->empty() &&
        static_cast<std::size_t>(count) > ResourceLimits::max_string_size / str->size()) {
        return std::nullopt; // Sentinel — caller reports overflow.
    }

    const auto total = str->size() * static_cast<std::size_t>(count);

    if (total > ResourceLimits::max_string_size) {
        return std::nullopt; // Sentinel — caller reports overflow.
    }

    std::string repeated;
    repeated.reserve(total);

    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
        repeated.append(*str);
    }

    return Value{std::move(repeated)};
}

/// Maps an arithmetic Op to its verb for error messages.
[[nodiscard]] constexpr std::string_view op_verb(Op op) noexcept {
    switch (op) {
        case Op::Add:
            return "add";
        case Op::Subtract:
            return "subtract";
        case Op::Multiply:
            return "multiply";
        default:
            return "operate on";
    }
}

/// Maps an arithmetic Op to its noun for overflow messages ("integer overflow
/// in addition", "number overflow in multiplication", …).
[[nodiscard]] constexpr std::string_view op_noun(Op op) noexcept {
    switch (op) {
        case Op::Add:
            return "addition";
        case Op::Subtract:
            return "subtraction";
        case Op::Multiply:
            return "multiplication";
        case Op::Divide:
            return "division";
        default:
            return "arithmetic";
    }
}

} // namespace

// ─────────── Arithmetic dispatch ───────────

Value VM::numeric_binary_op(const Value& a, const Value& b, Op op) const {
    // Hot path: integer op integer.  Overflow is an error, never a silent
    // widening to `number` (User Manual §29, "integer overflow in …").
    if (a.is_integer() && b.is_integer()) [[likely]] {
        if (const auto result = checked_integer_op(op, a.as_integer(), b.as_integer())) [[likely]] {
            return Value{*result};
        }

        runtime_error(vm_errors::integer_overflow_in(op_noun(op)),
                      vm_errors::hint_integer_overflow);
    }

    switch (op) {
        case Op::Add: {
            auto result = numeric_add(a, b);
            if (result.is_some()) [[likely]] {
                return result;
            }

            if (a.is_string() && b.is_string()) [[unlikely]] {
                runtime_error(vm_errors::string_concat_exceeds_max);
            }

            if (both_numeric(a, b)) [[unlikely]] {
                runtime_error(vm_errors::number_overflow_in(op_noun(op)));
            }

            break;
        }
        case Op::Subtract: {
            auto result = numeric_subtract(a, b);
            if (result.is_some()) [[likely]] {
                return result;
            }

            if (both_numeric(a, b)) [[unlikely]] {
                runtime_error(vm_errors::number_overflow_in(op_noun(op)));
            }

            break;
        }
        case Op::Multiply: {
            auto result = numeric_multiply(a, b);
            if (result.is_some()) [[likely]] {
                return result;
            }

            if (auto repeated = try_string_repeat(a, b)) {
                return *repeated;
            }

            // String repetition was attempted but exceeded size limit.
            if (a.is_string() || b.is_string()) [[unlikely]] {
                runtime_error(vm_errors::string_repetition_exceeds_max);
            }

            if (both_numeric(a, b)) [[unlikely]] {
                runtime_error(vm_errors::number_overflow_in(op_noun(op)));
            }

            break;
        }
        default:
            break;
    }

    runtime_error(
        vm_errors::cannot_operate_on(op_verb(op), a.display_type_name(), b.display_type_name()));
}

// ─────────── Comparison ───────────

Value VM::compare_values(const Value& a, const Value& b, Op op) const {
    if (a.is_integer() && b.is_integer()) [[likely]] {
        return Value{apply_comparison(a.as_integer(), b.as_integer(), op)};
    }

    if (a.is_string() && b.is_string()) {
        return Value{apply_comparison(a.as_string(), b.as_string(), op)};
    }

    return Value{apply_comparison(a.to_numeric(), b.to_numeric(), op)};
}

// ─────────── Division helpers ───────────
// handle_divide, handle_int_divide, and handle_modulo share a common
// structure: validate-nonzero → check-overflow → apply-op.  However,
// each function differs in its overflow handling (promote to double,
// emit an error, or return 0), type requirements (numeric vs
// integer-only), and error messages, so they are kept as separate
// functions rather than parameterised through a single template.

void VM::handle_divide() {
    auto [a_ref, b] = pop_binary_ref();

    // Report a type mismatch as a located Luma error up front, instead of
    // letting Value::to_numeric() throw its internal-sounding message.
    if (!both_numeric(a_ref, b)) [[unlikely]] {
        runtime_error(vm_errors::cannot_operate_on("divide", a_ref.display_type_name(),
                                                   b.display_type_name()));
    }

    validate_nonzero_divisor(b, "Division");

    // True division always produces a number (float), so integer / integer
    // promotes (7 / 2 == 3.5). Use // for integer (floor) division.
    const auto l = a_ref.to_numeric();
    const auto r = b.to_numeric();
    const auto result = l / r;

    // Finite operands producing ±Infinity (e.g. 1e308 / 1e-308) overflowed.
    if (std::isfinite(l) && std::isfinite(r) && !std::isfinite(result)) [[unlikely]] {
        runtime_error(vm_errors::number_overflow_in(op_noun(Op::Divide)));
    }

    a_ref = Value{result};
}

void VM::handle_int_divide() {
    auto [a_ref, b] = pop_binary_ref();

    if (a_ref.is_integer() && b.is_integer()) [[likely]] {
        validate_nonzero_divisor(b, "Integer division");

        if (would_overflow_div(a_ref.as_integer(), b.as_integer())) [[unlikely]] {
            runtime_error(vm_errors::integer_division_overflow,
                          vm_errors::hint_integer_division_overflow);
        }

        // Floor division rounds toward negative infinity, so -7 // 2 == -4.
        const std::int64_t x = a_ref.as_integer();
        const std::int64_t y = b.as_integer();
        std::int64_t quotient = x / y;
        const std::int64_t remainder = x % y;
        if (remainder != 0 && ((remainder < 0) != (y < 0))) {
            --quotient;
        }

        a_ref = Value{quotient};
    } else {
        runtime_error(vm_errors::integer_division_requires_integers,
                      vm_errors::hint_integer_division_only);
    }
}

void VM::handle_modulo() {
    auto [a_ref, b] = pop_binary_ref();

    if (!both_numeric(a_ref, b)) [[unlikely]] {
        runtime_error(vm_errors::cannot_operate_on(
            "take the remainder of", a_ref.display_type_name(), b.display_type_name()));
    }

    validate_nonzero_divisor(b, "Modulo");

    if (a_ref.is_integer() && b.is_integer()) [[likely]] {
        if (would_overflow_div(a_ref.as_integer(), b.as_integer())) {
            a_ref = Value{static_cast<std::int64_t>(0)};
        } else {
            a_ref = Value{a_ref.as_integer() % b.as_integer()};
        }
    } else {
        // `number` operands use floating-point remainder (std::fmod), matching
        // the type checker and language reference, which accept `number % number`.
        a_ref = Value{std::fmod(a_ref.to_numeric(), b.to_numeric())};
    }
}

void VM::validate_nonzero_divisor(const Value& divisor, std::string_view op_name) const {
    const bool is_zero = (divisor.is_integer() && divisor.as_integer() == 0) ||
                         (divisor.is_number() && divisor.as_number() == 0.0);
    if (is_zero) [[unlikely]] {
        runtime_error(vm_errors::division_by_zero_op(op_name), vm_errors::hint_divisor_not_zero);
    }
}

// ─────────── String concatenation ───────────

void VM::handle_concatenate() {
    auto [a_ref, b] = pop_binary_ref();

    // Shared validation for both paths.
    const auto validate_size = [this](std::size_t a_size, std::size_t b_size) {
        if (!check_string_concat_size(a_size, b_size)) [[unlikely]] {
            runtime_error(vm_errors::string_concat_exceeds_max, vm_errors::hint_string_too_large);
        }
    };

    // Fast path: both operands are already strings (avoids copies).  Strings
    // are stored directly in the Value variant (not behind a shared_ptr), so
    // a_ref always uniquely owns its buffer — appending in place via += is
    // safe and, unlike `Value{sa + sb}` (which always allocates an
    // exact-size buffer), can reuse existing capacity when the left operand's
    // buffer has room, avoiding a reallocation on repeated concatenation.
    if (a_ref.is_string() && b.is_string()) [[likely]] {
        const auto& sb = b.as_string();
        validate_size(a_ref.as_string().size(), sb.size());
        a_ref.as_string_mut() += sb;
    } else {
        auto sa = a_ref.to_string();
        auto sb = b.to_string();
        validate_size(sa.size(), sb.size());
        a_ref = Value{std::move(sa) + sb};
    }
}

} // namespace luma
