#include <algorithm>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/diagnostics/diagnostic_builders.hpp"
#include "analysis/types/expression_type_checker.hpp"
#include "analysis/types/match_arm_binding.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checker.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "common/string_utils.hpp"
#include "symbols/qualified_name.hpp"

namespace luma {

TypeInfo ExpressionTypeChecker::visit_lambda(const LambdaExpression& expr) {
    auto scope = tc_.make_scope_guard();

    // Lambda bodies are independent scopes — the pipe flag must not leak in.
    const bool saved_in_pipe = tc_.context().is_in_pipe;
    tc_.context().is_in_pipe = false;

    TypeInfo func_type{
        .kind = TypeInfo::Kind::Func, .name = {}, .inner_types = {}, .return_type = {}};

    for (const auto& param : expr.parameters) {
        const auto param_type = tc_.resolve_type(param.type);

        func_type.inner_types.push_back(param_type);

        tc_.context().current_scope->define(param.name, param_type,
                                            {.is_mutable = param.is_mutable,
                                             .is_unique = param.type.is_unique,
                                             .is_borrow = param.type.is_borrow});
    }

    const auto saved_return = tc_.context().current_return_type;

    if (expr.return_type) {
        tc_.context().current_return_type = tc_.resolve_type(*expr.return_type);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): assigned on the line above.
        func_type.return_type = std::make_shared<TypeInfo>(*tc_.context().current_return_type);
    } else {
        tc_.context().current_return_type = TypeInfo::make(TypeInfo::Kind::StdlibAny);
        func_type.return_type =
            std::make_shared<TypeInfo>(TypeInfo::make(TypeInfo::Kind::StdlibAny));
    }

    if (expr.is_expression_body() && (expr.expression_body() != nullptr)) {
        const auto body_type = infer_expression_type(*expr.expression_body());

        const auto& current_return_type = tc_.context().current_return_type;
        if (expr.return_type && current_return_type) {
            if (!tc_.is_assignable(*current_return_type, body_type)) {
                tc_.error(std::format("lambda body type '{}' does not match "
                                      "declared return type '{}'",
                                      body_type.to_string(), current_return_type->to_string()),
                          expr.expression_body()->location,
                          "change the return type annotation or adjust the lambda body");
            }
        } else {
            // No explicit return type — infer from the expression body.
            func_type.return_type = std::make_shared<TypeInfo>(body_type);
            tc_.context().current_return_type = body_type;
        }
    } else {
        for (const auto& s : expr.statements()) {
            tc_.check_statement(*s);
        }
    }

    tc_.context().current_return_type = saved_return;
    tc_.context().is_in_pipe = saved_in_pipe;

    return func_type;
}

TypeInfo ExpressionTypeChecker::visit_if(const IfExpression& expr) {
    return infer_conditional_result(
        *expr.condition,
        [&] {
            return expr.then_expr() != nullptr ? infer_expression_type(*expr.then_expr())
                                               : infer_block_result(expr.then_body());
        },
        [&] {
            return expr.else_expr() != nullptr ? infer_expression_type(*expr.else_expr())
                                               : infer_block_result(expr.else_body());
        },
        expr.location);
}

TypeInfo ExpressionTypeChecker::infer_if_statement_result(const IfStatement& stmt) {
    return infer_conditional_result(
        *stmt.condition, [&] { return infer_block_result(stmt.then_body); },
        [&] {
            return stmt.else_body.empty() ? TypeInfo::make(TypeInfo::Kind::Void)
                                          : infer_block_result(stmt.else_body);
        },
        stmt.location);
}

