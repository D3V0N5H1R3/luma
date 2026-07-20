#include "runtime/compiler/const_folder.hpp"

#include <cmath>
#include <optional>

#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "common/comparison_ops.hpp"
#include "common/overflow.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/i_constant_emitter.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

namespace {

// ─── Per-operation integer fold helpers ───────────────────────────────────────
// Each returns the folded result on success, or nullopt if overflow/invalid.
// Overflow detection is delegated to the shared checked-arithmetic predicates in
// common/overflow.hpp so the compile-time folder and the bytecode optimizer share
// one implementation of this subtle, easy-to-get-wrong logic.

/// Attempts to fold an integer addition, returning nullopt on overflow.
[[nodiscard]] std::optional<std::int64_t> try_fold_add(std::int64_t l, std::int64_t r) noexcept {
    if (would_overflow_add(l, r)) {
        return std::nullopt;
    }
    return l + r;
}

/// Attempts to fold an integer subtraction, returning nullopt on overflow.
[[nodiscard]] std::optional<std::int64_t> try_fold_subtract(std::int64_t l,
                                                            std::int64_t r) noexcept {
    if (would_overflow_sub(l, r)) {
        return std::nullopt;
    }
    return l - r;
}

/// Attempts to fold an integer multiplication, returning nullopt on overflow.
[[nodiscard]] std::optional<std::int64_t> try_fold_multiply(std::int64_t l,
                                                            std::int64_t r) noexcept {
    if (would_overflow_mul(l, r)) {
        return std::nullopt;
    }
    return l * r;
}

/// Attempts to fold an integer modulo, returning nullopt on division by zero
/// or INT64_MIN % -1 (undefined behaviour in C++).
// Returns std::nullopt when folding is not possible (e.g. division by zero).
// Division-by-zero errors are intentionally deferred to runtime for proper
// error reporting with source locations.
[[nodiscard]] std::optional<std::int64_t> try_fold_modulo(std::int64_t l, std::int64_t r) noexcept {
    if (r == 0 || would_overflow_div(l, r)) {
        return std::nullopt;
    }
    return l % r;
}

/// Attempts to fold an integer division, returning nullopt on division by zero
/// or INT64_MIN / -1 (undefined behaviour in C++).
// Returns std::nullopt when folding is not possible (e.g. division by zero).
// Division-by-zero errors are intentionally deferred to runtime for proper
// error reporting with source locations.
[[nodiscard]] std::optional<std::int64_t> try_fold_int_divide(std::int64_t l,
                                                              std::int64_t r) noexcept {
    if (r == 0 || would_overflow_div(l, r)) {
        return std::nullopt;
    }
    return l / r;
}

/// Attempts to fold a left shift, returning nullopt on invalid shift amount.
[[nodiscard]] std::optional<std::int64_t> try_fold_shift_left(std::int64_t l,
                                                              std::int64_t r) noexcept {
    if (r < 0 || r >= CompilerLimits::k_int64_bits) {
        return std::nullopt;
    }
    return l << r;
}

/// Attempts to fold a right shift, returning nullopt on invalid shift amount.
[[nodiscard]] std::optional<std::int64_t> try_fold_shift_right(std::int64_t l,
                                                               std::int64_t r) noexcept {
    if (r < 0 || r >= CompilerLimits::k_int64_bits) {
        return std::nullopt;
    }
    return l >> r;
}

} // anonymous namespace

// ─── try_fold_with ────────────────────────────────────────────────────────────
// Defined here (not in the header) to avoid pulling the full ICompilationBackend
// definition into const_folder.hpp and creating a circular dependency.

template <typename ComputeFn>
bool ConstantFolder::try_fold_with(SourceLocation loc, ComputeFn compute) {
    auto result = compute();
    if (!result) {
        return false;
    }
    if (result->is_bool()) {
        api_.emit(result->as_bool() ? Op::True : Op::False, loc);
    } else {
        api_.emit_constant(std::move(*result), loc);
    }
    return true;
}

// ─── Integer arithmetic ───────────────────────────────────────────────────────

