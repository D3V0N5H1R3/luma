#include "analysis/types/generic_resolver.hpp"

#include <format>
#include <optional>
#include <ranges>
#include <string>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/diagnostics/diagnostic_builders.hpp"
#include "analysis/types/type_checking_context.hpp"

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
    // Simple case: param is a bare type parameter name (e.g., T).
    const auto tp_it = bindings_.find(param_ann.name());

    if (tp_it != bindings_.end() && tp_it->second.kind == TypeInfo::Kind::Unknown) {
        bindings_[param_ann.name()] = arg_type;

        return;
    }

    // Nested generic: array<T> from array<integer>, result<T> from result<string>, etc.
    if (!param_ann.type_params().empty() && !arg_type.inner_types.empty()) {
        if ((param_ann.name() == "array" && arg_type.kind == TypeInfo::Kind::Array) ||
            (param_ann.name() == "result" && arg_type.kind == TypeInfo::Kind::Result) ||
            (param_ann.name() == "task" && arg_type.kind == TypeInfo::Kind::Task) ||
            (param_ann.name() == "channel" && arg_type.kind == TypeInfo::Kind::Channel) ||
            (param_ann.name() == "optional" && arg_type.kind == TypeInfo::Kind::Optional) ||
            (param_ann.name() == "reference" && arg_type.kind == TypeInfo::Kind::Reference)) {
            infer_param_from_arg(param_ann.type_params()[0], arg_type.inner_types[0]);
        }

        if (param_ann.name() == "dictionary" && arg_type.kind == TypeInfo::Kind::Dictionary) {
            const auto& inner_ann = (param_ann.type_params().size() >= 2)
                                        ? param_ann.type_params()[1]
                                        : param_ann.type_params()[0];
            infer_param_from_arg(inner_ann, arg_type.value_type());
        }

        // Generic choice types: List<T> from List<integer>, Tree<T> from Tree<string>, etc.
        if (arg_type.kind == TypeInfo::Kind::Choice &&
            services_->choices().contains(param_ann.name()) && param_ann.name() == arg_type.name) {
            for (std::size_t i{0};
                 i < param_ann.type_params().size() && i < arg_type.inner_types.size(); ++i) {
                infer_param_from_arg(param_ann.type_params()[i], arg_type.inner_types[i]);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Generic function call inference
// ═══════════════════════════════════════════════════════════

void GenericResolver::check_call_arity(const FunctionDeclaration& func, std::size_t arg_count,
                                       const SourceLocation& call_location) {
    const auto effective_args = arg_count + (services_->context().is_in_pipe ? 1 : 0);

    std::size_t min_required{0};

    for (const auto& param : func.parameters) {
        if (!param.default_value) {
            ++min_required;
        }
    }

    if (effective_args < min_required || effective_args > func.parameters.size()) {
        if (min_required == func.parameters.size()) {
            const auto diag = diag_builders::arity_mismatch(func.parameters.size(), effective_args);
            services_->error(diag.message, call_location, diag.hint);
        } else {
            const auto diag = diag_builders::arity_mismatch_range(
                min_required, func.parameters.size(), effective_args);
            services_->error(diag.message, call_location, diag.hint);
        }
    }
}

std::vector<TypeInfo>
GenericResolver::bind_or_infer_params(const FunctionDeclaration& func,
                                      const std::vector<std::unique_ptr<Expression>>& args,
                                      const std::vector<TypeAnnotation>& explicit_type_args) {
    // If explicit type arguments are provided (turbofish syntax),
    // bind them directly instead of inferring.
    if (!explicit_type_args.empty()) {
        for (std::size_t i{0}; i < func.type_params.size() && i < explicit_type_args.size(); ++i) {
            bindings_[func.type_params[i].name] = services_->resolve_type(explicit_type_args[i]);
        }
    }

    // Infer bindings from each argument and cache arg types to avoid
    // re-inferring (which can cause duplicate side effects).
    std::vector<TypeInfo> cached_arg_types;
    cached_arg_types.reserve(args.size());

    // In a pipe expression, the first parameter receives the piped value
    // implicitly, so explicit args start at parameter index 1.
    const std::size_t param_offset = services_->context().is_in_pipe ? 1 : 0;

    for (std::size_t i{0}; i + param_offset < func.parameters.size() && i < args.size(); ++i) {
        const auto arg_type = services_->infer_expression_type(*args[i]);
        cached_arg_types.push_back(arg_type);

        infer_param_from_arg(func.parameters[i + param_offset].type, arg_type);

        // Check ownership compatibility at call site.
        const auto& param = func.parameters[i + param_offset];
        const auto& arg = *args[i];

        if (param.type.is_unique && arg.kind == ExpressionKind::Variable) {
            const auto& var = static_cast<const VariableExpression&>(arg);
            const auto* sym = services_->lookup_variable(var.name);

            if ((sym != nullptr) && sym->is_borrow) {
                services_->error(std::format("cannot pass borrowed variable '{}' to "
                                             "unique parameter '{}' — borrow values "
                                             "cannot be consumed",
                                             var.name, param.name),
                                 arg.location);
            }
        }

        if (param.type.is_borrow && arg.kind == ExpressionKind::Variable) {
            const auto& var = static_cast<const VariableExpression&>(arg);
            services_->context().current_scope->mark_consumed(var.name, false);
        }
    }

    return cached_arg_types;
}

void GenericResolver::report_argument_mismatches(
    const FunctionDeclaration& func, const std::vector<std::unique_ptr<Expression>>& args,
    const std::vector<TypeInfo>& cached_arg_types) {
    const std::size_t param_offset = services_->context().is_in_pipe ? 1 : 0;

    for (std::size_t i{0}; i + param_offset < func.parameters.size() && i < args.size(); ++i) {
        const auto& arg_type = cached_arg_types[i];
        const auto param_type = services_->resolve_type(func.parameters[i + param_offset].type);

        if (!services_->is_assignable(param_type, arg_type)) {
            // Build a diagnostic that shows which type parameters were
            // inferred and from where.
            const std::string detail =
                std::format("in call to generic function '{}': argument {} type "
                            "mismatch: expected '{}', got '{}'",
                            func.name, i + 1, param_type.to_string(), arg_type.to_string());

            // List type parameter bindings for context.
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

            services_->error(detail, args[i]->location, hint);
        }
    }
}

TypeInfo GenericResolver::infer_call(const FunctionDeclaration& func,
                                     const std::vector<std::unique_ptr<Expression>>& args,
                                     const std::vector<TypeAnnotation>& explicit_type_args,
                                     const SourceLocation& call_location) {
    check_call_arity(func, args.size(), call_location);

    // Bind type params as unknown initially; the RAII scope pops them on every
    // exit path.  Turbofish arguments and per-argument inference refine the
    // bindings while the scope is active, and the return-type resolution and
    // mismatch reporting below observe those refined bindings.
    const BindingScope binding_scope{*this, func.type_params};

    const auto cached_arg_types = bind_or_infer_params(func, args, explicit_type_args);

    // Resolve the return type with the inferred bindings.
    const TypeInfo return_type = services_->resolve_type(func.return_type);

    report_argument_mismatches(func, args, cached_arg_types);

    // Validate that inferred type arguments satisfy declared bounds.
    validate_bounds(func.type_params, call_location);

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