TypeInfo ExpressionTypeChecker::infer_conditional_result(
    const Expression& condition, const std::function<TypeInfo()>& compute_then,
    const std::function<TypeInfo()>& compute_else, const SourceLocation& location) {
    const auto cond_type = infer_expression_type(condition);

    (void)type_check_helpers::require_boolean_operand(tc_, cond_type, "if condition",
                                                      condition.location);

    // Flow-sensitive type narrowing: check for is<T>(variable) condition.
    std::string narrowed_var;
    TypeInfo narrowed_type{TypeInfo::make(TypeInfo::Kind::Unknown)};
    const bool has_refinement = try_extract_is_refinement(condition, narrowed_var, narrowed_type);

    TypeInfo then_type = TypeInfo::make(TypeInfo::Kind::Void);
    TypeInfo else_type = TypeInfo::make(TypeInfo::Kind::Void);

    // Flow-sensitive ownership: snapshot before branches.
    const auto before = tc_.context().current_scope->snapshot_ownership();

    {
        auto scope = tc_.make_scope_guard();

        const auto mark = refinement_mark();

        if (has_refinement) {
            push_refinement(narrowed_var, narrowed_type);
        }

        then_type = compute_then();

        pop_refinements(mark);
    }

    const auto after_then = tc_.context().current_scope->snapshot_ownership();

    // Restore before checking else.
    tc_.context().current_scope->restore_ownership(before);

    {
        auto scope = tc_.make_scope_guard();

        else_type = compute_else();
    }

    const auto after_else = tc_.context().current_scope->snapshot_ownership();

    // Look up a variable's consumed state within a branch snapshot, falling
    // back to its pre-branch state when the branch left it untouched.
    const auto consumed_in_branch = [](const TypeScope::OwnershipSnapshot& snapshot,
                                       const std::string& name, bool fallback) {
        const auto entry = std::ranges::find_if(
            snapshot, [&](const auto& ownership) { return ownership.first == name; });

        return entry != snapshot.end() ? entry->second : fallback;
    };

    // Merge: a variable consumed in either branch is consumed afterwards.
    for (const auto& [name, was_consumed_before] : before) {
        const bool consumed_in_then = consumed_in_branch(after_then, name, was_consumed_before);
        const bool consumed_in_else = consumed_in_branch(after_else, name, was_consumed_before);

        if (consumed_in_then || consumed_in_else) {
            tc_.context().current_scope->mark_consumed(name, true);
        }
    }

    return merge_if_branch_types(then_type, else_type, location);
}

TypeInfo ExpressionTypeChecker::merge_if_branch_types(const TypeInfo& then_type,
                                                      const TypeInfo& else_type,
                                                      const SourceLocation& location) {
    if (then_type.kind == TypeInfo::Kind::Void && else_type.kind == TypeInfo::Kind::Void) {
        return TypeInfo::make(TypeInfo::Kind::Void);
    }

    if (then_type.kind == TypeInfo::Kind::Void) {
        return else_type;
    }

    if (else_type.kind == TypeInfo::Kind::Void) {
        return then_type;
    }

    if (!tc_.is_assignable(then_type, else_type) && !tc_.is_assignable(else_type, then_type)) {
        tc_.error(std::format("if branches have different types: '{}' and '{}'",
                              then_type.to_string(), else_type.to_string()),
                  location, "ensure both branches return the same type, or add a type annotation");
    }

    // Return the wider type: if else_type accepts then_type, use else_type.
    if (tc_.is_assignable(else_type, then_type) && !tc_.is_assignable(then_type, else_type)) {
        return else_type;
    }

    return then_type;
}

TypeInfo ExpressionTypeChecker::visit_match(const MatchExpression& expr) {
    return infer_match_result(*expr.subject, expr.arms, expr.location);
}

