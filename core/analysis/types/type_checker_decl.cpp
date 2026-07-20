// ─────────────────────────────────────────────────────────────────────────────
// Declaration Registration and Checking   (TypeChecker partial implementation)
// ─────────────────────────────────────────────────────────────────────────────
// This file implements the two-pass declaration processing:
//
//   Pass 1 — register_declarations(): Walk top-level declarations and populate
//            the symbol registries (records_, choices_, interfaces_, functions_,
//            type_aliases_, namespace_functions_) so that forward references
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
#include "analysis/types/type_checker.hpp"
#include "symbols/qualified_name.hpp"

namespace luma {

// ═══════════════════════════════════════════════════════════
// Registration pass
// ═══════════════════════════════════════════════════════════

void TypeChecker::register_declarations(const std::vector<std::unique_ptr<Declaration>>& decls) {
    for (const auto& decl : decls) {
        register_declaration(*decl);
    }
}

void TypeChecker::register_declaration(const Declaration& decl) {
    dispatch_declaration(decl, [this](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, FunctionDeclaration>) {
            warn_if_duplicate(functions_, node.name, "function", node.location);

            functions_[node.name] = &node;
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
            warn_if_duplicate(records_, node.name, "record", node.location);

            records_[node.name] = &node;
            registry_.register_symbol(node.name, SuggestionCategory::Type);

            // Define the record name in scope as a type placeholder.
            current_scope()->define(node.name,
                                    TypeInfo::make_named(TypeInfo::Kind::Record, node.name), {});
        } else if constexpr (std::is_same_v<T, ChoiceDeclaration>) {
            warn_if_duplicate(choices_, node.name, "choice", node.location);

            choices_[node.name] = &node;
            registry_.register_symbol(node.name, SuggestionCategory::Type);

            current_scope()->define(node.name,
                                    TypeInfo::make_named(TypeInfo::Kind::Choice, node.name), {});
        } else if constexpr (std::is_same_v<T, InterfaceDeclaration>) {
            warn_if_duplicate(interfaces_, node.name, "interface", node.location);

            interfaces_[node.name] = &node;
            registry_.register_symbol(node.name, SuggestionCategory::Type);
        } else if constexpr (std::is_same_v<T, NamespaceDeclaration>) {
            register_namespace(node, "");
        } else if constexpr (std::is_same_v<T, TypeAliasDeclaration>) {
            type_aliases_[node.name] = node.target_type;
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
    std::vector<TypeInfo> param_types;
    param_types.reserve(func.parameters.size());
    std::ranges::transform(func.parameters, std::back_inserter(param_types),
                           [this](const auto& param) { return resolve_type(param.type); });

    current_scope()->define(
        name, TypeInfo::make_func(std::move(param_types), resolve_type(func.return_type)), {});
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

        if (is_qualified_name(bare)) {
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
    const auto split = split_module(path);

    if (!split) {
        // Wildcard: use Namespace — import all members as bare names.
        const auto prefix = path + ".";

        if (const auto ns_it = namespace_functions_.find(path);
            ns_it != namespace_functions_.end()) {
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

        import_all_namespace_types(records_, TypeInfo::Kind::Record, prefix);
        import_all_namespace_types(choices_, TypeInfo::Kind::Choice, prefix);

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
    if (const auto ns_it = namespace_functions_.find(ns); ns_it != namespace_functions_.end()) {
        if (const auto func_it = ns_it->second.find(member); func_it != ns_it->second.end()) {
            define_function_in_scope(*func_it->second, member);
            return;
        }
    }

    if (import_one_namespace_type(records_, TypeInfo::Kind::Record, path, member)) {
        return;
    }

    if (import_one_namespace_type(choices_, TypeInfo::Kind::Choice, path, member)) {
        return;
    }

    error(std::format("cannot resolve import '{}' from namespace '{}'", member, ns),
          use_decl.location,
          "check that the namespace is defined and the name is spelled correctly");
}

void TypeChecker::register_namespace(const NamespaceDeclaration& ns, std::string_view prefix) {
    const auto qualified = make_qualified(prefix, ns.name);

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

            namespace_functions_[ns.name][func.name] = &func;

            if (decl->is_internal_to_namespace) {
                internal_members_.insert(make_qualified(qualified, func.name));
            }
        } else if (decl->kind == DeclarationKind::Namespace) {
            const auto& inner_ns = static_cast<const NamespaceDeclaration&>(*decl);

            register_namespace(inner_ns, qualified);
        } else if (decl->kind == DeclarationKind::Record) {
            const auto& rec = static_cast<const RecordDeclaration&>(*decl);
            const auto qname = make_qualified(qualified, rec.name);
            records_[qname] = &rec;
            register_type_member(qname, decl->is_internal_to_namespace);
        } else if (decl->kind == DeclarationKind::Choice) {
            const auto& ch = static_cast<const ChoiceDeclaration&>(*decl);
            const auto qname = make_qualified(qualified, ch.name);
            choices_[qname] = &ch;
            register_type_member(qname, decl->is_internal_to_namespace);
        } else if (decl->kind == DeclarationKind::Interface) {
            const auto& iface = static_cast<const InterfaceDeclaration&>(*decl);
            const auto qname = make_qualified(qualified, iface.name);
            interfaces_[qname] = &iface;
            register_type_member(qname, decl->is_internal_to_namespace);
        } else if (decl->kind == DeclarationKind::TypeAlias) {
            const auto& alias = static_cast<const TypeAliasDeclaration&>(*decl);
            const auto qname = make_qualified(qualified, alias.name);
            type_aliases_[qname] = alias.target_type;
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

            ctx_.current_namespace = node.name;

            for (const auto& decl : node.declarations) {
                check_declaration(*decl);
            }

            ctx_.current_namespace = saved_namespace;
        } else if constexpr (std::is_same_v<T, RecordDeclaration>) {
            // Push type parameter bindings for generic records.
            const GenericResolver::BindingScope generic_scope{generics_, node.type_params};

            for (const auto& field : node.fields) {
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
                }
            }
        } else if constexpr (std::is_same_v<T, ChoiceDeclaration>) {
            const GenericResolver::BindingScope generic_scope{generics_, node.type_params};

            for (const auto& variant : node.variants) {
                for (const auto& field : variant.fields) {
                    const auto field_type = resolve_type(field.type);

                    if (field_type.kind == TypeInfo::Kind::Unknown &&
                        !generics_.bindings().contains(field.type.name())) {
                        const auto hint = suggest_type_name(field.type.name());
                        error(std::format("unknown type '{}' for field '{}' in "
                                          "variant '{}' of choice '{}'",
                                          field.type.name(), field.name, variant.name, node.name),
                              node.location,
                              hint.empty() ? "check the spelling or make sure the type is declared"
                                           : hint);
                    }
                }
            }
        } else if constexpr (std::is_same_v<T, TypeAliasDeclaration>) {
            // Push type parameter bindings for generic aliases.
            const GenericResolver::BindingScope generic_scope{generics_, node.type_params};

            const auto resolved = resolve_type(node.target_type);

            // Only error on unknown if the alias has no type params
            // (generic aliases reference their type params which resolve to Unknown).
            if (resolved.kind == TypeInfo::Kind::Unknown && node.type_params.empty()) {
                const auto hint = suggest_type_name(node.target_type.name());
                error(std::format("unknown type '{}' in type alias '{}'", node.target_type.name(),
                                  node.name),
                      node.location,
                      hint.empty() ? "check the spelling or make sure the type is declared" : hint);
            }
        }
        // Include, Interface, Use — no checking action in this pass.
    });
}

// ═══════════════════════════════════════════════════════════
// Function checking
// ═══════════════════════════════════════════════════════════

void TypeChecker::check_function(const FunctionDeclaration& func) {
    auto scope = make_scope_guard();

    // Push type parameter bindings for generic functions.
    const GenericResolver::BindingScope generic_scope{generics_, func.type_params};

    // Track whether we are inside @main so that '?' propagation is forbidden.
    const auto saved_is_in_main = ctx_.is_in_main;

    ctx_.is_in_main = func.is_main;

    // Set expected return type.
    ctx_.current_return_type = resolve_type(func.return_type);

    for (const auto& param : func.parameters) {
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
            }
        }
    }

    check_statement_list(func.body);

    // Linter: warn about unused parameters.
    for (const auto& param : func.parameters) {
        if (param.name.starts_with('_')) {
            continue;
        }

        const auto* sym = current_scope()->lookup(param.name);

        if ((sym != nullptr) && !sym->is_read) {
            warn(std::format("unused parameter '{}'", param.name), func.location,
                 "prefix with '_' to suppress this warning", DiagnosticCode::UnusedParameter);
        }
    }

    // Warn when a concrete-typed function might fall off the end without returning.
    if (ctx_.current_return_type && ctx_.current_return_type->kind != TypeInfo::Kind::Void &&
        ctx_.current_return_type->kind != TypeInfo::Kind::StdlibAny &&
        ctx_.current_return_type->kind != TypeInfo::Kind::Unknown && !func.body.empty() &&
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
