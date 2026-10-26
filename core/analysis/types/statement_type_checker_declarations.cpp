#include <format>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/types/match_arm_binding.hpp"
#include "analysis/types/statement_type_checker.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "analysis/types/type_info.hpp"

namespace luma {

void StatementTypeChecker::visit_variable_declaration(const VariableDeclStatement& stmt) {
    // Report first: it knows the declaration's location, and each problem in
    // the annotation is reported only once.
    (void)type_check_helpers::report_unknown_types(tc_, stmt.type, "", stmt.location);

    const auto declared_type = tc_.resolve_type(stmt.type);

    if (stmt.initializer) {
        const auto init_type = tc_.infer_expression_type(*stmt.initializer);

        // Warn when storing the result of a void function.
        if (init_type.kind == TypeInfo::Kind::Void) {
            tc_.warn("assigning result of void function to a variable has no effect", stmt.location,
                     "the function does not return a value — call it as a standalone statement");
        }

        // Linter: optional chain result not unwrapped.
        // Warn when a ?. or ?[ expression is assigned to a non-optional variable
        // without a ?? fallback — the chain may produce none at runtime.
        if (declared_type.kind != TypeInfo::Kind::Optional) {
            const auto& init = *stmt.initializer;

            const bool is_optional_chain =
                (init.kind == ExpressionKind::FieldAccess &&
                 static_cast<const FieldAccessExpression&>(init).is_optional) ||
                (init.kind == ExpressionKind::IndexAccess &&
                 static_cast<const IndexAccessExpression&>(init).is_optional);

            if (is_optional_chain) {
                tc_.warn("optional chain result not unwrapped", stmt.location,
                         "use '?\?' to provide a default value, or declare the variable "
                         "as 'optional<T>'");
            }
        }

        if (type_check_helpers::check_type_assignable(tc_, declared_type, init_type,
                                                      "cannot assign to variable", stmt.location)) {
            type_check_helpers::mark_numeric_widening(tc_, *stmt.initializer, declared_type,
                                                      init_type);
        }
    }

    if (tc_.context().current_scope->has_local(stmt.name)) {
        tc_.error(std::format("variable '{}' is already declared in this scope", stmt.name),
                  stmt.location, "choose a different name, or use the existing variable");
    }

    // Shadowing an outer variable is reported by the linter (W0012), not here.

    tc_.context().current_scope->define(stmt.name, declared_type,
                                        {.is_mutable = stmt.is_mutable,
                                         .is_unique = stmt.type.is_unique,
                                         .is_borrow = stmt.type.is_borrow},
                                        stmt.location);
}

void StatementTypeChecker::visit_tuple_destructuring(const TupleDestructuringStatement& stmt) {
    if (stmt.initializer) {
        const auto init_type = tc_.infer_expression_type(*stmt.initializer);

        if (init_type.kind == TypeInfo::Kind::Tuple) {
            if (init_type.inner_types.size() != stmt.bindings.size()) {
                tc_.error(std::format("tuple destructuring: expected {} elements, got {}",
                                      init_type.inner_types.size(), stmt.bindings.size()),
                          stmt.location);
            }
        } else if (init_type.kind != TypeInfo::Kind::StdlibAny &&
                   init_type.kind != TypeInfo::Kind::Unknown) {
            tc_.error(std::format("cannot destructure a value of type '{}' — only tuples can be "
                                  "destructured into ( … ) bindings",
                                  init_type.to_string()),
                      stmt.location,
                      "destructure a tuple value, or bind the value to one variable");
        }

        std::vector<TypeInfo> declared_types;
        declared_types.reserve(stmt.bindings.size());

        for (std::size_t i{0}; i < stmt.bindings.size(); ++i) {
            const auto& [type_ann, name] = stmt.bindings[i];
            (void)type_check_helpers::report_unknown_types(
                tc_, type_ann, std::format(" for tuple binding '{}'", name), stmt.location);
            const auto declared = tc_.resolve_type(type_ann);
            declared_types.push_back(declared);

            TypeInfo actual = TypeInfo::make(TypeInfo::Kind::StdlibAny);

            if (init_type.kind == TypeInfo::Kind::Tuple && i < init_type.inner_types.size()) {
                actual = init_type.inner_types[i];
            }

            (void)type_check_helpers::check_type_assignable(tc_, declared, actual,
                                                            "tuple destructuring", stmt.location);

            tc_.context().current_scope->define(name, declared, {.is_mutable = stmt.is_mutable});
        }

        type_check_helpers::mark_numeric_widening(
            tc_, *stmt.initializer, TypeInfo::make_tuple(std::move(declared_types)), init_type);
    } else {
        for (const auto& [type_ann, name] : stmt.bindings) {
            (void)type_check_helpers::report_unknown_types(
                tc_, type_ann, std::format(" for tuple binding '{}'", name), stmt.location);
            tc_.context().current_scope->define(name, tc_.resolve_type(type_ann),
                                                {.is_mutable = stmt.is_mutable});
        }
    }
}

void StatementTypeChecker::visit_record_destructuring(const RecordDestructuringStatement& stmt) {
    TypeInfo init_type = TypeInfo::make(TypeInfo::Kind::Unknown);

    if (stmt.initializer) {
        init_type = tc_.infer_expression_type(*stmt.initializer);
    }

    // bind_record_fields validates that init_type is an instance of the record
    // type and binds each listed field to its declared type.
    match_arm_binding::bind_record_fields(tc_, stmt.type_name, stmt.fields, init_type,
                                          stmt.location, stmt.is_mutable);
}

} // namespace luma
