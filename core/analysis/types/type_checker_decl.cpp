// ─────────────────────────────────────────────────────────────────────────────
// Declaration Registration and Checking   (TypeChecker partial implementation)
// ─────────────────────────────────────────────────────────────────────────────
// This file implements the two-pass declaration processing:
//
//   Pass 1 — register_declarations(): Walk top-level declarations and populate
//            the symbol registries (symbols_.records, symbols_.choices, symbols_.interfaces, symbols_.functions,
//            symbols_.type_aliases, symbols_.namespace_functions) so that forward references
//            and mutual recursion resolve correctly.
//
//   Pass 2 — check_declaration(): Type-check each declaration body.  Functions
//            are checked via check_function(), which sets up the return-type
//            context and delegates statement checking to StatementTypeChecker.
// ─────────────────────────────────────────────────────────────────────────────

#include <algorithm>
#include <format>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "analysis/ast/ast_dispatch.hpp"
#include "analysis/ast/declaration.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checker.hpp"
#include "common/scope_guard.hpp"
#include "symbols/qualified_name.hpp"

namespace luma {

// ═══════════════════════════════════════════════════════════
// Registration pass
// ═══════════════════════════════════════════════════════════

void TypeChecker::register_declarations(const std::vector<std::unique_ptr<Declaration>>& decls) {
    // Function signatures are resolved (and stored) as each function is
    // registered, so every type-like declaration — records, choices,
    // interfaces, aliases, namespaces — must be registered first; otherwise a
    // type declared later in the file would resolve to the permissive Unknown
    // in an earlier function's signature.  `use` imports come next because
    // they may import functions (resolving their signatures) and bare type
    // names that the remaining function signatures refer to.
    const auto is_type_like = [](const Declaration& decl) {
        return decl.kind != DeclarationKind::Function && decl.kind != DeclarationKind::Use;
    };

    for (const auto& decl : decls) {
        if (is_type_like(*decl)) {
            register_declaration(*decl);
        }
    }

    for (const auto& decl : decls) {
        if (decl->kind == DeclarationKind::Use) {
            register_declaration(*decl);
        }
    }

    for (const auto& decl : decls) {
        if (decl->kind == DeclarationKind::Function) {
            register_declaration(*decl);
        }
    }
}

void TypeChecker::register_declaration(const Declaration& decl) {
    dispatch_declaration(decl, [this](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, FunctionDeclaration>) {
            warn_if_duplicate(symbols_.functions, node.name, "function", node.location);

            symbols_.functions[node.name] = &node;
            registry_.register_symbol(node.name, SuggestionCategory::Function);

            // For generic functions, push dummy type param bindings while
            // resolving the function type signature.
            const GenericResolver::BindingScope generic_scope{generics_, node.type_params};

            define_function_in_scope(node, node.name);

            if (node.is_main) {
                ++ctx_.main_count;

                if (!node.parameters.empty()) {
                    error("@main function must take no parameters", node.location,
                          "remove all parameters from the @main function");
                }
            }

            if (node.is_test && !node.parameters.empty()) {
                error("@test function must take no parameters", node.location,
                      "remove all parameters from the @test function");
            }
        } else if constexpr (std::is_same_v<T, RecordDeclaration>) {
            warn_if_duplicate(symbols_.records, node.name, "record", node.location);

            symbols_.records[node.name] = &node;
            registry_.register_symbol(node.name, SuggestionCategory::Type);

            // Define the record name in scope as a type placeholder.
            current_scope()->define(node.name,
                                    TypeInfo::make_named(TypeInfo::Kind::Record, node.name), {});
        } else if constexpr (std::is_same_v<T, ChoiceDeclaration>) {
            warn_if_duplicate(symbols_.choices, node.name, "choice", node.location);

            symbols_.choices[node.name] = &node;
            registry_.register_symbol(node.name, SuggestionCategory::Type);

            current_scope()->define(node.name,
                                    TypeInfo::make_named(TypeInfo::Kind::Choice, node.name), {});
        } else if constexpr (std::is_same_v<T, InterfaceDeclaration>) {
            warn_if_duplicate(symbols_.interfaces, node.name, "interface", node.location);

            symbols_.interfaces[node.name] = &node;
            registry_.register_symbol(node.name, SuggestionCategory::Type);
        } else if constexpr (std::is_same_v<T, NamespaceDeclaration>) {
            register_namespace(node, "");
        } else if constexpr (std::is_same_v<T, TypeAliasDeclaration>) {
            warn_if_duplicate(symbols_.type_aliases, node.name, "type alias", node.location);

            symbols_.type_aliases[node.name] = node.target_type;
            registry_.register_symbol(node.name, SuggestionCategory::Type);

            if (!node.type_params.empty()) {
                generics_.alias_params()[node.name] = node.type_params;
            }
        } else if constexpr (std::is_same_v<T, UseDeclaration>) {
            register_use_declaration(node);
        }
        // Include — no registration action.
    });
}

