#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <optional>
#include <string_view>
#include <system_error>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/types/expression_type_checker.hpp"
#include "analysis/types/generic_resolver.hpp"
#include "analysis/types/stdlib_type_handler.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checker.hpp"
#include "analysis/types/type_checking_context.hpp"

namespace luma {

namespace {

// Rejects access to a namespace member marked internal when the access
// originates outside that namespace: emits the diagnostic and yields the
// Unknown recovery type.  Returns nullopt when access is permitted, so the
// caller can continue resolving the member.
[[nodiscard]] std::optional<TypeInfo>
reject_external_internal_access(TypeCheckingServices& tc, std::string_view qualified_name,
                                std::string_view namespace_name, std::string_view member_name,
                                const SourceLocation& loc) {
    if (tc.is_internal_member(qualified_name) && tc.context().current_namespace != namespace_name) {
        tc.error(std::format("'{}' is internal to namespace '{}' and cannot "
                             "be accessed from outside",
                             member_name, namespace_name),
                 loc);

        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    return std::nullopt;
}

} // namespace

// ── Orchestrator ─────────────────────────────────────────────
TypeInfo ExpressionTypeChecker::infer_field_access_inner(const FieldAccessExpression& expr) {
    const auto object_type = infer_expression_type(*expr.object);

    // Optional chaining: x?.field — infer the same type as a regular field
    // access (the concrete field type when it is known) so that callers
    // retain type information after ?. chains.  The result may still be
    // null at runtime, but that is the caller's concern; the type checker
    // preserves whatever concrete type the field declares.
    // Fall through to the normal field-inference path below.

    if (const auto member = infer_namespace_member(expr)) {
        return *member;
    }

    switch (object_type.kind) {
        case TypeInfo::Kind::Choice:
            return infer_choice_member(expr, object_type);
        case TypeInfo::Kind::Interface:
            if (const auto member = infer_interface_member(expr, object_type)) {
                return *member;
            }
            break;
        case TypeInfo::Kind::Optional:
            if (const auto member = infer_optional_member(expr, object_type)) {
                return *member;
            }
            break;
        case TypeInfo::Kind::Tuple:
            if (const auto member = infer_tuple_member(expr, object_type)) {
                return *member;
            }
            break;
        case TypeInfo::Kind::Record:
            if (const auto member = infer_record_member(expr, object_type)) {
                return *member;
            }
            break;
        default:
            break;
    }

    return TypeInfo::make(TypeInfo::Kind::StdlibAny);
}

// ── Choice variant resolution ────────────────────────────────
// Shared by infer_namespace_member (bare ChoiceName.Variant) and
// infer_choice_member (qualified Ns.Choice.Variant).  Centralises the generic
// push/pop lifecycle and the unit-vs-data constructor typing so the logic lives
// in exactly one place.
std::optional<TypeInfo> ExpressionTypeChecker::resolve_choice_variant(std::string_view choice_name,
                                                                      std::string_view variant_name,
                                                                      const SourceLocation& loc) {
    const auto* choice_decl = tc_.find_choice(choice_name);

    if (choice_decl == nullptr) {
        return std::nullopt;
    }

    // Push the choice's type params as Unknown so that variant field types
    // referencing them (including the choice type itself for recursive ADTs)
    // resolve.  The RAII scope pops them on every exit path.
    std::optional<GenericResolver::BindingScope> generic_scope;

    if (!choice_decl->type_params.empty()) {
        generic_scope.emplace(tc_.generics(), choice_decl->type_params);
    }

    for (const auto& variant : choice_decl->variants) {
        if (variant.name != variant_name) {
            continue;
        }

        if (variant.fields.empty()) {
            // Unit variant — the name denotes the choice type itself.
            return TypeInfo::make_named(TypeInfo::Kind::Choice, std::string{choice_name});
        }

        // Data variant — a Func that constructs the choice type.
        std::vector<TypeInfo> field_types;

        field_types.reserve(variant.fields.size());
        for (const auto& field : variant.fields) {
            field_types.push_back(tc_.resolve_type(field.type));
        }

        auto func_type = TypeInfo::make_func(
            std::move(field_types),
            TypeInfo::make_named(TypeInfo::Kind::Choice, std::string{choice_name}));

        // Store the choice name on the func type so that infer_call can perform
        // generic inference for choice variant constructors.
        func_type.name = std::string{choice_name};

        return func_type;
    }

    tc_.error(std::format("choice '{}' has no variant '{}'", choice_name, variant_name), loc, "",
              DiagnosticCode::UndefinedField);

    return TypeInfo::make_named(TypeInfo::Kind::Choice, std::string{choice_name});
}

// ── Namespace / stdlib member access ─────────────────────────
std::optional<TypeInfo>
ExpressionTypeChecker::infer_namespace_member(const FieldAccessExpression& expr) {
    // Namespace/stdlib member access.
    if (expr.object->kind != ExpressionKind::Variable) {
        return std::nullopt;
    }

    const auto& var = static_cast<const VariableExpression&>(*expr.object);

    // Choice variant access: ChoiceName.Variant — returns the choice type
    // (for unit variants) or acts as a constructor (for data variants).
    if (auto variant_type = resolve_choice_variant(var.name, expr.field_name, expr.location)) {
        return variant_type;
    }

    // Stdlib namespace — look up known return type, else StdlibAny.
    if (tc_.is_stdlib_namespace(var.name)) {
        const auto qualified = var.name + "." + expr.field_name;
        const auto* sig_ptr = tc_.stdlib_handler().get_return_type(qualified);

        if (sig_ptr != nullptr) {
            return *sig_ptr;
        }

        // Check for stdlib-defined types (e.g. Log.Level).
        if (tc_.records().contains(qualified)) {
            return TypeInfo::make_named(TypeInfo::Kind::Record, tc_.records().at(qualified)->name);
        }

        if (tc_.choices().contains(qualified)) {
            return TypeInfo::make_named(TypeInfo::Kind::Choice, qualified);
        }

        return TypeInfo::make(TypeInfo::Kind::StdlibAny);
    }

    // User-defined namespace.
    const auto ns_it = tc_.namespace_functions().find(var.name);

    if (ns_it != tc_.namespace_functions().end()) {
        const auto fn_it = ns_it->second.find(expr.field_name);

        if (fn_it != ns_it->second.end()) {
            const auto qualified = var.name + "." + expr.field_name;

            if (auto denied = reject_external_internal_access(tc_, qualified, var.name,
                                                              expr.field_name, expr.location)) {
                return denied;
            }

            const auto& func = *fn_it->second;

            std::vector<TypeInfo> param_types;

            param_types.reserve(func.parameters.size());
            for (const auto& param : func.parameters) {
                param_types.push_back(tc_.resolve_type(param.type));
            }

            return TypeInfo::make_func(std::move(param_types), tc_.resolve_type(func.return_type));
        }
    }

    // Namespace-qualified type access: Namespace.TypeName.
    // Checked outside the tc_.namespace_functions() block so that
    // namespaces containing only types (no functions) are handled too.
    const auto qualified_type = var.name + "." + expr.field_name;

    if (tc_.records().contains(qualified_type)) {
        if (auto denied = reject_external_internal_access(tc_, qualified_type, var.name,
                                                          expr.field_name, expr.location)) {
            return denied;
        }

        return TypeInfo::make_named(TypeInfo::Kind::Record, tc_.records().at(qualified_type)->name);
    }

    if (tc_.interfaces().contains(qualified_type)) {
        if (auto denied = reject_external_internal_access(tc_, qualified_type, var.name,
                                                          expr.field_name, expr.location)) {
            return denied;
        }

        return TypeInfo::make_named(TypeInfo::Kind::Interface,
                                    tc_.interfaces().at(qualified_type)->name);
    }

    if (tc_.choices().contains(qualified_type)) {
        if (auto denied = reject_external_internal_access(tc_, qualified_type, var.name,
                                                          expr.field_name, expr.location)) {
            return denied;
        }

        return TypeInfo::make_named(TypeInfo::Kind::Choice, qualified_type);
    }

    if (ns_it != tc_.namespace_functions().end()) {
        // Build a hint listing available members (up to 5).
        std::string hint;
        const auto& members = ns_it->second;

        if (!members.empty()) {
            std::vector<std::string_view> names;
            names.reserve(members.size());

            for (const auto& [name, _] : members) {
                names.push_back(name);
            }

            std::ranges::sort(names);

            hint = "available members: ";

            const auto limit = std::min(names.size(), std::size_t{5});

            for (std::size_t i{0}; i < limit; ++i) {
                if (i > 0) {
                    hint += ", ";
                }
                hint += names[i];
            }

            if (names.size() > limit) {
                hint += std::format(", ... ({} more)", names.size() - limit);
            }
        }

        tc_.error(std::format("namespace '{}' has no member '{}'", var.name, expr.field_name),
                  expr.location, hint, DiagnosticCode::UndefinedField);

        return TypeInfo::make(TypeInfo::Kind::Unknown);
    }

    return std::nullopt;
}

// ── Choice variant access ────────────────────────────────────
// Reached when the object type is already known to be a choice (e.g. a
// qualified path like Ns.Shape.Circle where Ns.Shape resolves to
// TypeInfo{Choice, "Ns.Shape"}).
TypeInfo ExpressionTypeChecker::infer_choice_member(const FieldAccessExpression& expr,
                                                    const TypeInfo& object_type) {
    if (auto variant_type =
            resolve_choice_variant(object_type.name, expr.field_name, expr.location)) {
        return *variant_type;
    }

    return TypeInfo::make_named(TypeInfo::Kind::Choice, object_type.name);
}

// ── Interface field access ───────────────────────────────────
std::optional<TypeInfo>
ExpressionTypeChecker::infer_interface_member(const FieldAccessExpression& expr,
                                              const TypeInfo& object_type) {
    const auto iface_it = tc_.interfaces().find(object_type.name);

    if (iface_it != tc_.interfaces().end()) {
        // Bind generic type params from the concrete type arguments so field
        // types that reference them resolve; the guard restores prior bindings
        // on every exit path.
        const auto& iface_decl = *iface_it->second;
        const auto binding_guard = type_check_helpers::bind_type_params(tc_, iface_decl.type_params,
                                                                        object_type.inner_types);

        TypeInfo result_type = TypeInfo::make(TypeInfo::Kind::StdlibAny);
        bool found = false;

        for (const auto& field : iface_decl.fields) {
            if (field.name == expr.field_name) {
                result_type = tc_.resolve_type(field.type);
                found = true;
                break;
            }
        }

        if (found) {
            return result_type;
        }

        tc_.error(
            std::format("interface '{}' has no field '{}'", object_type.name, expr.field_name),
            expr.location, "", DiagnosticCode::UndefinedField);
    }

    return std::nullopt;
}

// ── Optional field access ────────────────────────────────────
// Handles optional chaining (x?.field) or direct field access on an optional
// wrapping a record / interface / tuple.
std::optional<TypeInfo>
ExpressionTypeChecker::infer_optional_member(const FieldAccessExpression& expr,
                                             const TypeInfo& object_type) {
    const auto inner_opt =
        type_check_helpers::unwrap_optional_or_error(tc_, object_type, expr.location);

    if (!inner_opt) {
        return TypeInfo::make(TypeInfo::Kind::StdlibAny);
    }

    const auto& inner = *inner_opt;

    // Field access through the optional wrapper resolves against the wrapped
    // type: records, interfaces, and tuples all reuse the same field-lookup
    // helpers as direct access.
    if (inner.kind == TypeInfo::Kind::Record) {
        return infer_record_member(expr, inner);
    }

    if (inner.kind == TypeInfo::Kind::Interface) {
        return infer_interface_member(expr, inner);
    }

    if (inner.kind == TypeInfo::Kind::Tuple) {
        return infer_tuple_member(expr, inner);
    }

    return std::nullopt;
}

// ── Tuple numeric field access (pair.0, triple.2) ────────────
std::optional<TypeInfo> ExpressionTypeChecker::infer_tuple_member(const FieldAccessExpression& expr,
                                                                  const TypeInfo& object_type) {
    // Optional-chaining wrapping (for `pair?.0`) is applied once by the caller
    // in visit_field_access, so this helper returns the raw element type and,
    // like its record/interface siblings, never wraps.
    int element_index{};
    const char* const name_begin = expr.field_name.data();
    const char* const name_end = name_begin + expr.field_name.size();
    const auto [parse_end, ec] = std::from_chars(name_begin, name_end, element_index);

    // A field name that is not a whole number denotes a non-tuple access (for
    // example a named field on some other receiver): report no error and let
    // the caller fall through to its default.
    if (ec != std::errc{} || parse_end != name_end) {
        return std::nullopt;
    }

    if (element_index < 0 || element_index >= static_cast<int>(object_type.inner_types.size())) {
        tc_.error(std::format("tuple index {} out of bounds (tuple has {} elements)", element_index,
                              object_type.inner_types.size()),
                  expr.location);

        return std::nullopt;
    }

    return object_type.inner_types[static_cast<std::size_t>(element_index)];
}

// ── Record field access ──────────────────────────────────────
std::optional<TypeInfo>
ExpressionTypeChecker::infer_record_member(const FieldAccessExpression& expr,
                                           const TypeInfo& object_type) {
    const auto rec_it = tc_.records().find(object_type.name);

    if (rec_it != tc_.records().end()) {
        // Bind generic type params from the concrete type arguments so field
        // types that reference them resolve; the guard restores prior bindings
        // on every exit path.
        const auto& rec_decl = *rec_it->second;
        const auto binding_guard = type_check_helpers::bind_type_params(tc_, rec_decl.type_params,
                                                                        object_type.inner_types);

        TypeInfo result_type = TypeInfo::make(TypeInfo::Kind::StdlibAny);
        bool found = false;

        for (const auto& field : rec_decl.fields) {
            if (field.name == expr.field_name) {
                result_type = tc_.resolve_type(field.type);
                found = true;
                break;
            }
        }

        if (found) {
            return result_type;
        }

        tc_.error(std::format("record '{}' has no field '{}'", object_type.name, expr.field_name),
                  expr.location, "", DiagnosticCode::UndefinedField);
    }

    return std::nullopt;
}

} // namespace luma
