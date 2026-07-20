#include <cstdint>
#include <format>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/types/compile_time_arithmetic.hpp"
#include "analysis/types/expression_type_checker.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "common/overflow.hpp"
#include "common/resource_limits.hpp"

namespace luma {

namespace {

// Promote two numeric types: if either is Number, result is Number; otherwise Integer.
[[nodiscard]] TypeInfo promote_numeric(const TypeInfo& left, const TypeInfo& right) {
    if (left.kind == TypeInfo::Kind::Number || right.kind == TypeInfo::Kind::Number) {
        return TypeInfo::make(TypeInfo::Kind::Number);
    }
    return TypeInfo::make(TypeInfo::Kind::Integer);
}

// Check if either type is Unknown or StdlibAny (error propagation).
[[nodiscard]] bool is_error_or_unknown(const TypeInfo& left, const TypeInfo& right) {
    return left.kind == TypeInfo::Kind::StdlibAny || right.kind == TypeInfo::Kind::StdlibAny ||
           left.kind == TypeInfo::Kind::Unknown || right.kind == TypeInfo::Kind::Unknown;
}

} // namespace

// ── Constant-folding diagnostics ────────────────────────────────────────────
// Detects errors when both operands are literals (division by zero, overflow,
// shift out of range, string repeat limits).  Called once at the top of
// visit_binary() before dispatching to per-family helpers.
void ExpressionTypeChecker::check_binary_constant_folding(const BinaryExpression& expr,
                                                          const TypeInfo& left_type) {
    // Division by zero: detect when the divisor is literal 0.
    if ((expr.op == TokenType::Slash || expr.op == TokenType::Percent ||
         expr.op == TokenType::SlashSlash) &&
        is_integer_literal(*expr.right)) {
        const auto msg = compile_time_arithmetic::check_division(*get_integer_value(*expr.right),
                                                                 expr.op == TokenType::SlashSlash ||
                                                                     expr.op == TokenType::Percent);
        if (msg) {
            emit_err(*msg, expr.location, "the divisor is always zero — this will crash at runtime",
                     DiagnosticCode::DivisionByZero);
        }
    } else if ((expr.op == TokenType::Slash || expr.op == TokenType::Percent) &&
               is_number_literal(*expr.right)) {
        const auto msg = compile_time_arithmetic::check_float_division(
            static_cast<const LiteralExpression&>(*expr.right).number_value());
        if (msg) {
            emit_err(*msg, expr.location, "the divisor is always zero — this will crash at runtime",
                     DiagnosticCode::DivisionByZero);
        }
    }

    // Integer overflow: detect when both operands are integer literals.
    if (is_integer_literal(*expr.left) && is_integer_literal(*expr.right)) {
        const auto left_val = *get_integer_value(*expr.left);
        const auto right_val = *get_integer_value(*expr.right);

        switch (expr.op) {
            case TokenType::Plus:
                if (would_overflow_add(left_val, right_val)) {
                    emit_err("integer overflow in addition", expr.location,
                             "use 'number' type for larger values",
                             DiagnosticCode::IntegerOverflow);
                }
                break;
            case TokenType::Minus:
                if (would_overflow_sub(left_val, right_val)) {
                    emit_err("integer overflow in subtraction", expr.location,
                             "use 'number' type for larger values",
                             DiagnosticCode::IntegerOverflow);
                }
                break;
            case TokenType::Star:
                if (would_overflow_mul(left_val, right_val)) {
                    emit_err("integer overflow in multiplication", expr.location,
                             "use 'number' type for larger values",
                             DiagnosticCode::IntegerOverflow);
                }
                break;
            case TokenType::Slash:
            case TokenType::SlashSlash:
                if (right_val != 0 && would_overflow_div(left_val, right_val)) {
                    emit_err("integer overflow in division", expr.location,
                             "use 'number' type for larger values",
                             DiagnosticCode::IntegerOverflow);
                }
                break;
            case TokenType::Percent:
                if (right_val != 0 && would_overflow_div(left_val, right_val)) {
                    emit_err("integer overflow in modulo", expr.location,
                             "use 'number' type for larger values",
                             DiagnosticCode::IntegerOverflow);
                }
                break;
            default:
                break;
        }
    }

    // Shift amount out of range: detect when shift amount is a literal
    // outside [0, 63].
    if ((expr.op == TokenType::LessLess || expr.op == TokenType::GreaterGreater) &&
        is_integer_literal(*expr.right)) {
        const auto msg =
            compile_time_arithmetic::check_shift_amount(*get_integer_value(*expr.right));
        if (msg) {
            emit_err(*msg, expr.location, "shift amount must be between 0 and 63",
                     DiagnosticCode::ShiftOutOfRange);
        }
    }

    // String repeat: detect negative or excessive literal repeat count.
    if (expr.op == TokenType::Star && left_type.kind == TypeInfo::Kind::String &&
        is_integer_literal(*expr.right)) {
        const auto msg = compile_time_arithmetic::check_string_repeat(
            *get_integer_value(*expr.right),
            static_cast<std::size_t>(ResourceLimits::max_string_repeat));
        if (msg) {
            emit_err(*msg, expr.location, "reduce the repeat count to avoid excessive memory usage",
                     DiagnosticCode::InvalidRepeatCount);
        }
    }
}

// ── Arithmetic: +, -, *, /, %, //, ** ────────────────────────────────────────
TypeInfo ExpressionTypeChecker::check_arithmetic_binary(const BinaryExpression& expr,
                                                        const TypeInfo& left,
                                                        const TypeInfo& right) {
    // Plus: also supports string concatenation.
    if (expr.op == TokenType::Plus) {
        if (left.kind == TypeInfo::Kind::String && right.kind == TypeInfo::Kind::String) {
            return TypeInfo::make(TypeInfo::Kind::String);
        }
        if (left.is_numeric() && right.is_numeric()) {
            return promote_numeric(left, right);
        }
        if (is_error_or_unknown(left, right)) {
            return TypeInfo::make(TypeInfo::Kind::StdlibAny);
        }

        tc_.error(std::format("operator '+' requires numeric or string operands, "
                              "got '{}' and '{}'",
                              left.to_string(), right.to_string()),
                  expr.location,
                  "convert the operand to a compatible type using Converter.to_string() or "
                  "Converter.to_number()",
                  DiagnosticCode::InvalidOperand);

        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    // Star: also supports string repeat (string * integer).
    if (expr.op == TokenType::Star) {
        if (left.kind == TypeInfo::Kind::String && right.kind == TypeInfo::Kind::Integer) {
            return TypeInfo::make(TypeInfo::Kind::String);
        }

        if (left.is_numeric() && right.is_numeric()) {
            return promote_numeric(left, right);
        }
        if (is_error_or_unknown(left, right)) {
            return TypeInfo::make(TypeInfo::Kind::StdlibAny);
        }

        tc_.error(std::format("operator '*' requires numeric operands or "
                              "string * integer, got '{}' and '{}'",
                              left.to_string(), right.to_string()),
                  expr.location,
                  "ensure both operands are numbers, or use 'string * integer' to repeat a string",
                  DiagnosticCode::InvalidOperand);

        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    // Integer division: both operands must be integer; result is integer.
    if (expr.op == TokenType::SlashSlash) {
        if (left.kind == TypeInfo::Kind::Integer && right.kind == TypeInfo::Kind::Integer) {
            return TypeInfo::make(TypeInfo::Kind::Integer);
        }

        if (is_error_or_unknown(left, right)) {
            return TypeInfo::make(TypeInfo::Kind::StdlibAny);
        }

        tc_.error(std::format("operator '//' requires integer operands, "
                              "got '{}' and '{}'",
                              left.to_string(), right.to_string()),
                  expr.location, "convert the operands to integers using Converter.to_integer()",
                  DiagnosticCode::InvalidOperand);

        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    // Minus, Slash, Percent: standard numeric arithmetic.
    if (left.is_numeric() && right.is_numeric()) {
        return promote_numeric(left, right);
    }
    if (is_error_or_unknown(left, right)) {
        return TypeInfo::make(TypeInfo::Kind::StdlibAny);
    }

    tc_.error(std::format("arithmetic operator requires numeric operands, "
                          "got '{}' and '{}'",
                          left.to_string(), right.to_string()),
              expr.location,
              "convert the operand to a number using Converter.to_integer() or "
              "Converter.to_number()",
              DiagnosticCode::InvalidOperand);

    return TypeInfo::make(TypeInfo::Kind::Unknown);
}

// ── Bitwise: &, |, ^, <<, >> ────────────────────────────────────────────────
TypeInfo ExpressionTypeChecker::check_bitwise_binary(const BinaryExpression& expr,
                                                     const TypeInfo& left, const TypeInfo& right) {
    const auto op_str = [&]() -> std::string_view {
        switch (expr.op) {
            case TokenType::Ampersand:
                return "&";
            case TokenType::Pipe:
                return "|";
            case TokenType::Caret:
                return "^";
            case TokenType::LessLess:
                return "<<";
            case TokenType::GreaterGreater:
                return ">>";
            default:
                return "?";
        }
    }();

    if (left.kind == TypeInfo::Kind::Integer && right.kind == TypeInfo::Kind::Integer) {
        return TypeInfo::make(TypeInfo::Kind::Integer);
    }

    if (is_error_or_unknown(left, right)) {
        return TypeInfo::make(TypeInfo::Kind::StdlibAny);
    }

    tc_.error(std::format("operator '{}' requires integer operands, "
                          "got '{}' and '{}'",
                          op_str, left.to_string(), right.to_string()),
              expr.location, "bitwise operators only work with integer values",
              DiagnosticCode::InvalidOperand);

    return TypeInfo::make(TypeInfo::Kind::Unknown);
}

// ── Equality: ==, != ────────────────────────────────────────────────────────
TypeInfo ExpressionTypeChecker::check_equality_binary(const BinaryExpression& expr,
                                                      const TypeInfo& left, const TypeInfo& right) {
    // Linter: redundant boolean comparison (x == true, x != false).
    if ((left.kind == TypeInfo::Kind::Boolean && is_boolean_literal(*expr.right)) ||
        (right.kind == TypeInfo::Kind::Boolean && is_boolean_literal(*expr.left))) {
        tc_.warn("redundant comparison of boolean with 'true' or 'false'", expr.location,
                 "remove the comparison and use the boolean directly");
    }

    // Linter: floating-point equality comparison.
    if (expr.op == TokenType::EqualsEquals) {
        if (left.kind == TypeInfo::Kind::Number && right.kind == TypeInfo::Kind::Number) {
            tc_.warn("comparing floating-point numbers with '==' can give "
                     "unexpected results due to rounding",
                     expr.location,
                     "consider comparing with a tolerance: "
                     "Math.abs(a - b) < epsilon",
                     DiagnosticCode::FloatingPointEquality);
        }
    }

    // Linter: comparison of incompatible types (always false / true).
    if (left.kind != TypeInfo::Kind::StdlibAny && left.kind != TypeInfo::Kind::Unknown &&
        right.kind != TypeInfo::Kind::StdlibAny && right.kind != TypeInfo::Kind::Unknown &&
        left.kind != TypeInfo::Kind::None && right.kind != TypeInfo::Kind::None) {
        // Allow integer ↔ number comparisons (numeric promotion).
        const bool both_numeric = left.is_numeric() && right.is_numeric();

        if (!both_numeric && left.kind != right.kind) {
            const auto* const op_str = expr.op == TokenType::EqualsEquals ? "==" : "!=";

            tc_.warn(std::format("comparison '{}' between incompatible types '{}' and "
                                 "'{}' — result is always {}",
                                 op_str, left.to_string(), right.to_string(),
                                 expr.op == TokenType::EqualsEquals ? "false" : "true"),
                     expr.location,
                     "ensure both sides have the same type, or convert "
                     "one side explicitly");
        }
    }

    return TypeInfo::make(TypeInfo::Kind::Boolean);
}

// ── Comparison: <, >, <=, >= ────────────────────────────────────────────────
TypeInfo ExpressionTypeChecker::check_comparison_binary(const BinaryExpression& expr,
                                                        const TypeInfo& left,
                                                        const TypeInfo& right) {
    const bool left_ok = left.is_numeric() || left.kind == TypeInfo::Kind::String ||
                         left.kind == TypeInfo::Kind::StdlibAny ||
                         left.kind == TypeInfo::Kind::Unknown;
    const bool right_ok = right.is_numeric() || right.kind == TypeInfo::Kind::String ||
                          right.kind == TypeInfo::Kind::StdlibAny ||
                          right.kind == TypeInfo::Kind::Unknown;

    if (!left_ok || !right_ok) {
        tc_.error(std::format("comparison operator requires numeric or string "
                              "operands, got '{}' and '{}'",
                              left.to_string(), right.to_string()),
                  expr.location,
                  "ensure both operands are the same comparable type (number, integer, or "
                  "string)",
                  DiagnosticCode::InvalidOperand);
    }

    // Reject mixed string/numeric comparisons (both must be in the
    // same category unless one side is StdlibAny/Unknown).
    const bool left_numeric = left.is_numeric();
    const bool left_string = left.kind == TypeInfo::Kind::String;
    const bool right_numeric = right.is_numeric();
    const bool right_string = right.kind == TypeInfo::Kind::String;

    if ((left_numeric && right_string) || (left_string && right_numeric)) {
        tc_.error(std::format("cannot compare string with numeric type: "
                              "'{}' and '{}'",
                              left.to_string(), right.to_string()),
                  expr.location, "convert one side so both operands are the same type",
                  DiagnosticCode::IncompatibleTypes);
    }

    return TypeInfo::make(TypeInfo::Kind::Boolean);
}

// ── Logical: &&, || ─────────────────────────────────────────────────────────
TypeInfo ExpressionTypeChecker::check_logical_binary(const BinaryExpression& expr,
                                                     const TypeInfo& left, const TypeInfo& right) {
    (void)type_check_helpers::require_boolean_operand(tc_, left, "logical operator",
                                                      expr.left->location);
    (void)type_check_helpers::require_boolean_operand(tc_, right, "logical operator",
                                                      expr.right->location);

    return TypeInfo::make(TypeInfo::Kind::Boolean);
}

// ── Null coalescing: ?? ─────────────────────────────────────────────────────
TypeInfo ExpressionTypeChecker::check_null_coalescing_binary(const BinaryExpression& expr,
                                                             const TypeInfo& left,
                                                             const TypeInfo& right) {
    if (left.kind == TypeInfo::Kind::Optional) {
        if (const auto inner =
                type_check_helpers::unwrap_optional_or_error(tc_, left, expr.location)) {
            // Chaining: optional<T> ?? optional<T>  →  optional<T>
            if (right.kind == TypeInfo::Kind::Optional && !right.inner_types.empty() &&
                tc_.is_assignable(*inner, right.element_type())) {
                return left;
            }

            // Unwrapping: optional<T> ?? T  →  T
            if (!tc_.is_assignable(*inner, right)) {
                tc_.error(std::format("'?\?' default type '{}' is not assignable to "
                                      "unwrapped optional type '{}'",
                                      right.to_string(), inner->to_string()),
                          expr.location,
                          "ensure the default value matches the type inside the optional",
                          DiagnosticCode::TypeMismatch);
            }

            return *inner;
        }

        return left; // degenerate — empty optional, should not occur
    }

    if (left.kind == TypeInfo::Kind::Result && !left.inner_types.empty()) {
        const auto& inner = left.result_value_type();

        // Chaining: result<T> ?? result<T>  →  result<T>
        if (right.kind == TypeInfo::Kind::Result && !right.inner_types.empty() &&
            tc_.is_assignable(inner, right.result_value_type())) {
            return left;
        }

        // Unwrapping: result<T> ?? T  →  T
        if (!tc_.is_assignable(inner, right)) {
            tc_.error(std::format("'?\?' default type '{}' is not assignable to "
                                  "unwrapped result type '{}'",
                                  right.to_string(), inner.to_string()),
                      expr.location,
                      "ensure the default value matches the success type of the result",
                      DiagnosticCode::TypeMismatch);
        }

        return inner;
    }

    if (left.kind == TypeInfo::Kind::None) {
        return right;
    }

    if (left.kind != TypeInfo::Kind::StdlibAny && left.kind != TypeInfo::Kind::Unknown) {
        return left;
    }

    return right;
}

// ── Containment: in ─────────────────────────────────────────────────────────
TypeInfo ExpressionTypeChecker::check_containment_binary(const BinaryExpression& expr,
                                                         const TypeInfo& left,
                                                         const TypeInfo& right) {
    if (right.kind == TypeInfo::Kind::Dictionary && left.kind != TypeInfo::Kind::String &&
        left.kind != TypeInfo::Kind::StdlibAny && left.kind != TypeInfo::Kind::Unknown) {
        tc_.error(std::format("'in' on dictionary requires a string key, "
                              "got '{}'",
                              left.to_string()),
                  expr.left->location,
                  "dictionary keys are always strings — convert the value using "
                  "Converter.to_string()");
    }

    if (right.kind == TypeInfo::Kind::String && left.kind != TypeInfo::Kind::String &&
        left.kind != TypeInfo::Kind::StdlibAny && left.kind != TypeInfo::Kind::Unknown) {
        tc_.error(std::format("'in' on string requires a string operand, "
                              "got '{}'",
                              left.to_string()),
                  expr.left->location, "use a string value on the left side of 'in'");
    }

    if (right.kind == TypeInfo::Kind::Array || right.kind == TypeInfo::Kind::Dictionary ||
        right.kind == TypeInfo::Kind::String || right.kind == TypeInfo::Kind::StdlibAny ||
        right.kind == TypeInfo::Kind::Unknown) {
        return TypeInfo::make(TypeInfo::Kind::Boolean);
    }

    tc_.error(std::format("'in' operator requires array, dictionary, or "
                          "string on the right, got '{}'",
                          right.to_string()),
              expr.right->location, "the right side of 'in' must be a collection or string");

    return TypeInfo::make(TypeInfo::Kind::Boolean);
}

// ── visit_binary: dispatch to per-family helpers ────────────────────────────
TypeInfo ExpressionTypeChecker::visit_binary(const BinaryExpression& expr) {
    const auto left_type = infer_expression_type(*expr.left);
    const auto right_type = infer_expression_type(*expr.right);

    // Constant-folding diagnostics (division by zero, overflow, etc.).
    check_binary_constant_folding(expr, left_type);

    switch (expr.op) {
        // Arithmetic operators.
        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Star:
        case TokenType::Slash:
        case TokenType::Percent:
        case TokenType::SlashSlash:
            return check_arithmetic_binary(expr, left_type, right_type);

        // Bitwise operators.
        case TokenType::Ampersand:
        case TokenType::Pipe:
        case TokenType::Caret:
        case TokenType::LessLess:
        case TokenType::GreaterGreater:
            return check_bitwise_binary(expr, left_type, right_type);

        // Equality operators.
        case TokenType::EqualsEquals:
        case TokenType::BangEquals:
            return check_equality_binary(expr, left_type, right_type);

        // Comparison operators.
        case TokenType::Less:
        case TokenType::Greater:
        case TokenType::LessEquals:
        case TokenType::GreaterEquals:
            return check_comparison_binary(expr, left_type, right_type);

        // Logical operators.
        case TokenType::AmpersandAmpersand:
        case TokenType::PipePipe:
            return check_logical_binary(expr, left_type, right_type);

        // Null-coalescing operator.
        case TokenType::QuestionQuestion:
            return check_null_coalescing_binary(expr, left_type, right_type);

        // Containment operator.
        case TokenType::In:
            return check_containment_binary(expr, left_type, right_type);

        default:
            return TypeInfo::make(TypeInfo::Kind::StdlibAny);
    }
}

} // namespace luma