void TypeChecker::define_function_in_scope(const FunctionDeclaration& func,
                                           const std::string& name) {
    // check_function() reports internal types in the signature, with the
    // function's location; resolving it here must not report them first.
    ++deferred_internal_access_reports_;
    const ScopeGuard deferred_guard{[this] { --deferred_internal_access_reports_; }};

    std::vector<TypeInfo> param_types;
    param_types.reserve(func.parameters.size());
    std::ranges::transform(func.parameters, std::back_inserter(param_types),
                           [this](const auto& param) { return resolve_type(param.type); });

    current_scope()->define(
        name, TypeInfo::make_func(std::move(param_types), resolve_type(func.return_type)), {});

    if (auto* symbol = current_scope()->lookup_mut(name)) {
        symbol->function_declaration = &func;
    }
}

template <typename T>
void TypeChecker::import_all_namespace_types(StringMap<const T*>& map, TypeInfo::Kind kind,
                                             const std::string& prefix) {
    // Collect matches first: importing inserts bare names into `map`, so
    // mutating it while iterating would invalidate the range iterator.
    std::vector<std::pair<std::string, const T*>> to_import;

    for (const auto& [qname, decl] : map) {
        if (!qname.starts_with(prefix)) {
            continue;
        }

        const auto bare = qname.substr(prefix.size());

        if (symbols::is_qualified_name(bare)) {
            continue; // nested namespace member — skip
        }

        if (internal_members_.contains(qname)) {
            continue; // internal — not exported
        }

        to_import.emplace_back(bare, decl);
    }

    for (const auto& [bare, decl] : to_import) {
        if (!current_scope()->has_local(bare)) {
            current_scope()->define(bare, TypeInfo::make_named(kind, decl->name), {});
            // Also register under the bare name so resolve_type can find it.
            map.emplace(bare, decl);
        }
    }
}

template <typename T>
bool TypeChecker::import_one_namespace_type(StringMap<const T*>& map, TypeInfo::Kind kind,
                                            const std::string& qname, const std::string& bare) {
    const auto it = map.find(qname);

    if (it == map.end()) {
        return false;
    }

    current_scope()->define(bare, TypeInfo::make_named(kind, it->second->name), {});
    map.emplace(bare, it->second);

    return true;
}

void TypeChecker::register_use_declaration(const UseDeclaration& use_decl) {
    const auto& path = use_decl.namespace_path;
    const auto split = symbols::split_module(path);

    // Imported function signatures name their namespace's own types by their
    // bare names, so resolve them from inside that namespace.
    const auto saved_namespace = ctx_.current_namespace;
    ctx_.current_namespace = split ? std::string{split->first} : path;
    const ScopeGuard namespace_guard{[&] { ctx_.current_namespace = saved_namespace; }};

    if (!split) {
        // Wildcard: use Namespace — import all members as bare names.
        const auto prefix = path + ".";

        if (const auto ns_it = symbols_.namespace_functions.find(path);
            ns_it != symbols_.namespace_functions.end()) {
            for (const auto& [name, func] : ns_it->second) {
                std::string qualified = path;
                qualified += '.';
                qualified += name;

                if (internal_members_.contains(qualified)) {
                    continue; // internal — not exported
                }

                if (current_scope()->has_local(name)) {
                    continue; // already imported or user-defined
                }

                define_function_in_scope(*func, name);
            }
        }

        import_all_namespace_types(symbols_.records, TypeInfo::Kind::Record, prefix);
        import_all_namespace_types(symbols_.choices, TypeInfo::Kind::Choice, prefix);

        return;
    }

    // Specific import: use Namespace.member — import a single name.
    const std::string ns{split->first};
    const std::string member{split->second};

    if (internal_members_.contains(path)) {
        error(std::format("'{}' is internal to namespace '{}' and cannot be imported", member, ns),
              use_decl.location,
              "internal members are private to their namespace — use a public API "
              "instead");
        return;
    }

    // Try to import as a function, then as a record, then as a choice.
    if (const auto ns_it = symbols_.namespace_functions.find(ns);
        ns_it != symbols_.namespace_functions.end()) {
        if (const auto func_it = ns_it->second.find(member); func_it != ns_it->second.end()) {
            define_function_in_scope(*func_it->second, member);
            return;
        }
    }

    if (import_one_namespace_type(symbols_.records, TypeInfo::Kind::Record, path, member)) {
        return;
    }

    if (import_one_namespace_type(symbols_.choices, TypeInfo::Kind::Choice, path, member)) {
        return;
    }

    error(std::format("cannot resolve import '{}' from namespace '{}'", member, ns),
          use_decl.location,
          "check that the namespace is defined and the name is spelled correctly");
}

