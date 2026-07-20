#include <format>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/types/statement_type_checker.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "analysis/types/type_info.hpp"

namespace luma {

void StatementTypeChecker::visit_variable_declaration(const VariableDeclStatement& stmt) {
    const auto declared_type = tc_.resolve_type(stmt.type);

    if (declared_type.kind == TypeInfo::Kind::Unknown && !stmt.type.name().empty()) {
        const auto hint = tc_.suggest_type_name(stmt.type.name());
        tc_.error(std::format("unknown type '{}'", stmt.type.name()), stmt.location,
                  hint.empty() ? "check the spelling or make sure the type is declared" : hint);
    }

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

        (void)type_check_helpers::check_type_assignable(tc_, declared_type, init_type,
                                                        "cannot assign to variable", stmt.location);
    }

    if (tc_.context().current_scope->has_local(stmt.name)) {
        tc_.error(std::format("variable '{}' is already declared in this scope", stmt.name),
                  stmt.location, "choose a different name, or use the existing variable");
    }

    // Linter: shadowed variable warning.
    if (!stmt.name.starts_with('_')) {
        const auto parent_scope = tc_.context().current_scope->parent();
        const auto* outer = parent_scope ? parent_scope->lookup(stmt.name) : nullptr;

        if (outer != nullptr) {
            tc_.warn(std::format("'{}' shadows an outer variable", stmt.name), stmt.location,
                     "choose a different name or prefix with '_' to suppress",
                     DiagnosticCode::ShadowedVariable);
        }
    }

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
        }

        for (std::size_t i{0}; i < stmt.bindings.size(); ++i) {
            const auto& [type_ann, name] = stmt.bindings[i];
            const auto declared = tc_.resolve_type(type_ann);

            TypeInfo actual = TypeInfo::make(TypeInfo::Kind::StdlibAny);

            if (init_type.kind == TypeInfo::Kind::Tuple && i < init_type.inner_types.size()) {
                actual = init_type.inner_types[i];
            }

            (void)type_check_helpers::check_type_assignable(tc_, declared, actual,
                                                            "tuple destructuring", stmt.location);

            tc_.context().current_scope->define(name, declared, {.is_mutable = stmt.is_mutable});
        }
    } else {
        for (const auto& [type_ann, name] : stmt.bindings) {
            tc_.context().current_scope->define(name, tc_.resolve_type(type_ann),
                                                {.is_mutable = stmt.is_mutable});
        }
    }
}

} // namespace luma