TypeInfo ExpressionTypeChecker::infer_match_result(const Expression& subject,
                                                   const std::vector<MatchArm>& arms,
                                                   const SourceLocation& location) {
    const auto subject_type = infer_expression_type(subject);

    tc_.check_match_exhaustiveness(arms, subject_type, location);

    // Flow-sensitive ownership: snapshot before arms.
    const auto before = tc_.context().current_scope->snapshot_ownership();

    std::vector<TypeScope::OwnershipSnapshot> arm_snapshots;

    TypeInfo result_type = TypeInfo::make(TypeInfo::Kind::Unknown);
    bool first{true};

    for (const auto& arm : arms) {
        // Restore to pre-match state before each arm.
        tc_.context().current_scope->restore_ownership(before);

        tc_.push_scope();

        if (arm.comparison_value()) {
            (void)infer_expression_type(*arm.comparison_value());
        }

        // Bind success/failure/some names.
        match_arm_binding::bind_arm_names(tc_, arm, subject_type);

        // Bind choice variant destructured fields.
        match_arm_binding::bind_choice_fields(tc_, arm, subject_type, location, false);

        // Type-check optional guard expression.
        if (arm.guard) {
            const auto guard_type = infer_expression_type(*arm.guard);

            (void)type_check_helpers::require_boolean_operand(tc_, guard_type, "match guard",
                                                              arm.guard->location);
        }

        TypeInfo arm_type = TypeInfo::make(TypeInfo::Kind::Void);

        if (arm.body_expr) {
            arm_type = infer_expression_type(*arm.body_expr);
        } else {
            arm_type = infer_block_result(arm.body);
        }

        if (first) {
            result_type = arm_type;
            first = false;
        } else if (result_type.kind == TypeInfo::Kind::Unknown &&
                   arm_type.kind != TypeInfo::Kind::Unknown &&
                   arm_type.kind != TypeInfo::Kind::Void) {
            // First arm resolved to Unknown (error recovery); adopt this arm's type.
            result_type = arm_type;
        } else if (result_type.kind != TypeInfo::Kind::Void &&
                   arm_type.kind != TypeInfo::Kind::Void &&
                   result_type.kind != TypeInfo::Kind::Unknown &&
                   arm_type.kind != TypeInfo::Kind::Unknown) {
            if (!tc_.is_assignable(result_type, arm_type) &&
                !tc_.is_assignable(arm_type, result_type)) {
                tc_.error(std::format("match arms have different types: '{}' and '{}'",
                                      result_type.to_string(), arm_type.to_string()),
                          location, "ensure all match arms return the same type");
            }
        }

        tc_.pop_scope();

        // Capture ownership state after this arm.
        arm_snapshots.push_back(tc_.context().current_scope->snapshot_ownership());
    }

    match_arm_binding::merge_arm_ownership(tc_, before, arm_snapshots);

    return result_type;
}

bool ExpressionTypeChecker::check_internal_record_access(const RecordCreationExpression& expr) {
    if (!tc_.is_internal_member(expr.type_name)) {
        return false;
    }

    const auto split = split_module(expr.type_name);

    if (!split) {
        return false;
    }

    const auto ns = split->first;
    const auto short_name = split->second;

    if (tc_.context().current_namespace == ns) {
        return false;
    }

    tc_.error(std::format("'{}' is internal to namespace '{}' and cannot "
                          "be accessed from outside",
                          short_name, ns),
              expr.location,
              "internal members are private to their namespace — use a public API instead");

    return true;
}

void ExpressionTypeChecker::check_record_provided_fields(const RecordDeclaration& record,
                                                         const RecordCreationExpression& expr) {
    for (const auto& decl_field : record.fields) {
        const auto init_it = std::ranges::find_if(
            expr.fields, [&](const auto& f) { return f.name == decl_field.name; });

        if (init_it == expr.fields.end()) {
            // Field not provided — only allowed if the field has a default.
            if (!decl_field.default_value) {
                tc_.error(std::format("missing field '{}' in record creation of '{}'",
                                      decl_field.name, expr.type_name),
                          expr.location,
                          "provide a value for all required fields, or add a default in the record "
                          "definition",
                          DiagnosticCode::UndefinedField);
            }
        } else {
            const auto expected_type = tc_.resolve_type(decl_field.type);
            const auto actual_type = infer_expression_type(*init_it->value);

            if (!tc_.is_assignable(expected_type, actual_type)) {
                const auto diag = diag_builders::field_type_mismatch(
                    decl_field.name, expected_type, actual_type,
                    "ensure the field value matches the type declared in the record");
                tc_.error(diag.message, init_it->value->location, diag.hint,
                          DiagnosticCode::TypeMismatch);
            }
        }
    }
}

void ExpressionTypeChecker::check_record_unknown_fields(const RecordDeclaration& record,
                                                        const RecordCreationExpression& expr) {
    for (const auto& init_field : expr.fields) {
        const bool found = std::ranges::any_of(
            record.fields, [&](const auto& f) { return f.name == init_field.name; });

        if (!found) {
            std::vector<std::string_view> field_names;
            field_names.reserve(record.fields.size());
            for (const auto& f : record.fields) {
                field_names.emplace_back(f.name);
            }

            tc_.error(
                std::format("unknown field '{}' in record '{}'", init_field.name, expr.type_name),
                init_field.value->location, suggest_name(field_names, init_field.name),
                DiagnosticCode::UndefinedField);
        }
    }
}