void TypeChecker::register_namespace(const NamespaceDeclaration& ns, std::string_view prefix) {
    const auto qualified = symbols::make_qualified(prefix, ns.name);

    // Helper: register a qualified name as a type symbol, and mark internal if needed.
    auto register_type_member = [&](const std::string& qname, bool is_internal) {
        registry_.register_symbol(qname, SuggestionCategory::Type);
        if (is_internal) {
            internal_members_.insert(qname);
        }
    };

    for (const auto& decl : ns.declarations) {
        if (decl->kind == DeclarationKind::Function) {
            const auto& func = static_cast<const FunctionDeclaration&>(*decl);

            symbols_.namespace_functions[ns.name][func.name] = &func;

            if (decl->is_internal_to_namespace) {
                internal_members_.insert(symbols::make_qualified(qualified, func.name));
            }
        } else if (decl->kind == DeclarationKind::Namespace) {
            const auto& inner_ns = static_cast<const NamespaceDeclaration&>(*decl);

            register_namespace(inner_ns, qualified);
        } else if (decl->kind == DeclarationKind::Record) {
            const auto& rec = static_cast<const RecordDeclaration&>(*decl);
            const auto qname = symbols::make_qualified(qualified, rec.name);
            symbols_.records[qname] = &rec;
            register_type_member(qname, decl->is_internal_to_namespace);
        } else if (decl->kind == DeclarationKind::Choice) {
            const auto& ch = static_cast<const ChoiceDeclaration&>(*decl);
            const auto qname = symbols::make_qualified(qualified, ch.name);
            symbols_.choices[qname] = &ch;
            register_type_member(qname, decl->is_internal_to_namespace);
        } else if (decl->kind == DeclarationKind::Interface) {
            const auto& iface = static_cast<const InterfaceDeclaration&>(*decl);
            const auto qname = symbols::make_qualified(qualified, iface.name);
            symbols_.interfaces[qname] = &iface;
            register_type_member(qname, decl->is_internal_to_namespace);
        } else if (decl->kind == DeclarationKind::TypeAlias) {
            const auto& alias = static_cast<const TypeAliasDeclaration&>(*decl);
            const auto qname = symbols::make_qualified(qualified, alias.name);
            symbols_.type_aliases[qname] = alias.target_type;
            register_type_member(qname, decl->is_internal_to_namespace);
        }
        // Deliberately NOT calling register_declaration(*decl) —
        // namespace members are not visible as bare names until the
        // user writes 'use Namespace'.
    }

    // Define the namespace name in scope.
    if (!current_scope()->has_local(ns.name)) {
        current_scope()->define(ns.name, TypeInfo::make(TypeInfo::Kind::Namespace), {});
    }
}

// ═══════════════════════════════════════════════════════════
// Declaration checking
// ═══════════════════════════════════════════════════════════

