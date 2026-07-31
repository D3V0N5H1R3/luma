#include <cstdint>
#include <format>
#include <limits>

#include "analysis/ast/expression.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/types/expression_type_checker.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checking_context.hpp"

namespace luma {

TypeInfo ExpressionTypeChecker::visit_unary(const UnaryExpression& expr) {
    const auto operand_type = infer_expression_type(*expr.operand);

    if (expr.op == TokenType::Bang) {
        if (operand_type.kind != TypeInfo::Kind::Boolean &&
            operand_type.kind != TypeInfo::Kind::StdlibAny &&
            operand_type.kind != TypeInfo::Kind::Unknown) {
            tc_.error(std::format("logical NOT requires boolean operand, got '{}'",
                                  operand_type.to_string()),
                      expr.operand->location,
                      "use a comparison (==, !=) to get a boolean, or convert with "
                      "Converter.to_boolean()",
                      DiagnosticCode::InvalidOperand);
        }

        return TypeInfo::make(TypeInfo::Kind::Boolean);
    }

    if (expr.op == TokenType::Minus) {
        (void)type_check_helpers::require_numeric_operand(tc_, operand_type, "unary '-'",
                                                          expr.operand->location);

        // Negating a literal INT64_MIN overflows: its magnitude exceeds
        // INT64_MAX. get_integer_value yields nullopt for non-literal operands.
        if (const auto val = get_integer_value(*expr.operand);
            val && *val == std::numeric_limits<std::int64_t>::min()) {
            tc_.error("integer overflow in negation", expr.location,
                      "use 'number' type for larger values", DiagnosticCode::IntegerOverflow);
        }

        return operand_type;
    }

    // Postfix ? — error/none propagation on result and optional types.
    if (expr.op == TokenType::QuestionMark) {
        // '?' requires the enclosing function to return result<T> or optional<T>,
        // or to be inside @main (where a propagated failure prints the error and exits).
        const auto& ctx = tc_.context();
        if (!ctx.is_in_main && (!ctx.current_return_type ||
                                (ctx.current_return_type->kind != TypeInfo::Kind::Result &&
                                 ctx.current_return_type->kind != TypeInfo::Kind::Optional))) {
            tc_.error("error propagation '?' can only be used inside a function "
                      "that returns 'result<T>' or 'optional<T>', or inside '@main'",
                      expr.location,
                      "change the function return type to 'result<T>' or 'optional<T>' to use '?'");
            return TypeInfo::make(TypeInfo::Kind::StdlibAny);
        }

        // The operand's wrapper kind must also match the enclosing function's
        // return wrapper: propagating a 'none' out of a result-returning
        // function (or a failure out of an optional-returning function) would
        // let a value of the wrong wrapper kind escape the declared return type.
        // '@main' is exempt because it consumes the propagated failure locally
        // (printing it and exiting) instead of returning it to a caller.
        if (!ctx.is_in_main && ctx.current_return_type &&
            (operand_type.kind == TypeInfo::Kind::Result ||
             operand_type.kind == TypeInfo::Kind::Optional) &&
            operand_type.kind != ctx.current_return_type->kind) {
            const char* const required =
                operand_type.kind == TypeInfo::Kind::Result ? "result<T>" : "optional<T>";
            tc_.error(std::format("error propagation '?' on a '{}' value requires the enclosing "
                                  "function to return '{}', or to be '@main'",
                                  operand_type.to_string(), required),
                      expr.location,
                      std::format("change the function return type to '{}' to propagate this value",
                                  required));
            return TypeInfo::make(TypeInfo::Kind::StdlibAny);
        }

        if (operand_type.kind == TypeInfo::Kind::Result) {
            if (!operand_type.inner_types.empty()) {
                return operand_type.result_value_type();
            }

            return TypeInfo::make(TypeInfo::Kind::StdlibAny);
        }

        if (operand_type.kind == TypeInfo::Kind::Optional) {
            if (const auto inner = type_check_helpers::unwrap_optional_or_error(
                    tc_, operand_type, expr.operand->location)) {
                return *inner;
            }

            return TypeInfo::make(TypeInfo::Kind::StdlibAny);
        }

        if (operand_type.kind != TypeInfo::Kind::StdlibAny &&
            operand_type.kind != TypeInfo::Kind::Unknown) {
            tc_.error(std::format("propagation '?' requires result or optional type, got '{}'",
                                  operand_type.to_string()),
                      expr.operand->location,
                      "the '?' operator can only be used on result<T> or optional<T> values");
        }

        return TypeInfo::make(TypeInfo::Kind::StdlibAny);
    }

    return operand_type;
}

} // namespace luma