TypeInfo ExpressionTypeChecker::visit_record_creation(const RecordCreationExpression& expr) {
    const auto rec_it = tc_.records().find(expr.type_name);

    if (rec_it == tc_.records().end()) {
        tc_.error(std::format("unknown record type '{}'", expr.type_name), expr.location,
                  "check that the record type is defined and imported");

        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    // Block access to internal records from outside their namespace.
    if (check_internal_record_access(expr)) {
        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    const auto& record = *rec_it->second;
    const auto& canonical_name = rec_it->second->name;

    // Push type parameter bindings for generic record instantiation.  The
    // scope pops them on every exit path.
    const bool is_generic = !record.type_params.empty() && !expr.type_args.empty();

    std::optional<GenericResolver::BindingScope> generic_scope;

    if (is_generic) {
        generic_scope.emplace(tc_.generics(), record.type_params, expr.type_args);
    } else if (!record.type_params.empty()) {
        generic_scope.emplace(tc_.generics(), record.type_params);
    }

    check_record_provided_fields(record, expr);
    check_record_unknown_fields(record, expr);

    // Build result type: generic if type args were provided.
    if (is_generic) {
        std::vector<TypeInfo> resolved_args;
        resolved_args.reserve(expr.type_args.size());

        for (const auto& arg : expr.type_args) {
            resolved_args.push_back(tc_.resolve_type(arg));
        }

        return TypeInfo::make_generic(TypeInfo::Kind::Record, canonical_name,
                                      std::move(resolved_args));
    }

    return TypeInfo::make_named(TypeInfo::Kind::Record, canonical_name);
}

TypeInfo ExpressionTypeChecker::visit_record_with(const RecordWithExpression& expr) {
    auto base_type = infer_expression_type(*expr.base);

    if (base_type.kind != TypeInfo::Kind::Record) {
        tc_.error("'with' expression requires a record value", expr.location,
                  "'with' creates a modified copy of a record, e.g. point with { x = 10 }");

        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    auto rec_it = tc_.records().find(base_type.name);

    // Stdlib records are registered in the record map only under their
    // fully-qualified name (e.g. "Terminal.Style"), yet a record TypeInfo carries
    // just the short declaration name ("Style") — see resolve_user_named.  When
    // the direct lookup misses, fall back to a short-name match so `with` works on
    // stdlib records (Terminal.Style, DateTime.Zoned, …), not only on user records
    // (which are also keyed by their bare name).  The match must be UNIQUE: some
    // stdlib records share a short name across namespaces (e.g. DateTime.Interval
    // vs Math.Interval, or the several *.ParseError records), and the record map
    // is unordered, so binding the first hit could silently resolve to the wrong
    // record.  On an ambiguous (or absent) match, leave rec_it at end() so the
    // "unknown record type" error below fires — the pre-existing behaviour for
    // every stdlib record before this fallback existed.
    if (rec_it == tc_.records().end()) {
        for (auto it = tc_.records().begin(); it != tc_.records().end(); ++it) {
            if (it->second->name != base_type.name) {
                continue;
            }

            if (rec_it != tc_.records().end()) {
                // A second match makes the short name ambiguous — refuse to guess.
                rec_it = tc_.records().end();
                break;
            }

            rec_it = it;
        }
    }

    if (rec_it == tc_.records().end()) {
        tc_.error(std::format("unknown record type '{}'", base_type.name), expr.location,
                  "check that the record type is defined and imported");

        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    const auto& record = *rec_it->second;

    // Bind generic type params from the concrete type arguments so overridden
    // field types that reference them resolve; the guard restores prior
    // bindings on destruction.
    const auto binding_guard =
        type_check_helpers::bind_type_params(tc_, record.type_params, base_type.inner_types);

    for (const auto& override_field : expr.overrides) {
        const auto decl_it = std::ranges::find_if(
            record.fields, [&](const auto& f) { return f.name == override_field.name; });

        if (decl_it == record.fields.end()) {
            tc_.error(std::format("'with' expression: record '{}' has no field '{}'",
                                  base_type.name, override_field.name),
                      override_field.value->location,
                      "check the record definition for available field names",
                      DiagnosticCode::UndefinedField);
        } else {
            const auto expected_type = tc_.resolve_type(decl_it->type);
            const auto actual_type = infer_expression_type(*override_field.value);

            if (!tc_.is_assignable(expected_type, actual_type)) {
                const auto diag = diag_builders::field_type_mismatch(
                    override_field.name, expected_type, actual_type,
                    "ensure the override value matches the field's declared type");
                tc_.error(diag.message, override_field.value->location, diag.hint,
                          DiagnosticCode::TypeMismatch);
            }
        }
    }

    return base_type;
}

} // namespace luma