void TypeChecker::check_declaration(const Declaration& decl) {
    dispatch_declaration(decl, [this](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, FunctionDeclaration>) {
            check_function(node);
        } else if constexpr (std::is_same_v<T, NamespaceDeclaration>) {
            const auto saved_namespace = ctx_.current_namespace;

            // The fully qualified path ("Outer.Inner"): nested namespace
            // members are registered under it, so a bare sibling name is
            // retried as "<path>.<name>".
            ctx_.current_namespace = symbols::make_qualified(saved_namespace, node.name);

            for (const auto& decl : node.declarations) {
                check_declaration(*decl);
            }

            ctx_.current_namespace = saved_namespace;
        } else if constexpr (std::is_same_v<T, RecordDeclaration>) {
            // Push type parameter bindings for generic records.
            const GenericResolver::BindingScope generic_scope{generics_, node.type_params};

            for (const auto& field : node.fields) {
                (void)type_check_helpers::report_unknown_types(
                    *this, field.type,
                    std::format(" for field '{}' of record '{}'", field.name, node.name),
                    node.location);

                if (!field.default_value) {
                    continue;
                }

                const auto field_type = resolve_type(field.type);
                const auto default_type = infer_expression_type(*field.default_value);

                if (!is_assignable(field_type, default_type)) {
                    error(std::format("default value for field '{}' in record '{}' "
                                      "has type '{}' which is not assignable to '{}'",
                                      field.name, node.name, default_type.to_string(),
                                      field_type.to_string()),
                          field.default_value->location,
                          "ensure the default value matches the field's declared type",
                          DiagnosticCode::TypeMismatch);
                } else {
                    type_check_helpers::mark_numeric_widening(*this, *field.default_value,
                                                              field_type, default_type);
                }
            }
        } else if constexpr (std::is_same_v<T, ChoiceDeclaration>) {
            const GenericResolver::BindingScope generic_scope{generics_, node.type_params};

            for (const auto& variant : node.variants) {
                for (const auto& field : variant.fields) {
                    (void)type_check_helpers::report_unknown_types(
                        *this, field.type,
                        std::format(" for field '{}' in variant '{}' of choice '{}'", field.name,
                                    variant.name, node.name),
                        node.location);
                }
            }
        } else if constexpr (std::is_same_v<T, InterfaceDeclaration>) {
            const GenericResolver::BindingScope generic_scope{generics_, node.type_params};

            for (const auto& field : node.fields) {
                (void)type_check_helpers::report_unknown_types(
                    *this, field.type,
                    std::format(" for field '{}' of interface '{}'", field.name, node.name),
                    node.location);
            }
        } else if constexpr (std::is_same_v<T, TypeAliasDeclaration>) {
            // Push type parameter bindings for generic aliases; references to
            // them are bound type parameters, not unknown types.
            const GenericResolver::BindingScope generic_scope{generics_, node.type_params};

            (void)type_check_helpers::report_unknown_types(
                *this, node.target_type, std::format(" in type alias '{}'", node.name),
                node.location);
        }
        // Include, Use — no checking action in this pass.
    });
}

// ═══════════════════════════════════════════════════════════
// Function checking
// ═══════════════════════════════════════════════════════════

void TypeChecker::check_function(const FunctionDeclaration& func) {
    auto scope = make_scope_guard();

    // Push type parameter bindings for generic functions.
    const GenericResolver::BindingScope generic_scope{generics_, func.type_params};

    // Track whether we are inside @main, where '?' propagation is allowed: a
    // propagated failure there prints the error and exits.
    const auto saved_is_in_main = ctx_.is_in_main;

    ctx_.is_in_main = func.is_main;

    (void)type_check_helpers::report_unknown_types(
        *this, func.return_type, std::format(" for the return type of function '{}'", func.name),
        func.location);

    // Set expected return type.
    ctx_.current_return_type = resolve_type(func.return_type);

    for (const auto& param : func.parameters) {
        (void)type_check_helpers::report_unknown_types(
            *this, param.type,
            std::format(" for parameter '{}' of function '{}'", param.name, func.name),
            func.location);

        const auto param_type = resolve_type(param.type);

        current_scope()->define(param.name, param_type,
                                {.is_mutable = param.is_mutable,
                                 .is_unique = param.type.is_unique,
                                 .is_borrow = param.type.is_borrow});

        // Mark as parameter for distinct unused-parameter warning.
        current_scope()->mark_as_parameter(param.name);

        // Check default value type.
        if (param.default_value) {
            const auto default_type = infer_expression_type(*param.default_value);

            if (!is_assignable(param_type, default_type)) {
                error(std::format("default value type '{}' does not match "
                                  "parameter type '{}'",
                                  default_type.to_string(), param_type.to_string()),
                      param.default_value->location);
            } else {
                type_check_helpers::mark_numeric_widening(*this, *param.default_value, param_type,
                                                          default_type);
            }
        }
    }

    check_statement_list(func.body);

    // Unused parameters are reported by the linter (W0003), not here.

    // A concrete-typed function must not fall off the end without returning —
    // including one whose body is empty.
    if (ctx_.current_return_type && ctx_.current_return_type->kind != TypeInfo::Kind::Void &&
        ctx_.current_return_type->kind != TypeInfo::Kind::StdlibAny &&
        ctx_.current_return_type->kind != TypeInfo::Kind::Unknown &&
        !definitely_returns(func.body)) {
        error(std::format("function '{}' declares return type '{}' but may fall through "
                          "without returning a value — add a return statement on all paths",
                          func.name, ctx_.current_return_type->to_string()),
              func.location, "ensure every code path ends with a return statement");
    }

    ctx_.current_return_type.reset();

    ctx_.is_in_main = saved_is_in_main;
}

} // namespace luma
