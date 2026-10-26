#include "analysis/types/generic_resolver.hpp"

#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/diagnostics/diagnostic_builders.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "common/scope_guard.hpp"
#include "symbols/qualified_name.hpp"

namespace luma {

namespace {

// The prior binding for `name` — its current value if bound, or nullopt if
// absent — captured before an overwrite so pop_params can restore it exactly.
[[nodiscard]] std::optional<TypeInfo> prior_binding(const StringMap<TypeInfo>& bindings,
                                                    const std::string& name) {
    if (const auto it = bindings.find(name); it != bindings.end()) {
        return it->second;
    }

    return std::nullopt;
}

} // namespace

// ═══════════════════════════════════════════════════════════
// GenericResolver
// ═══════════════════════════════════════════════════════════

GenericResolver::GenericResolver(TypeCheckingServices& services) : services_{&services} {}

void GenericResolver::reset() {
    bindings_.clear();
    saved_bindings_.clear();
    alias_params_.clear();
}

StringMap<TypeInfo>& GenericResolver::bindings() {
    return bindings_;
}

const StringMap<TypeInfo>& GenericResolver::bindings() const {
    return bindings_;
}

StringMap<std::vector<TypeParam>>& GenericResolver::alias_params() {
    return alias_params_;
}

const StringMap<std::vector<TypeParam>>& GenericResolver::alias_params() const {
    return alias_params_;
}

// ═══════════════════════════════════════════════════════════
// Push / Pop type parameter bindings
// ═══════════════════════════════════════════════════════════

void GenericResolver::push_params_as_unknown(const std::vector<TypeParam>& params) {
    for (const auto& param : params) {
        saved_bindings_.emplace_back(param.name, prior_binding(bindings_, param.name));
        bindings_[param.name] = TypeInfo{.kind = TypeInfo::Kind::Unknown, .name = param.name};
    }
}

void GenericResolver::push_params(const std::vector<TypeParam>& params,
                                  const std::vector<TypeAnnotation>& args) {
    for (std::size_t i{0}; i < params.size() && i < args.size(); ++i) {
        saved_bindings_.emplace_back(params[i].name, prior_binding(bindings_, params[i].name));
        bindings_[params[i].name] = services_->resolve_type(args[i]);
    }
}

void GenericResolver::pop_params(const std::vector<TypeParam>& params) {
    // Restore in reverse order to handle duplicates correctly.
    //
    // Design note: saved_bindings_ is a flat push-stack of (name, old_value)
    // pairs rather than a stack of complete binding maps.  This is intentional:
    // only the overwritten entries are saved, so memory use is proportional to
    // the number of type parameters in the current call (typically 1–3) rather
    // than the total number of bindings in the map.  A stack<map<...>> would
    // require copying every binding on every generic call, which is far more
    // expensive when the binding map is large (e.g. after many inferences in a
    // complex expression).
    for (const auto& param : std::views::reverse(params)) {
        if (!saved_bindings_.empty() && saved_bindings_.back().first == param.name) {
            auto& [saved_name, saved_value] = saved_bindings_.back();
            if (saved_value) {
                bindings_[param.name] = *saved_value;
            } else {
                bindings_.erase(param.name);
            }
            saved_bindings_.pop_back();
        } else {
            bindings_.erase(param.name);
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Type parameter inference from arguments
// ═══════════════════════════════════════════════════════════

void GenericResolver::infer_param_from_arg(const TypeAnnotation& param_ann,
                                           const TypeInfo& arg_type) {
    using K = TypeInfo::Kind;

    // Tuple: (T, U) from (integer, string), element by element.
    if (param_ann.kind() == TypeAnnotationKind::Tuple) {
        if (arg_type.kind == K::Tuple) {
            const auto& elements = param_ann.tuple_elements();

            for (std::size_t i{0}; i < elements.size() && i < arg_type.inner_types.size(); ++i) {
                infer_param_from_arg(elements[i], arg_type.inner_types[i]);
            }
        }

        return;
    }

    // Function type: function(T) -> U from a lambda or function value.  An
    // unannotated block-bodied lambda's return type is the permissive
    // StdlibAny, which says nothing about U, so it is not bound.
    if (param_ann.kind() == TypeAnnotationKind::Function) {
        if (arg_type.kind == K::Func) {
            const auto& params = param_ann.function_params();

            for (std::size_t i{0}; i < params.size() && i < arg_type.inner_types.size(); ++i) {
                infer_param_from_arg(params[i], arg_type.inner_types[i]);
            }

            if (const auto* return_ann = param_ann.return_type_ptr();
                return_ann != nullptr && arg_type.return_type &&
                arg_type.return_type->kind != K::StdlibAny) {
                infer_param_from_arg(*return_ann, *arg_type.return_type);
            }
        }

        return;
    }

    // Simple case: param is a bare type parameter name (e.g., T).
    const auto tp_it = bindings_.find(param_ann.name());

    if (tp_it != bindings_.end() && tp_it->second.kind == K::Unknown) {
        bindings_[param_ann.name()] = arg_type;

        return;
    }

    if (param_ann.type_params().empty() || arg_type.inner_types.empty()) {
        return;
    }

    // Nested generic: array<T> from array<integer>, result<T> from result<string>, etc.
    if ((param_ann.name() == "array" && arg_type.kind == K::Array) ||
        (param_ann.name() == "result" && arg_type.kind == K::Result) ||
        (param_ann.name() == "task" && arg_type.kind == K::Task) ||
        (param_ann.name() == "channel" && arg_type.kind == K::Channel) ||
        (param_ann.name() == "optional" && arg_type.kind == K::Optional) ||
        (param_ann.name() == "reference" && arg_type.kind == K::Reference)) {
        infer_param_from_arg(param_ann.type_params()[0], arg_type.inner_types[0]);

        // result<T, E>: also infer the error type parameter.
        if (arg_type.kind == K::Result && param_ann.type_params().size() >= 2 &&
            arg_type.inner_types.size() >= 2) {
            infer_param_from_arg(param_ann.type_params()[1], arg_type.inner_types[1]);
        }

        return;
    }

    if (param_ann.name() == "dictionary" && arg_type.kind == K::Dictionary) {
        const auto& inner_ann = (param_ann.type_params().size() >= 2) ? param_ann.type_params()[1]
                                                                      : param_ann.type_params()[0];
        infer_param_from_arg(inner_ann, arg_type.value_type());

        return;
    }

    // Generic user types — choices (List<T>), records (Box<T>), interfaces —
    // and the record-modelled built-ins (queue<T>, set<T>, ...): match the
    // annotation to the argument's type by name, then pair the type arguments.
    if (arg_type.kind == K::Choice || arg_type.kind == K::Record || arg_type.kind == K::Interface) {
        const auto resolved = services_->resolve_type(param_ann);
        const bool is_same_type =
            param_ann.name() == arg_type.name ||
            (resolved.kind == arg_type.kind && resolved.name == arg_type.name);

        if (!is_same_type) {
            return;
        }

        for (std::size_t i{0};
             i < param_ann.type_params().size() && i < arg_type.inner_types.size(); ++i) {
            infer_param_from_arg(param_ann.type_params()[i], arg_type.inner_types[i]);
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Generic function call inference
// ═══════════════════════════════════════════════════════════

void GenericResolver::infer_from_arguments(const FunctionDeclaration& func,
                                           const CallExpression& expr,
                                           const type_check_helpers::CallArgumentMapping& mapping,
                                           const std::vector<TypeInfo>& arg_types,
                                           const std::vector<NamedArgumentType>& named_arg_types) {
    // Explicit type arguments (turbofish syntax) bind directly; inference
    // below only fills parameters that are still unbound.
    for (std::size_t i{0}; i < func.type_params.size() && i < expr.type_arguments.size(); ++i) {
        bindings_[func.type_params[i].name] = services_->resolve_type(expr.type_arguments[i]);
    }

    // A piped value (`x |> f()`) fills parameter 0, so it takes part in
    // inference like any written argument.
    const auto& context = services_->context();

    if (context.is_in_pipe && context.pipe_left_type && !func.parameters.empty()) {
        infer_param_from_arg(func.parameters[0].type, *context.pipe_left_type);
    }

    for (std::size_t i{0}; i < mapping.positional.size() && i < arg_types.size(); ++i) {
        if (mapping.positional[i]) {
            infer_param_from_arg(func.parameters[*mapping.positional[i]].type, arg_types[i]);
        }
    }

    for (std::size_t k{0}; k < mapping.named.size() && k < named_arg_types.size(); ++k) {
        if (mapping.named[k]) {
            infer_param_from_arg(func.parameters[*mapping.named[k]].type,
                                 named_arg_types[k].second);
        }
    }
}

void GenericResolver::check_argument(const FunctionDeclaration& func, std::string_view fn_name,
                                     std::size_t param_index, const Expression& arg,
                                     const TypeInfo& arg_type, std::string_view arg_label) {
    const auto& param = func.parameters[param_index];
    const auto param_type = services_->resolve_type(param.type);

    if (services_->is_assignable(param_type, arg_type)) {
        type_check_helpers::mark_numeric_widening(*services_, arg, param_type, arg_type);
    } else {
        // List the inferred type parameter bindings for context.
        std::string binding_context;

        for (const auto& tp : func.type_params) {
            const auto tp_it = bindings_.find(tp.name);

            if (tp_it != bindings_.end() && tp_it->second.kind != TypeInfo::Kind::Unknown) {
                if (!binding_context.empty()) {
                    binding_context += ", ";
                }

                binding_context += std::format("{} = {}", tp.name, tp_it->second.to_string());
            }
        }

        std::string hint;

        if (!binding_context.empty()) {
            hint = std::format("type parameters were inferred as: {}", binding_context);
        }

        services_->error(std::format("in call to generic function '{}': {} type mismatch: "
                                     "expected '{}', got '{}'",
                                     func.name, arg_label, param_type.to_string(),
                                     arg_type.to_string()),
                         arg.location, hint, DiagnosticCode::TypeMismatch);
    }

    type_check_helpers::check_argument_ownership(*services_, param, arg, fn_name,
                                                 /*is_piped=*/false);
}

TypeInfo GenericResolver::infer_call(const FunctionDeclaration& func, std::string_view fn_name,
                                     const CallExpression& expr,
                                     const std::vector<TypeInfo>& arg_types,
                                     const std::vector<NamedArgumentType>& named_arg_types) {
    // The same mapping as for non-generic calls: arity, unknown and duplicate
    // named arguments, and missing parameters are reported in one place.
    const auto mapping = type_check_helpers::map_call_arguments(*services_, func, fn_name, expr,
                                                                services_->context().is_in_pipe);

    // Bind type params as unknown initially; the RAII scope pops them on every
    // exit path.  Turbofish arguments and per-argument inference refine the
    // bindings while the scope is active, and the return-type resolution and
    // argument checks below observe those refined bindings.  The argument
    // types were inferred once by the caller and are reused here — inferring
    // an argument again would duplicate its diagnostics and re-consume a
    // unique variable passed to it.
    const BindingScope binding_scope{*this, func.type_params};

    // A namespace function's signature names its namespace's own types by
    // their bare names, so resolve it from inside that namespace.
    auto& context = services_->context();
    const auto saved_namespace = context.current_namespace;

    if (const auto split = symbols::split_module(fn_name)) {
        context.current_namespace = std::string{split->first};
    }

    const ScopeGuard namespace_guard{[&] { context.current_namespace = saved_namespace; }};

    infer_from_arguments(func, expr, mapping, arg_types, named_arg_types);

    // Resolve the return type with the inferred bindings.
    const TypeInfo return_type = services_->resolve_type(func.return_type);

    for (std::size_t i{0}; i < mapping.positional.size() && i < arg_types.size(); ++i) {
        if (mapping.positional[i]) {
            check_argument(func, fn_name, *mapping.positional[i], *expr.arguments[i], arg_types[i],
                           std::format("argument {}", i + 1));
        }
    }

    for (std::size_t k{0}; k < mapping.named.size() && k < named_arg_types.size(); ++k) {
        if (mapping.named[k]) {
            check_argument(func, fn_name, *mapping.named[k], *expr.named_arguments[k].value,
                           named_arg_types[k].second,
                           std::format("named argument '{}'", named_arg_types[k].first));
        }
    }

    // Validate that inferred type arguments satisfy declared bounds.
    validate_bounds(func.type_params, expr.location);

    return return_type;
}

// ═══════════════════════════════════════════════════════════
// Type parameter bound validation
// ═══════════════════════════════════════════════════════════

void GenericResolver::validate_bounds(const std::vector<TypeParam>& params,
                                      const SourceLocation& location) {
    for (const auto& param : params) {
        if (param.bounds.empty()) {
            continue;
        }

        const auto binding_it = bindings_.find(param.name);

        if (binding_it == bindings_.end() || binding_it->second.kind == TypeInfo::Kind::Unknown ||
            binding_it->second.kind == TypeInfo::Kind::StdlibAny) {
            continue; // Unresolved — skip validation.
        }

        const auto& bound_type = binding_it->second;

        for (const auto& bound_name : param.bounds) {
            bool satisfied{false};

            if (bound_type.kind == TypeInfo::Kind::Record) {
                satisfied = services_->satisfies_interface(bound_type.name, bound_name);
            } else if (bound_type.kind == TypeInfo::Kind::Interface) {
                satisfied = services_->satisfies_interface_interface(bound_type.name, bound_name);
            }

            if (!satisfied) {
                services_->error(std::format("type parameter '{}' bound violation: '{}' does "
                                             "not satisfy interface '{}'",
                                             param.name, bound_type.to_string(), bound_name),
                                 location,
                                 std::format("'{}' was inferred as '{}', which must "
                                             "implement all fields required by '{}'",
                                             param.name, bound_type.to_string(), bound_name));
            }
        }
    }
}

} // namespace luma
