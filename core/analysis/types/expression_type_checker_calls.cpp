#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string_view>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/diagnostics/diagnostic_builders.hpp"
#include "analysis/types/expression_type_checker.hpp"
#include "analysis/types/stdlib_type_handler.hpp"
#include "analysis/types/type_check_helpers.hpp"
#include "analysis/types/type_checker.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "common/string_utils.hpp"
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

    if (fn_name == "assert" && tc_.functions().find(fn_name) == tc_.functions().end()) {
        check_assert_args(expr, arg_types);
    }

    const auto* declared =
        callee_type.kind == TypeInfo::Kind::Func ? find_declared_function(fn_name) : nullptr;

    // Dispatch to generic call handler for generic functions.
    if (declared != nullptr && !declared->type_params.empty()) {
        return tc_.generics().infer_call(*declared, fn_name, expr, arg_types, named_arg_types);
    }

    // Generic choice variant constructor: infer type params from arguments.
    if (auto choice_type = infer_generic_choice_call(expr, callee_type, arg_types)) {
        return *std::move(choice_type);
    }

    if (callee_type.kind == TypeInfo::Kind::Func) {
        if (declared != nullptr) {
            check_declared_function_args(expr, *declared, callee_type, fn_name, arg_types,
                                         named_arg_types);
        } else if (!callee_type.inner_types.empty()) {
            // A function value (lambda, parameter, imported name) has no
            // parameter names, so only the argument count and the positional
            // argument types can be checked.
            check_call_arity(expr, callee_type);
            check_positional_args(expr, callee_type, arg_types);
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
    (void)check_piped_value_type(tc_.resolve_type(first_param.type), left_type, piped_value, loc);

    // Ownership: the piped value flows into the first parameter.
    type_check_helpers::check_argument_ownership(tc_, first_param, piped_value, fn_name,
                                                 /*is_piped=*/true);
}

bool ExpressionTypeChecker::check_piped_value_type(const TypeInfo& param_type,
                                                   const TypeInfo& left_type,
                                                   const Expression& piped_value,
                                                   const SourceLocation& loc) {
    const auto is_dynamic = [](const TypeInfo& type) {
        return type.kind == TypeInfo::Kind::StdlibAny || type.kind == TypeInfo::Kind::Unknown;
    };

    if (is_dynamic(left_type) || is_dynamic(param_type)) {
        return true;
    }

    // A result or optional must be unwrapped before it can flow into a
    // parameter that expects the wrapped value; say how.
    std::string hint;

    if (left_type.kind == TypeInfo::Kind::Result && param_type.kind != TypeInfo::Kind::Result) {
        hint = "the piped value is a result — unwrap it first with '?', handle the failure "
               "with '!>', or use 'Result.unwrap_or(value, fallback)'";
    } else if (left_type.kind == TypeInfo::Kind::Optional &&
               param_type.kind != TypeInfo::Kind::Optional) {
        hint = "the piped value is optional — unwrap it first with '??' or "
               "'Optional.unwrap_or(value, fallback)', or 'match' on it";
    }

    if (!type_check_helpers::check_argument_type(tc_, 1, param_type, left_type, loc, hint)) {
        return false;
    }

    type_check_helpers::mark_numeric_widening(tc_, piped_value, param_type, left_type);

    return true;
}

std::optional<TypeInfo> ExpressionTypeChecker::piped_callee_first_param(const Expression& callee) {
    if (callee.kind == ExpressionKind::Variable) {
        // A function value: its type lists the parameter types.
        const auto* symbol =
            tc_.lookup_variable(static_cast<const VariableExpression&>(callee).name);

        if (symbol != nullptr && symbol->function_declaration == nullptr &&
            symbol->type.kind == TypeInfo::Kind::Func && !symbol->type.inner_types.empty()) {
            return symbol->type.inner_types[0];
        }

        return std::nullopt;
    }

    if (callee.kind != ExpressionKind::FieldAccess) {
        return std::nullopt;
    }

    const auto& field = static_cast<const FieldAccessExpression&>(callee);

    if (field.object->kind != ExpressionKind::Variable) {
        return std::nullopt;
    }

    const auto& ns_name = static_cast<const VariableExpression&>(*field.object).name;

    if (!tc_.is_stdlib_namespace(ns_name)) {
        return std::nullopt;
    }

    const auto* param_types =
        tc_.stdlib_handler().get_param_types(symbols::make_qualified(ns_name, field.field_name));

    // Function-typed stdlib parameters are matched loosely, as for explicit
    // arguments in check_stdlib_function_call().
    if (param_types == nullptr || param_types->empty() ||
        param_types->front().kind == TypeInfo::Kind::Func) {
        return std::nullopt;
    }

    return param_types->front();
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
    // the piped value is compatible with the first parameter (and ownership).
    // Otherwise check it against a stdlib function's or a function value's
    // first parameter type.
    if (callee != nullptr) {
        bool is_declared_callee = false;

        if (callee->kind == ExpressionKind::Variable) {
            const auto& fn_name = static_cast<const VariableExpression&>(*callee).name;

            // Only the declared function itself, not a local that shadows it.
            if (const auto* func = find_declared_function(fn_name); func != nullptr) {
                is_declared_callee = true;

                if (!func->parameters.empty()) {
                    check_pipe_first_parameter(func->parameters[0], fn_name, left_type, *expr.left,
                                               expr.location);
                }
            }
        } else if (callee->kind == ExpressionKind::FieldAccess) {
            // Namespace function call: Module.function()
            const auto& field = static_cast<const FieldAccessExpression&>(*callee);

            if (field.object->kind == ExpressionKind::Variable) {
                const auto& ns_name = static_cast<const VariableExpression&>(*field.object).name;
                const auto ns_it = tc_.namespace_functions().find(ns_name);

                if (ns_it != tc_.namespace_functions().end()) {
                    const auto fn_it = ns_it->second.find(field.field_name);

                    if (fn_it != ns_it->second.end()) {
                        is_declared_callee = true;

                        if (!fn_it->second->parameters.empty()) {
                            check_pipe_first_parameter(
                                fn_it->second->parameters[0],
                                symbols::make_qualified(ns_name, field.field_name), left_type,
                                *expr.left, expr.location);
                        }
                    }
                }
            }
        }

        if (!is_declared_callee) {
            if (const auto param_type = piped_callee_first_param(*callee)) {
                (void)check_piped_value_type(*param_type, left_type, *expr.left, expr.location);
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
    auto saved_pipe_left_type = tc_.context().pipe_left_type;

    tc_.context().is_in_pipe = true;
    tc_.context().pipe_left_type = left_type;

    const auto result = infer_expression_type(*expr.right);

    tc_.context().is_in_pipe = saved_in_pipe;
    tc_.context().pipe_left_type = std::move(saved_pipe_left_type);

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
    const auto left_type = infer_expression_type(*expr.left);

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
    auto saved_pipe_left_type = tc_.context().pipe_left_type;

    tc_.context().is_in_pipe = true;
    tc_.context().pipe_left_type =
        left_type.kind == TypeInfo::Kind::Result && !left_type.inner_types.empty()
            ? left_type.result_value_type()
            : TypeInfo::make(TypeInfo::Kind::StdlibAny);

    auto right_type = infer_expression_type(*expr.right);

    tc_.context().is_in_pipe = saved_in_pipe;
    tc_.context().pipe_left_type = std::move(saved_pipe_left_type);

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
            return symbols::make_qualified(static_cast<const VariableExpression&>(*fa.object).name,
                                           fa.field_name);
        }
    }

    return {};
}

const FunctionDeclaration*
ExpressionTypeChecker::find_declared_function(std::string_view fn_name) const {
    if (fn_name.empty()) {
        return nullptr;
    }

    // A bare name is the declared function only while the visible symbol of
    // that name is the function itself: a parameter or local variable that
    // shadows it is a function value, checked by its type alone.
    if (!symbols::is_qualified_name(fn_name)) {
        const auto* func = tc_.find_function(fn_name);
        const auto* symbol = tc_.lookup_variable(fn_name);

        return (func != nullptr && symbol != nullptr && symbol->function_declaration == func)
                   ? func
                   : nullptr;
    }

    if (const auto* func = tc_.find_function(fn_name)) {
        return func;
    }

    // Namespace function called as Namespace.function(...).
    const auto split = symbols::split_module(fn_name);

    if (!split) {
        return nullptr;
    }

    const auto ns_it = tc_.namespace_functions().find(split->first);

    if (ns_it == tc_.namespace_functions().end()) {
        return nullptr;
    }

    const auto fn_it = ns_it->second.find(split->second);

    return fn_it != ns_it->second.end() ? fn_it->second : nullptr;
}

void ExpressionTypeChecker::check_assert_args(const CallExpression& expr,
                                              const std::vector<TypeInfo>& arg_types) {
    const auto is_permissive = [](const TypeInfo& type, TypeInfo::Kind expected) {
        return type.kind == expected || type.kind == TypeInfo::Kind::StdlibAny ||
               type.kind == TypeInfo::Kind::Unknown;
    };

    // assert(condition, message = ...): when piped (`x |> assert("msg")`) the
    // piped value is the condition and the written arguments follow it.
    struct AssertArgument {
        TypeInfo type;
        SourceLocation location;
    };

    std::vector<AssertArgument> effective;

    if (tc_.context().is_in_pipe) {
        effective.push_back({.type = tc_.context().pipe_left_type.value_or(
                                 TypeInfo::make(TypeInfo::Kind::StdlibAny)),
                             .location = expr.location});
    }

    for (std::size_t i{0}; i < arg_types.size() && i < expr.arguments.size(); ++i) {
        effective.push_back({.type = arg_types[i], .location = expr.arguments[i]->location});
    }

    const auto count = effective.size() + expr.named_arguments.size();

    if (count == 0 || count > 2) {
        tc_.error(std::format("assert expects a condition and an optional message, got {} "
                              "argument{}",
                              count, count == 1 ? "" : "s"),
                  expr.location, "call assert(condition) or assert(condition, \"message\")",
                  DiagnosticCode::WrongArgCount);
    }

    // Runtime truthiness would let any non-boolean through — notably every
    // result value, including failure(...) — so a mistyped assertion could
    // never fail. Require a boolean, as for if/while conditions.
    if (!effective.empty() && !is_permissive(effective[0].type, TypeInfo::Kind::Boolean)) {
        tc_.error(std::format("assert condition must be boolean, got '{}'",
                              effective[0].type.to_string()),
                  effective[0].location,
                  "use a comparison (==, !=), or compare a result with success(...)");
    }

    if (effective.size() >= 2 && !is_permissive(effective[1].type, TypeInfo::Kind::String)) {
        tc_.error(
            std::format("assert message must be a string, got '{}'", effective[1].type.to_string()),
            effective[1].location, "pass a string as the assertion message");
    }
}

void ExpressionTypeChecker::check_call_arity(const CallExpression& expr,
                                             const TypeInfo& callee_type) {
    // In a pipe expression, one argument is provided implicitly.  A function
    // value has no default parameters, so every parameter is required.
    const auto effective_args =
        expr.arguments.size() + expr.named_arguments.size() + (tc_.context().is_in_pipe ? 1 : 0);

    if (effective_args != callee_type.inner_types.size()) {
        const auto diag =
            diag_builders::arity_mismatch(callee_type.inner_types.size(), effective_args);
        tc_.error(diag.message, expr.location, diag.hint, DiagnosticCode::WrongArgCount);
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

void ExpressionTypeChecker::check_positional_args(const CallExpression& expr,
                                                  const TypeInfo& callee_type,
                                                  const std::vector<TypeInfo>& arg_types) {
    // In a pipe expression the piped value fills parameter 0, so explicit
    // arguments are matched starting at parameter index 1 (the runtime injects
    // the piped value as the first argument).
    const std::size_t param_offset = tc_.context().is_in_pipe ? 1 : 0;

    for (std::size_t i{0}; i < expr.arguments.size() && i < arg_types.size() &&
                           i + param_offset < callee_type.inner_types.size();
         ++i) {
        const auto& arg_type = arg_types[i];
        const auto& param_type = callee_type.inner_types[i + param_offset];
        const auto hint = type_mismatch_hint(param_type, arg_type);

        // The user-visible argument number counts only the explicitly written
        // arguments, so report `i + 1` regardless of the pipe offset.
        if (type_check_helpers::check_argument_type(tc_, i + 1, param_type, arg_type,
                                                    expr.arguments[i]->location, hint)) {
            type_check_helpers::mark_numeric_widening(tc_, *expr.arguments[i], param_type,
                                                      arg_type);
        }
    }
}

void ExpressionTypeChecker::check_declared_function_args(
    const CallExpression& expr, const FunctionDeclaration& func, const TypeInfo& callee_type,
    std::string_view fn_name, const std::vector<TypeInfo>& arg_types,
    const std::vector<std::pair<std::string, TypeInfo>>& named_arg_types) {
    const auto mapping =
        type_check_helpers::map_call_arguments(tc_, func, fn_name, expr, tc_.context().is_in_pipe);

    // The callee's signature types were resolved when the function was
    // registered; fall back to resolving the declaration for safety.
    const auto param_type_at = [&](std::size_t index) {
        return index < callee_type.inner_types.size()
                   ? callee_type.inner_types[index]
                   : tc_.resolve_type(func.parameters[index].type);
    };

    for (std::size_t i{0}; i < mapping.positional.size() && i < arg_types.size(); ++i) {
        if (!mapping.positional[i]) {
            continue;
        }

        const auto index = *mapping.positional[i];
        const auto param_type = param_type_at(index);
        const auto& arg_type = arg_types[i];
        const auto hint = type_mismatch_hint(param_type, arg_type);

        if (type_check_helpers::check_argument_type(tc_, i + 1, param_type, arg_type,
                                                    expr.arguments[i]->location, hint)) {
            type_check_helpers::mark_numeric_widening(tc_, *expr.arguments[i], param_type,
                                                      arg_type);
        }

        type_check_helpers::check_argument_ownership(tc_, func.parameters[index],
                                                     *expr.arguments[i], fn_name,
                                                     /*is_piped=*/false);
    }

    for (std::size_t k{0}; k < mapping.named.size() && k < named_arg_types.size(); ++k) {
        if (!mapping.named[k]) {
            continue;
        }

        const auto index = *mapping.named[k];
        const auto param_type = param_type_at(index);
        const auto& [named_name, named_type] = named_arg_types[k];
        const auto& value = *expr.named_arguments[k].value;

        if (!tc_.is_assignable(param_type, named_type)) {
            const auto diag =
                diag_builders::named_argument_type_mismatch(named_name, param_type, named_type);
            tc_.error(diag.message, value.location, type_mismatch_hint(param_type, named_type),
                      DiagnosticCode::TypeMismatch);
        } else {
            type_check_helpers::mark_numeric_widening(tc_, value, param_type, named_type);
        }

        type_check_helpers::check_argument_ownership(tc_, func.parameters[index], value, fn_name,
                                                     /*is_piped=*/false);
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

    const auto full_name = symbols::make_qualified(ns.name, fa.field_name);
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

            // `param_idx` indexes the parameter-type array (the piped value
            // fills parameter 0), but the user-visible argument number must
            // count only the explicitly written arguments — so report `i + 1`,
            // matching the generic-function path in generic_resolver.cpp.
            (void)type_check_helpers::check_argument_type(tc_, i + 1, expected, arg_types[i],
                                                          expr.location);
        }
    }

    mark_stdlib_element_widening(full_name, expr, arg_types);

    if (ret_type != nullptr) {
        return tc_.stdlib_handler().refine_return_type(full_name, *ret_type, arg_types);
    }

    tc_.warn(std::format("unknown stdlib function '{}' — return type "
                         "assumed to be 'any'",
                         full_name),
             expr.location);

    return TypeInfo::make(TypeInfo::Kind::StdlibAny);
}

void ExpressionTypeChecker::mark_stdlib_element_widening(std::string_view full_name,
                                                         const CallExpression& expr,
                                                         const std::vector<TypeInfo>& arg_types) {
    // Stdlib functions that store a value (or a whole array) of their first
    // argument's element type.  The catalog types these parameters loosely, so
    // the element relationship is spelled out here.
    struct ElementParameter {
        std::string_view function;
        std::size_t parameter;
        bool is_whole_container;
    };

    static constexpr std::array<ElementParameter, 5> k_element_parameters{{
        {"Array.push", 1, false},
        {"Array.insert_at", 2, false},
        {"Array.set", 2, false},
        {"Array.concat", 1, true},
        {"Dictionary.set", 2, false},
    }};

    const auto it = std::ranges::find(k_element_parameters, full_name, &ElementParameter::function);

    if (it == k_element_parameters.end()) {
        return;
    }

    // When piped, the container is the piped value and explicit arguments
    // start at parameter 1.
    const bool is_piped = tc_.context().is_in_pipe;
    const std::size_t first_explicit_parameter = is_piped ? 1 : 0;

    std::optional<TypeInfo> container_type;

    if (is_piped) {
        container_type = tc_.context().pipe_left_type;
    } else if (!arg_types.empty()) {
        container_type = arg_types[0];
    }

    if (!container_type ||
        (container_type->kind != TypeInfo::Kind::Array &&
         container_type->kind != TypeInfo::Kind::Dictionary) ||
        container_type->inner_types.empty() || it->parameter < first_explicit_parameter) {
        return;
    }

    const auto argument_index = it->parameter - first_explicit_parameter;

    if (argument_index >= expr.arguments.size() || argument_index >= arg_types.size()) {
        return;
    }

    const auto& target = it->is_whole_container ? *container_type : container_type->element_type();
    type_check_helpers::mark_numeric_widening(tc_, *expr.arguments[argument_index], target,
                                              arg_types[argument_index]);
}

// ═══════════════════════════════════════════════════════════
// Shared call-argument helpers (type_check_helpers)
// ═══════════════════════════════════════════════════════════

namespace type_check_helpers {

CallArgumentMapping map_call_arguments(TypeCheckingServices& tc, const FunctionDeclaration& func,
                                       std::string_view fn_name, const CallExpression& expr,
                                       bool is_piped) {
    const auto& params = func.parameters;
    const std::size_t param_offset = is_piped ? 1 : 0;

    std::vector<bool> is_filled(params.size(), false);

    if (is_piped && !params.empty()) {
        is_filled[0] = true;
    }

    CallArgumentMapping mapping;
    mapping.positional.reserve(expr.arguments.size());
    mapping.named.reserve(expr.named_arguments.size());

    for (std::size_t i{0}; i < expr.arguments.size(); ++i) {
        const auto index = i + param_offset;

        if (index < params.size()) {
            is_filled[index] = true;
            mapping.positional.emplace_back(index);
        } else {
            mapping.positional.emplace_back(std::nullopt);
        }
    }

    bool are_named_valid = true;

    for (const auto& named : expr.named_arguments) {
        const auto param_it = std::ranges::find(params, named.name, &Parameter::name);

        if (param_it == params.end()) {
            std::vector<std::string_view> param_names;
            param_names.reserve(params.size());

            for (const auto& param : params) {
                param_names.emplace_back(param.name);
            }

            const auto suggestion = suggest_name(param_names, named.name);
            tc.error(std::format("'{}' has no parameter named '{}'", fn_name, named.name),
                     named.value->location,
                     suggestion.empty() ? "check the function signature for its parameter names"
                                        : suggestion,
                     DiagnosticCode::WrongArgCount);
            mapping.named.emplace_back(std::nullopt);
            are_named_valid = false;
            continue;
        }

        const auto index = static_cast<std::size_t>(param_it - params.begin());

        if (is_filled[index]) {
            tc.error(
                std::format("parameter '{}' of '{}' is given more than once", named.name, fn_name),
                named.value->location, "pass each parameter once — either by position or by name",
                DiagnosticCode::WrongArgCount);
            mapping.named.emplace_back(std::nullopt);
            are_named_valid = false;
            continue;
        }

        is_filled[index] = true;
        mapping.named.emplace_back(index);
    }

    const auto effective_args = expr.arguments.size() + expr.named_arguments.size() + param_offset;
    const auto min_required = static_cast<std::size_t>(std::ranges::count_if(
        params, [](const Parameter& param) { return param.default_value == nullptr; }));

    if (effective_args < min_required || effective_args > params.size()) {
        const auto diag =
            min_required == params.size()
                ? diag_builders::arity_mismatch(params.size(), effective_args)
                : diag_builders::arity_mismatch_range(min_required, params.size(), effective_args);
        tc.error(diag.message, expr.location, diag.hint, DiagnosticCode::WrongArgCount);

        return mapping;
    }

    // The count fits, but named arguments may have skipped a required
    // parameter (e.g. f(1, c: 3) for f(a, b, c)).  Unknown or duplicate names
    // were reported above and would only repeat here.
    if (are_named_valid) {
        for (std::size_t i{0}; i < params.size(); ++i) {
            if (!is_filled[i] && !params[i].default_value) {
                tc.error(std::format("missing argument for parameter '{}' of '{}'", params[i].name,
                                     fn_name),
                         expr.location, "pass a value for every parameter without a default",
                         DiagnosticCode::WrongArgCount);
            }
        }
    }

    return mapping;
}

void check_argument_ownership(TypeCheckingServices& tc, const Parameter& param,
                              const Expression& arg, std::string_view fn_name, bool is_piped) {
    if (arg.kind != ExpressionKind::Variable) {
        return;
    }

    const auto& var = static_cast<const VariableExpression&>(arg);

    // A unique parameter takes ownership, which a borrowed variable cannot give.
    if (param.type.is_unique) {
        const auto* sym = tc.lookup_variable(var.name);

        if ((sym != nullptr) && sym->is_borrow) {
            if (is_piped) {
                tc.error(std::format("cannot pipe borrowed variable '{}' to unique parameter "
                                     "'{}' of '{}'",
                                     var.name, param.name, fn_name),
                         arg.location,
                         "the function requires ownership — pass the original variable or clone "
                         "it");
            } else {
                tc.error(std::format("cannot pass borrowed variable '{}' to unique parameter "
                                     "'{}' — borrow values cannot be consumed",
                                     var.name, param.name),
                         arg.location,
                         "the function requires ownership — pass the original variable or clone "
                         "it");
            }
        }
    }

    // A borrow parameter is a read-only loan: undo the consumption that
    // inferring the argument recorded on a unique variable.
    if (param.type.is_borrow) {
        tc.context().current_scope->mark_consumed(var.name, false);
    }
}

} // namespace type_check_helpers

} // namespace luma
