#include <format>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/diagnostics/diagnostic_builders.hpp"
#include "analysis/types/expression_type_checker.hpp"
#include "analysis/types/stdlib_type_handler.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checker.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "stdlib/stdlib_catalog.hpp"
#include "symbols/qualified_name.hpp"

namespace luma {

namespace {

// Collect inferred types from a vector of argument expressions.
[[nodiscard]] std::vector<TypeInfo>
collect_arg_types(ExpressionTypeChecker& checker,
                  const std::vector<std::unique_ptr<Expression>>& args) {
    std::vector<TypeInfo> result;
    result.reserve(args.size());
    for (const auto& arg : args) {
        result.push_back(checker.infer_expression_type(*arg));
    }
    return result;
}

} // namespace

TypeInfo ExpressionTypeChecker::visit_call(const CallExpression& expr) {
    // Save the pipe flag — it applies only to THIS call's arity check,
    // not to nested calls inside argument expressions or lambda bodies.
    const bool pipe_applies_here = tc_.context().is_in_pipe;
    tc_.context().is_in_pipe = false;

    auto arg_types = collect_arg_types(*this, expr.arguments);

    // Infer named argument types (used for type checking below).
    std::vector<std::pair<std::string, TypeInfo>> named_arg_types;
    named_arg_types.reserve(expr.named_arguments.size());

    for (const auto& named : expr.named_arguments) {
        named_arg_types.emplace_back(named.name, infer_expression_type(*named.value));
    }

    // Restore the pipe flag for the arity checks below.
    tc_.context().is_in_pipe = pipe_applies_here;

    const auto callee_type = infer_expression_type(*expr.callee);

    // Resolve function name for declaration lookups.
    const auto fn_name = resolve_callee_name(expr, callee_type);

    // Track called function name for unused-function detection.
    if (!fn_name.empty()) {
        tc_.mark_function_called(fn_name);
    }

    // Dispatch to generic call handler for generic functions.
    if (callee_type.kind == TypeInfo::Kind::Func) {
        if (!fn_name.empty()) {
            const auto fn_it = tc_.functions().find(fn_name);

            if (fn_it != tc_.functions().end() && !fn_it->second->type_params.empty()) {
                return tc_.generics().infer_call(*fn_it->second, expr.arguments,
                                                 expr.type_arguments, expr.location);
            }
        }
    }

    // Generic choice variant constructor: infer type params from arguments.
    if (auto choice_type = infer_generic_choice_call(expr, callee_type, arg_types)) {
        return *std::move(choice_type);
    }

    if (callee_type.kind == TypeInfo::Kind::Func) {
        // Check argument count against declared parameters.
        check_call_arity(expr, callee_type, fn_name);

        if (!callee_type.inner_types.empty()) {
            check_user_function_args(expr, callee_type, fn_name, arg_types, named_arg_types);
            check_call_ownership(expr, fn_name);
        }

        if (callee_type.return_type) {
            return *callee_type.return_type;
        }

        return TypeInfo::make(TypeInfo::Kind::Void);
    }

    // Delegate stdlib namespace calls; refines StdlibAny return types using
    // concrete argument types at this call site.
    if (auto stdlib_result = check_stdlib_function_call(expr, arg_types)) {
        return *std::move(stdlib_result);
    }

    // For calls on Unknown or StdlibAny callee, return StdlibAny.
    if (callee_type.kind != TypeInfo::Kind::Unknown &&
        callee_type.kind != TypeInfo::Kind::StdlibAny) {
        tc_.error(std::format("type '{}' is not callable", callee_type.to_string()),
                  expr.callee->location, "only functions and methods are callable",
                  DiagnosticCode::NotCallable);
    }

    return TypeInfo::make(TypeInfo::Kind::StdlibAny);
}

void ExpressionTypeChecker::check_pipe_first_parameter(const Parameter& first_param,
                                                       std::string_view fn_name,
                                                       const TypeInfo& left_type,
                                                       const Expression& piped_value,
                                                       const SourceLocation& loc) {
    const auto param_type = tc_.resolve_type(first_param.type);

    if (left_type.kind != TypeInfo::Kind::StdlibAny &&
        param_type.kind != TypeInfo::Kind::StdlibAny && left_type.kind != TypeInfo::Kind::Unknown &&
        param_type.kind != TypeInfo::Kind::Unknown) {
        (void)type_check_helpers::check_argument_type(tc_, 1, param_type, left_type, loc);
    }

    // Ownership: the piped value flows into the first parameter, so a borrowed
    // variable cannot satisfy a unique parameter, and a borrow parameter
    // borrows out (consumes) the piped variable.
    if (piped_value.kind != ExpressionKind::Variable) {
        return;
    }

    const auto& var = static_cast<const VariableExpression&>(piped_value);

    if (first_param.type.is_unique) {
        const auto* sym = tc_.lookup_variable(var.name);

        if ((sym != nullptr) && sym->is_borrow) {
            tc_.error(std::format("cannot pipe borrowed variable '{}' to "
                                  "unique parameter '{}' of '{}'",
                                  var.name, first_param.name, fn_name),
                      loc,
                      "the function requires ownership — pass the original variable or clone it");
        }
    }

    if (first_param.type.is_borrow) {
        tc_.context().current_scope->mark_consumed(var.name, false);
    }
}

TypeInfo ExpressionTypeChecker::visit_pipe(const PipeExpression& expr) {
    const auto left_type = infer_expression_type(*expr.left);

    // Identify the piped-into callee, whether written as a call (`x |> f()`)
    // or a bare function reference (`x |> f`). Both desugar to `f(x)`, so the
    // first-parameter compatibility check applies to either form.
    const Expression* callee = nullptr;

    if (expr.right->kind == ExpressionKind::Call) {
        callee = static_cast<const CallExpression&>(*expr.right).callee.get();
    } else if (expr.right->kind == ExpressionKind::Variable ||
               expr.right->kind == ExpressionKind::FieldAccess) {
        callee = expr.right.get();
    }

    // If the callee is a known user-defined or namespace function, check that
    // the piped value is compatible with the first parameter.
    if (callee != nullptr) {
        if (callee->kind == ExpressionKind::Variable) {
            const auto& fn_name = static_cast<const VariableExpression&>(*callee).name;
            const auto fn_it = tc_.functions().find(fn_name);

            if (fn_it != tc_.functions().end() && !fn_it->second->parameters.empty()) {
                check_pipe_first_parameter(fn_it->second->parameters[0], fn_name, left_type,
                                           *expr.left, expr.location);
            }
        } else if (callee->kind == ExpressionKind::FieldAccess) {
            // Namespace function call: Module.function()
            const auto& field = static_cast<const FieldAccessExpression&>(*callee);

            if (field.object->kind == ExpressionKind::Variable) {
                const auto& ns_name = static_cast<const VariableExpression&>(*field.object).name;
                const auto ns_it = tc_.namespace_functions().find(ns_name);

                if (ns_it != tc_.namespace_functions().end()) {
                    const auto fn_it = ns_it->second.find(field.field_name);

                    if (fn_it != ns_it->second.end() && !fn_it->second->parameters.empty()) {
                        check_pipe_first_parameter(fn_it->second->parameters[0],
                                                   make_qualified(ns_name, field.field_name),
                                                   left_type, *expr.left, expr.location);
                    }
                }
            }
        }
    }

    // Validate that the right side is a function call or function reference.
    if (expr.right->kind != ExpressionKind::Call && expr.right->kind != ExpressionKind::Variable &&
        expr.right->kind != ExpressionKind::FieldAccess) {
        tc_.error("pipe operator requires a function call on the right side", expr.right->location,
                  "use the syntax: value |> function() or value |> Module.function()");
    }

    // The right side is a call — infer its type (it already includes
    // the left as implicit first argument at runtime).
    const bool saved_in_pipe = tc_.context().is_in_pipe;

    tc_.context().is_in_pipe = true;

    const auto result = infer_expression_type(*expr.right);

    tc_.context().is_in_pipe = saved_in_pipe;

    // A bare function reference (`x |> f`, no parentheses) infers to the
    // function's own type, but the pipe evaluates to `f(x)` — the function's
    // RETURN type. Unwrap it. (`x |> f()` has a Call on the right, which is
    // already inferred as the return type above.)
    if (expr.right->kind != ExpressionKind::Call && result.kind == TypeInfo::Kind::Func) {
        return result.return_type ? *result.return_type : TypeInfo::make(TypeInfo::Kind::Void);
    }

    return result;
}

TypeInfo ExpressionTypeChecker::visit_error_pipe(const ErrorPipeExpression& expr) {
    // The left side must ultimately be a result<T> at runtime. The type
    // checker infers the left side but does not enforce this statically
    // (it may be StdlibAny or result<T>).
    (void)infer_expression_type(*expr.left);

    // Validate that the right side is a function call or function reference.
    if (expr.right->kind != ExpressionKind::Call && expr.right->kind != ExpressionKind::Variable &&
        expr.right->kind != ExpressionKind::FieldAccess) {
        tc_.error("error pipe '!>' requires a function call on the right side",
                  expr.right->location,
                  "use the syntax: value !> function() or value !> Module.function()");
    }

    // Infer the right side. The overall expression returns result<U> where
    // U is the return type of the right-hand function. We always return
    // a Result kind to the caller.
    const bool saved_in_pipe = tc_.context().is_in_pipe;

    tc_.context().is_in_pipe = true;

    auto right_type = infer_expression_type(*expr.right);

    tc_.context().is_in_pipe = saved_in_pipe;

    // A bare function reference (`x !> f`) infers to the function's own type,
    // but the error pipe evaluates to `f(x)` — use the function's RETURN type.
    if (expr.right->kind != ExpressionKind::Call && right_type.kind == TypeInfo::Kind::Func) {
        right_type =
            right_type.return_type ? *right_type.return_type : TypeInfo::make(TypeInfo::Kind::Void);
    }

    // If the right side already returns result<T>, propagate that type.
    if (right_type.kind == TypeInfo::Kind::Result) {
        return right_type;
    }

    // Otherwise wrap in result<T>.
    return TypeInfo::make_result(right_type);
}

// ─── visit_call helpers ─────────────────────────────────────────────────

std::string ExpressionTypeChecker::resolve_callee_name(const CallExpression& expr,
                                                       const TypeInfo& callee_type) const {
    if (callee_type.kind != TypeInfo::Kind::Func) {
        return {};
    }

    if (expr.callee->kind == ExpressionKind::Variable) {
        return static_cast<const VariableExpression&>(*expr.callee).name;
    }

    if (expr.callee->kind == ExpressionKind::FieldAccess) {
        const auto& fa = static_cast<const FieldAccessExpression&>(*expr.callee);

        if (fa.object->kind == ExpressionKind::Variable) {
            return make_qualified(static_cast<const VariableExpression&>(*fa.object).name,
                                  fa.field_name);
        }
    }

    return {};
}

void ExpressionTypeChecker::check_call_arity(const CallExpression& expr,
                                             const TypeInfo& callee_type,
                                             const std::string& fn_name) {
    if (callee_type.inner_types.empty()) {
        return;
    }

    // In a pipe expression, one argument is provided implicitly.
    const auto effective_args =
        expr.arguments.size() + expr.named_arguments.size() + (tc_.context().is_in_pipe ? 1 : 0);

    // Determine minimum required args (parameters without defaults).
    auto min_required = callee_type.inner_types.size();

    const auto fn_it = fn_name.empty() ? tc_.functions().end() : tc_.functions().find(fn_name);

    if (fn_it != tc_.functions().end()) {
        min_required = 0;

        for (const auto& param : fn_it->second->parameters) {
            if (!param.default_value) {
                ++min_required;
            }
        }
    }

    if (effective_args < min_required || effective_args > callee_type.inner_types.size()) {
        if (min_required == callee_type.inner_types.size()) {
            const auto diag =
                diag_builders::arity_mismatch(callee_type.inner_types.size(), effective_args);
            tc_.error(diag.message, expr.location, diag.hint, DiagnosticCode::WrongArgCount);
        } else {
            const auto diag = diag_builders::arity_mismatch_range(
                min_required, callee_type.inner_types.size(), effective_args);
            tc_.error(diag.message, expr.location, diag.hint, DiagnosticCode::WrongArgCount);
        }
    }
}

std::optional<TypeInfo>
ExpressionTypeChecker::infer_generic_choice_call(const CallExpression& expr,
                                                 const TypeInfo& callee_type,
                                                 const std::vector<TypeInfo>& arg_types) {
    if (callee_type.kind != TypeInfo::Kind::Func || callee_type.name.empty()) {
        return std::nullopt;
    }

    const auto ch_it = tc_.choices().find(callee_type.name);

    if (ch_it == tc_.choices().end() || ch_it->second->type_params.empty()) {
        return std::nullopt;
    }

    const auto& choice_decl = *ch_it->second;

    tc_.generics().push_params_as_unknown(choice_decl.type_params);

    // Find the variant being constructed.
    std::string variant_name;

    if (expr.callee->kind == ExpressionKind::FieldAccess) {
        variant_name = static_cast<const FieldAccessExpression&>(*expr.callee).field_name;
    }

    // Infer type params from the call arguments against the
    // variant's declared field types.
    for (const auto& variant : choice_decl.variants) {
        if (variant.name == variant_name) {
            for (std::size_t i{0}; i < variant.fields.size() && i < arg_types.size(); ++i) {
                tc_.generics().infer_param_from_arg(variant.fields[i].type, arg_types[i]);
            }

            break;
        }
    }

    // Build the return type with inferred type args.
    std::vector<TypeInfo> type_args;
    type_args.reserve(choice_decl.type_params.size());

    for (const auto& tp : choice_decl.type_params) {
        const auto tp_it = tc_.generics().bindings().find(tp.name);

        if (tp_it != tc_.generics().bindings().end()) {
            type_args.push_back(tp_it->second);
        } else {
            type_args.push_back(TypeInfo::make(TypeInfo::Kind::Unknown));
        }
    }

    tc_.generics().pop_params(choice_decl.type_params);

    return TypeInfo::make_generic(TypeInfo::Kind::Choice, callee_type.name, std::move(type_args));
}

void ExpressionTypeChecker::check_user_function_args(
    const CallExpression& expr, const TypeInfo& callee_type, const std::string& fn_name,
    const std::vector<TypeInfo>& arg_types,
    const std::vector<std::pair<std::string, TypeInfo>>& named_arg_types) {
    // In a pipe expression the piped value fills parameter 0, so explicit
    // arguments are matched starting at parameter index 1 — mirroring the
    // stdlib and generic-function argument checks (the runtime injects the
    // piped value as the first argument).
    const std::size_t param_offset = tc_.context().is_in_pipe ? 1 : 0;

    // Check positional argument types.
    for (std::size_t i{0};
         i < expr.arguments.size() && i + param_offset < callee_type.inner_types.size(); ++i) {
        const auto& arg_type = arg_types[i];
        const auto& param_type = callee_type.inner_types[i + param_offset];
        const auto hint = type_mismatch_hint(param_type, arg_type);
        (void)type_check_helpers::check_argument_type(tc_, i + 1 + param_offset, param_type,
                                                      arg_type, expr.arguments[i]->location, hint);
    }

    // Check named argument types against declared parameter types.
    const auto fn_it = fn_name.empty() ? tc_.functions().end() : tc_.functions().find(fn_name);

    if (fn_it != tc_.functions().end()) {
        for (const auto& [named_name, named_type] : named_arg_types) {
            for (const auto& param : fn_it->second->parameters) {
                if (param.name == named_name) {
                    const auto param_type = tc_.resolve_type(param.type);

                    if (!tc_.is_assignable(param_type, named_type)) {
                        const auto diag = diag_builders::named_argument_type_mismatch(
                            named_name, param_type, named_type);
                        tc_.error(diag.message, expr.location, "", DiagnosticCode::TypeMismatch);
                    }

                    break;
                }
            }
        }
    }
}

void ExpressionTypeChecker::check_call_ownership(const CallExpression& expr,
                                                 const std::string& fn_name) {
    const auto fn_it = fn_name.empty() ? tc_.functions().end() : tc_.functions().find(fn_name);

    if (fn_it == tc_.functions().end()) {
        return;
    }

    // Explicit arguments map to parameters after the piped value (parameter 0
    // is filled implicitly by the pipe), so shift by the pipe offset.  The
    // piped value's own ownership is checked in check_pipe_first_parameter.
    const std::size_t param_offset = tc_.context().is_in_pipe ? 1 : 0;

    for (std::size_t i{0};
         i < expr.arguments.size() && i + param_offset < fn_it->second->parameters.size(); ++i) {
        const auto& param = fn_it->second->parameters[i + param_offset];
        const auto& arg = *expr.arguments[i];

        // If the parameter is unique, the argument must be a unique
        // (non-borrow) variable so that ownership transfers to the callee.
        if (param.type.is_unique && arg.kind == ExpressionKind::Variable) {
            const auto& var = static_cast<const VariableExpression&>(arg);
            const auto* sym = tc_.lookup_variable(var.name);

            if ((sym != nullptr) && sym->is_borrow) {
                tc_.error(std::format("cannot pass borrowed variable '{}' to "
                                      "unique parameter '{}' — borrow values "
                                      "cannot be consumed",
                                      var.name, param.name),
                          arg.location);
            }
        }

        // If the parameter is borrow, the argument should not be consumed
        // (borrow params are read-only loans).  Un-mark consumption that
        // infer_expression_type() may have triggered on a unique argument
        // passed to a borrow parameter.
        if (param.type.is_borrow && arg.kind == ExpressionKind::Variable) {
            const auto& var = static_cast<const VariableExpression&>(arg);

            auto* sym = tc_.lookup_variable_mut(var.name);

            if ((sym != nullptr) && sym->is_unique) {
                tc_.context().current_scope->mark_consumed(var.name, false);
            }
        }
    }
}

std::optional<TypeInfo>
ExpressionTypeChecker::check_stdlib_function_call(const CallExpression& expr,
                                                  const std::vector<TypeInfo>& arg_types) {
    if (expr.callee->kind != ExpressionKind::FieldAccess) {
        return std::nullopt;
    }

    const auto& fa = static_cast<const FieldAccessExpression&>(*expr.callee);

    if (fa.object->kind != ExpressionKind::Variable) {
        return std::nullopt;
    }

    const auto& ns = static_cast<const VariableExpression&>(*fa.object);

    if (!tc_.is_stdlib_namespace(ns.name)) {
        return std::nullopt;
    }

    const auto full_name = make_qualified(ns.name, fa.field_name);
    const auto* ret_type = tc_.stdlib_handler().get_return_type(full_name);

    // Check stdlib arity.
    const auto* arity_ptr = tc_.stdlib_handler().get_arity(full_name);

    // The arities map already excludes variadic-with-min-0 entries,
    // so a non-null pointer always represents a checkable arity.
    if (arity_ptr != nullptr) {
        const auto effective_args =
            static_cast<int>(expr.arguments.size() + expr.named_arguments.size()) +
            (tc_.context().is_in_pipe ? 1 : 0);

        if (!arity_ptr->is_variadic) {
            if (effective_args != arity_ptr->min_arity) {
                const auto diag = diag_builders::stdlib_arity_mismatch(
                    full_name, arity_ptr->min_arity, effective_args);
                tc_.error(diag.message, expr.location, diag.hint);
            }
        } else {
            if (effective_args < arity_ptr->min_arity) {
                const auto diag = diag_builders::stdlib_arity_mismatch(
                    full_name, arity_ptr->min_arity, effective_args, true);
                tc_.error(diag.message, expr.location, diag.hint);
            }
        }
    }

    // Check stdlib parameter types.
    const auto* param_types_ptr = tc_.stdlib_handler().get_param_types(full_name);

    if (param_types_ptr != nullptr) {
        const auto& param_types = *param_types_ptr;

        for (std::size_t i{0}; i < arg_types.size(); ++i) {
            // When piped, arg_types[0] is param 1 (piped value is param 0).
            const auto param_idx = tc_.context().is_in_pipe ? i + 1 : i;

            if (param_idx >= param_types.size()) {
                break;
            }

            const auto& expected = param_types[param_idx];

            if (expected.kind == TypeInfo::Kind::StdlibAny ||
                expected.kind == TypeInfo::Kind::Func) {
                continue;
            }

            (void)type_check_helpers::check_argument_type(tc_, param_idx + 1, expected,
                                                          arg_types[i], expr.location);
        }
    }

    if (ret_type != nullptr) {
        return tc_.stdlib_handler().refine_return_type(full_name, *ret_type, arg_types);
    }

    tc_.warn(std::format("unknown stdlib function '{}' — return type "
                         "assumed to be 'any'",
                         full_name),
             expr.location);

    return TypeInfo::make(TypeInfo::Kind::StdlibAny);
}

} // namespace luma