bool ConstantFolder::try_fold_integer_arithmetic(std::int64_t l, std::int64_t r, TokenType op,
                                                 SourceLocation loc) {
    // Attempt per-operation folding via named helpers.
    // Operations that can overflow return nullopt, and we fall back to double.
    switch (op) {
        case TokenType::Plus: {
            if (auto result = try_fold_add(l, r)) {
                api_.emit_constant(Value{*result}, loc);
                return true;
            }
            api_.emit_constant(Value{static_cast<double>(l) + static_cast<double>(r)}, loc);
            return true;
        }
        case TokenType::Minus: {
            if (auto result = try_fold_subtract(l, r)) {
                api_.emit_constant(Value{*result}, loc);
                return true;
            }
            api_.emit_constant(Value{static_cast<double>(l) - static_cast<double>(r)}, loc);
            return true;
        }
        case TokenType::Star: {
            if (auto result = try_fold_multiply(l, r)) {
                api_.emit_constant(Value{*result}, loc);
                return true;
            }
            api_.emit_constant(Value{static_cast<double>(l) * static_cast<double>(r)}, loc);
            return true;
        }
        case TokenType::Percent: {
            if (auto result = try_fold_modulo(l, r)) {
                api_.emit_constant(Value{*result}, loc);
                return true;
            }
            return false;
        }
        case TokenType::SlashSlash: {
            if (auto result = try_fold_int_divide(l, r)) {
                api_.emit_constant(Value{*result}, loc);
                return true;
            }
            return false;
        }
        case TokenType::Ampersand:
            api_.emit_constant(Value{l & r}, loc);
            return true;
        case TokenType::Pipe:
            api_.emit_constant(Value{l | r}, loc);
            return true;
        case TokenType::Caret:
            api_.emit_constant(Value{l ^ r}, loc);
            return true;
        case TokenType::LessLess: {
            if (auto result = try_fold_shift_left(l, r)) {
                api_.emit_constant(Value{*result}, loc);
                return true;
            }
            return false;
        }
        case TokenType::GreaterGreater: {
            if (auto result = try_fold_shift_right(l, r)) {
                api_.emit_constant(Value{*result}, loc);
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

// ─── Number arithmetic ────────────────────────────────────────────────────────

bool ConstantFolder::try_fold_number_arithmetic(double l, double r, TokenType op,
                                                SourceLocation loc) {
    return try_fold_with(loc, [&]() -> std::optional<Value> {
        double result{0.0};

        switch (op) {
            case TokenType::Plus:
                result = l + r;
                break;
            case TokenType::Minus:
                result = l - r;
                break;
            case TokenType::Star:
                result = l * r;
                break;
            case TokenType::Slash:
                if (r != 0.0) {
                    result = l / r;
                } else {
                    return std::nullopt;
                }
                break;
            default:
                return std::nullopt;
        }

        if (std::isfinite(l) && std::isfinite(r) && !std::isfinite(result)) {
            return std::nullopt;
        }

        return Value{result};
    });
}

// ─── Comparison ───────────────────────────────────────────────────────────────

bool ConstantFolder::try_fold_comparison(double l, double r, TokenType op, SourceLocation loc) {
    return try_fold_with(loc, [&]() -> std::optional<Value> {
        if (const auto cmp_op = token_to_comparison_op(op)) {
            return Value{apply_comparison(l, r, *cmp_op)};
        }

        return std::nullopt;
    });
}

// ─── String concatenation ─────────────────────────────────────────────────────

bool ConstantFolder::try_fold_string_concatenation(const std::string& l, const std::string& r,
                                                   TokenType op, SourceLocation loc) {
    return try_fold_with(loc, [&]() -> std::optional<Value> {
        if (op != TokenType::Plus) {
            return std::nullopt;
        }
        return Value{l + r};
    });
}

// ─── Primary entry point ──────────────────────────────────────────────────────

bool ConstantFolder::try_fold_binary_at_compile_time(const LiteralExpression& lhs,
                                                     const LiteralExpression& rhs, TokenType op,
                                                     SourceLocation loc) {
    const bool both_int = lhs.literal_type() == LiteralExpression::LiteralType::Integer &&
                          rhs.literal_type() == LiteralExpression::LiteralType::Integer;
    const bool both_num = (lhs.literal_type() == LiteralExpression::LiteralType::Integer ||
                           lhs.literal_type() == LiteralExpression::LiteralType::Number) &&
                          (rhs.literal_type() == LiteralExpression::LiteralType::Integer ||
                           rhs.literal_type() == LiteralExpression::LiteralType::Number);

    // Convert a literal to its double representation.
    auto as_double = [](const LiteralExpression& lit) -> double {
        return lit.literal_type() == LiteralExpression::LiteralType::Integer
                   ? static_cast<double>(lit.integer_value())
                   : lit.number_value();
    };

    // Integer arithmetic folding.
    if (both_int) {
        if (try_fold_integer_arithmetic(lhs.integer_value(), rhs.integer_value(), op, loc)) {
            return true;
        }

        // Integer comparison folding — compare the int64 values exactly.
        // Routing integer comparisons through double (the numeric path below)
        // would lose precision for magnitudes beyond 2^53 and fold to a result
        // that disagrees with the VM's exact int64 comparison.
        const auto li = lhs.integer_value();
        const auto ri = rhs.integer_value();

        switch (op) {
            case TokenType::EqualsEquals:
                api_.emit(li == ri ? Op::True : Op::False, loc);
                return true;
            case TokenType::BangEquals:
                api_.emit(li != ri ? Op::True : Op::False, loc);
                return true;
            case TokenType::Less:
                api_.emit(li < ri ? Op::True : Op::False, loc);
                return true;
            case TokenType::LessEquals:
                api_.emit(li <= ri ? Op::True : Op::False, loc);
                return true;
            case TokenType::Greater:
                api_.emit(li > ri ? Op::True : Op::False, loc);
                return true;
            case TokenType::GreaterEquals:
                api_.emit(li >= ri ? Op::True : Op::False, loc);
                return true;
            default:
                break;
        }
    } else if (both_num) {
        // Floating-point arithmetic folding.
        if (try_fold_number_arithmetic(as_double(lhs), as_double(rhs), op, loc)) {
            return true;
        }
    }

    // String constant folding.
    if (lhs.literal_type() == LiteralExpression::LiteralType::String &&
        rhs.literal_type() == LiteralExpression::LiteralType::String) {
        if (try_fold_string_concatenation(lhs.string_value(), rhs.string_value(), op, loc)) {
            return true;
        }
    }

    // Boolean equality folding.
    if (lhs.literal_type() == LiteralExpression::LiteralType::Boolean &&
        rhs.literal_type() == LiteralExpression::LiteralType::Boolean) {
        if (op == TokenType::EqualsEquals) {
            api_.emit(lhs.boolean_value() == rhs.boolean_value() ? Op::True : Op::False, loc);
            return true;
        }

        if (op == TokenType::BangEquals) {
            api_.emit(lhs.boolean_value() != rhs.boolean_value() ? Op::True : Op::False, loc);
            return true;
        }
    }

    // Numeric comparison folding.
    if (both_num) {
        return try_fold_comparison(as_double(lhs), as_double(rhs), op, loc);
    }

    return false;
}

} // namespace luma
