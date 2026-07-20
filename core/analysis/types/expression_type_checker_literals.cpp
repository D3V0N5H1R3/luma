#include <cstddef>
#include <format>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/diagnostics/diagnostic_builders.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/types/expression_type_checker.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "common/resource_limits.hpp"

namespace luma {

// Luma tuples hold between two and four elements; larger fixed groupings should
// use an array.  Both bounds appear in the arity check and its diagnostic.
namespace {
constexpr std::size_t k_min_tuple_arity = 2;
constexpr std::size_t k_max_tuple_arity = 4;
} // namespace

TypeInfo ExpressionTypeChecker::visit_literal(const LiteralExpression& expr) {
    switch (expr.literal_type()) {
        case LiteralExpression::LiteralType::Integer:
            return TypeInfo::make(TypeInfo::Kind::Integer);
        case LiteralExpression::LiteralType::Number:
            return TypeInfo::make(TypeInfo::Kind::Number);
        case LiteralExpression::LiteralType::Boolean:
            return TypeInfo::make(TypeInfo::Kind::Boolean);
        case LiteralExpression::LiteralType::String:
            return TypeInfo::make(TypeInfo::Kind::String);
        case LiteralExpression::LiteralType::None:
            return TypeInfo::make(TypeInfo::Kind::None);
    }

    return TypeInfo::make(TypeInfo::Kind::Unknown);
}

TypeInfo ExpressionTypeChecker::visit_variable(const VariableExpression& expr) {
    const auto* sym = tc_.lookup_variable(expr.name);

    if (sym != nullptr) {
        // Mark variable as read.
        tc_.context().current_scope->mark_read(expr.name);

        // If the variable references a user-defined function (e.g. passed
        // as a higher-order argument), mark it as "called" so the unused
        // function warning does not fire.
        if (sym->type.kind == TypeInfo::Kind::Func && tc_.functions().contains(expr.name)) {
            tc_.mark_function_called(expr.name);
        }

        // Unique variable: check if already consumed.
        if (sym->is_unique && sym->is_consumed) {
            tc_.error(
                std::format("use of consumed unique variable '{}' — "
                            "unique values can only be used once",
                            expr.name),
                expr.location,
                "unique values can only be used once — consider cloning or restructuring your "
                "code");
        }

        // Mark unique variables as consumed when accessed (moved).
        if (sym->is_unique && !sym->is_borrow) {
            tc_.context().current_scope->mark_consumed(expr.name, true);
        }

        // Flow-sensitive type narrowing: if there is an active refinement
        // for this variable, return the narrowed type instead.
        const auto* refined = find_refinement(expr.name);

        if (refined != nullptr) {
            return *refined;
        }

        return sym->type;
    }

    if (tc_.records().contains(expr.name)) {
        return TypeInfo::make_named(TypeInfo::Kind::Record, expr.name);
    }
    if (tc_.choices().contains(expr.name)) {
        return TypeInfo::make_named(TypeInfo::Kind::Choice, expr.name);
    }

    const auto hint = tc_.suggest_variable_name(expr.name);
    const auto diag = diag_builders::undefined_symbol("variable", expr.name, hint);
    tc_.error(diag.message, expr.location, diag.hint, DiagnosticCode::UndefinedVariable);

    return TypeInfo::make(TypeInfo::Kind::Unknown);
}

TypeInfo ExpressionTypeChecker::visit_array_literal(const ArrayLiteralExpression& expr) {
    if (expr.elements.empty()) {
        return TypeInfo::make_array(TypeInfo::make(TypeInfo::Kind::StdlibAny));
    }

    const auto first_type = infer_expression_type(*expr.elements[0]);

    for (std::size_t i{1}; i < expr.elements.size(); ++i) {
        const auto elem_type = infer_expression_type(*expr.elements[i]);

        if (!tc_.is_assignable(first_type, elem_type)) {
            tc_.error(std::format("array elements must have the same type: first is "
                                  "'{}', element {} is '{}'",
                                  first_type.to_string(), i + 1, elem_type.to_string()),
                      expr.elements[i]->location,
                      "all array elements must be the same type — convert elements or use separate "
                      "arrays",
                      DiagnosticCode::IncompatibleTypes);
        }
    }

    return TypeInfo::make_array(first_type);
}

TypeInfo ExpressionTypeChecker::visit_dictionary_literal(const DictionaryLiteralExpression& expr) {
    if (expr.entries.empty()) {
        return TypeInfo::make_dict(TypeInfo::make(TypeInfo::Kind::StdlibAny));
    }

    // Keys must be strings.
    for (const auto& entry : expr.entries) {
        const auto key_type = infer_expression_type(*entry.key);

        if (key_type.kind != TypeInfo::Kind::String && key_type.kind != TypeInfo::Kind::StdlibAny &&
            key_type.kind != TypeInfo::Kind::Unknown) {
            tc_.error(
                std::format("dictionary keys must be strings, got '{}'", key_type.to_string()),
                entry.key->location,
                "dictionary keys are always strings — use a string literal or convert with "
                "Converter.to_string()",
                DiagnosticCode::TypeMismatch);
        }
    }

    const auto first_val_type = infer_expression_type(*expr.entries[0].value);

    for (std::size_t i{1}; i < expr.entries.size(); ++i) {
        const auto val_type = infer_expression_type(*expr.entries[i].value);

        if (!tc_.is_assignable(first_val_type, val_type)) {
            tc_.error(std::format("dictionary values must have the same type: "
                                  "first is '{}', entry {} is '{}'",
                                  first_val_type.to_string(), i + 1, val_type.to_string()),
                      expr.entries[i].value->location,
                      "all dictionary values must be the same type — convert values or split into "
                      "separate dictionaries",
                      DiagnosticCode::IncompatibleTypes);
        }
    }

    return TypeInfo::make_dict(first_val_type);
}

TypeInfo ExpressionTypeChecker::visit_tuple_literal(const TupleLiteralExpression& expr) {
    if (expr.elements.size() < k_min_tuple_arity || expr.elements.size() > k_max_tuple_arity) {
        tc_.error(std::format("tuples must have {} to {} elements, got {}", k_min_tuple_arity,
                              k_max_tuple_arity, expr.elements.size()),
                  expr.location,
                  std::format("tuples support {} to {} elements — use an array "
                              "for more",
                              k_min_tuple_arity, k_max_tuple_arity));
    }

    TypeInfo info{.kind = TypeInfo::Kind::Tuple, .name = {}, .inner_types = {}, .return_type = {}};

    for (const auto& elem : expr.elements) {
        info.inner_types.push_back(infer_expression_type(*elem));
    }

    return info;
}

TypeInfo ExpressionTypeChecker::visit_downcast(const DowncastExpression& expr) {
    const auto operand_type = infer_expression_type(*expr.operand);
    const auto target = tc_.resolve_type(expr.target_type);

    // Error on unknown type name in downcast<T>.
    if (target.kind == TypeInfo::Kind::Unknown && !expr.target_type.name().empty() &&
        expr.target_type.is_plain()) {
        const auto hint = tc_.suggest_type_name(expr.target_type.name());
        tc_.error(std::format("unknown type '{}' in downcast", expr.target_type.name()),
                  expr.location,
                  hint.empty() ? "check the spelling or make sure the type is declared" : hint);
    }

    // Warn when trusted_downcast is used on a runtime-permissive value.
    if (expr.is_trusted && operand_type.kind == TypeInfo::Kind::StdlibAny) {
        tc_.warn("'trusted_downcast' on unrefined stdlib value has no compile-time "
                 "safety guarantee — use 'downcast<T>' with a match or "
                 "'is<T>' guard to handle the failure case safely",
                 expr.location, "replace 'trusted_downcast' with 'downcast' and handle the result");
    }

    // Warn when the operand's static type is provably incompatible with target.
    if (operand_type.kind != TypeInfo::Kind::StdlibAny &&
        operand_type.kind != TypeInfo::Kind::Unknown && target.kind != TypeInfo::Kind::Unknown) {
        // integer -> number widening is valid; same kind is valid.
        const bool widening =
            (target.kind == TypeInfo::Kind::Number && operand_type.kind == TypeInfo::Kind::Integer);

        const bool different_primitives =
            (operand_type.kind != target.kind) && !widening &&
            (operand_type.kind == TypeInfo::Kind::Boolean ||
             operand_type.kind == TypeInfo::Kind::Integer ||
             operand_type.kind == TypeInfo::Kind::Number ||
             operand_type.kind == TypeInfo::Kind::String) &&
            (target.kind == TypeInfo::Kind::Boolean || target.kind == TypeInfo::Kind::Integer ||
             target.kind == TypeInfo::Kind::Number || target.kind == TypeInfo::Kind::String);

        if (different_primitives) {
            tc_.warn(std::format("downcast from '{}' to '{}' will always fail at runtime",
                                 operand_type.to_string(), target.to_string()),
                     expr.location,
                     "these types are incompatible — this downcast can never succeed");
        }

        // Warn when the operand is already the target type — the downcast is redundant.
        if (!different_primitives && operand_type == target) {
            tc_.warn(std::format("redundant downcast: value is already of type '{}'",
                                 target.to_string()),
                     expr.location, "remove the downcast — the value is already the correct type");
        }
    }

    return expr.is_trusted ? target : TypeInfo::make_result(target);
}

TypeInfo ExpressionTypeChecker::visit_is(const IsExpression& expr) {
    (void)infer_expression_type(*expr.operand);

    // Error on unknown type name in is<T>.
    if (expr.target_type.is_plain() && !expr.target_type.name().empty()) {
        const auto target = tc_.resolve_type(expr.target_type);

        if (target.kind == TypeInfo::Kind::Unknown) {
            const auto hint = tc_.suggest_type_name(expr.target_type.name());
            tc_.error(std::format("unknown type '{}' in is<T>", expr.target_type.name()),
                      expr.location,
                      hint.empty() ? "check the spelling or make sure the type is declared" : hint);
        }
    }

    return TypeInfo::make(TypeInfo::Kind::Boolean);
}

TypeInfo ExpressionTypeChecker::visit_success(const SuccessExpression& expr) {
    const auto inner = infer_expression_type(*expr.value);

    return TypeInfo::make_result(inner);
}

TypeInfo ExpressionTypeChecker::visit_failure(const FailureExpression& expr) {
    const auto error_type = infer_expression_type(*expr.message);

    // failure(msg) is assignable to result<T, E> for any T.
    // Use StdlibAny as the success type so it's assignable to any result.
    return TypeInfo::make_result(TypeInfo::make(TypeInfo::Kind::StdlibAny), error_type);
}

TypeInfo ExpressionTypeChecker::visit_some(const SomeExpression& expr) {
    const auto inner = infer_expression_type(*expr.value);

    if (inner.kind == TypeInfo::Kind::None) {
        tc_.error("cannot wrap 'none' in 'some'; use none literal directly", expr.location,
                  "'some(none)' is meaningless — use 'none' instead");

        return TypeInfo::make_optional(TypeInfo::make(TypeInfo::Kind::StdlibAny));
    }

    return TypeInfo::make_optional(inner);
}

TypeInfo ExpressionTypeChecker::visit_range(const RangeExpression& expr) {
    (void)type_check_helpers::infer_and_require_kind(
        tc_, *expr.start, TypeInfo::Kind::Integer, expr.start->location,
        "range start must be integer", "ranges only support integer bounds");

    (void)type_check_helpers::infer_and_require_kind(
        tc_, *expr.end, TypeInfo::Kind::Integer, expr.end->location, "range end must be integer",
        "ranges only support integer bounds");

    return TypeInfo::make(TypeInfo::Kind::Range);
}

} // namespace luma
