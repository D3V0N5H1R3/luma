#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Type Check Helpers — Shared Validation Utilities
// ─────────────────────────────────────────────────────────────────────────────
// Reusable validation helpers that eliminate repeated boilerplate across
// ExpressionTypeChecker, StatementTypeChecker, and their sub-files.
//
// Each helper:
//   - Takes TypeCheckingServices& for error emission and type queries.
//   - Emits exactly one diagnostic on failure (callers do not need to check).
//   - Returns bool or std::optional<TypeInfo> so callers can early-out.
//   - Silently accepts StdlibAny and Unknown (dynamic types) without errors.
//
// Usage:
//   #include "analysis/types/type_check_helpers.hpp"
//
//   if (!type_check_helpers::require_numeric_operand(tc_, t, "'++'", loc)) {
//       return;  // error already emitted
//   }
//
//   if (!type_check_helpers::require_boolean_operand(tc_, t, "if condition", loc)) {
//       // error already emitted
//   }
//
//   if (!type_check_helpers::check_argument_type(tc_, 1, expected, actual, loc, hint)) {
//       // error already emitted; continue to check remaining args
//   }
//
//   if (auto inner = type_check_helpers::unwrap_optional_or_error(tc_, t, loc)) {
//       // use *inner — it's the TypeInfo inside optional<T>
//   }
//
//   const auto t = type_check_helpers::infer_and_require_kind(
//       tc_, *node.operand, TypeInfo::Kind::Task, node.location,
//       "await requires a task value", "use Task.run() or spawn");
//   // t is the inferred type; error already emitted if kind didn't match
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_builders.hpp"
#include "analysis/source/source_location.hpp"
#include "analysis/types/generic_resolver.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "analysis/types/type_info.hpp"

// Reusable inline validation helpers shared by ExpressionTypeChecker,
// StatementTypeChecker, and their sub-translation-units.  See the file-level
// comment block above for the full contract and usage examples.
namespace luma::type_check_helpers {

// ── require_numeric_operand ──────────────────────────────────────────────────
// Returns true when `type` is numeric (integer or number), StdlibAny, or
// Unknown.  Emits a DiagnosticCode::InvalidOperand error and returns false for
// every other concrete type.
//
// `context` names the operator or construct for the message, e.g. "'++'".
// Call sites that already guard on StdlibAny/Unknown do not need to change:
// the helper handles those cases silently regardless.

[[nodiscard]] inline bool require_numeric_operand(TypeCheckingServices& tc, const TypeInfo& type,
                                                  std::string_view context,
                                                  const SourceLocation& loc) {
    if (type.is_numeric() || type.kind == TypeInfo::Kind::StdlibAny ||
        type.kind == TypeInfo::Kind::Unknown) {
        return true;
    }

    tc.error(std::format("{}: expected numeric type (integer or number), got '{}'", context,
                         type.to_string()),
             loc, "convert the value using Converter.to_number() or Converter.to_integer()",
             DiagnosticCode::InvalidOperand);

    return false;
}

// ── check_argument_type ──────────────────────────────────────────────────────
// Returns true when `actual` is assignable to `expected`.  Otherwise emits an
// argument type mismatch diagnostic (DiagnosticCode::TypeMismatch) and returns
// false.
//
// `arg_index` is 1-based — it appears in the diagnostic as "argument 1",
// "argument 2", etc.  Pass `hint` if the caller already has a conversion hint
// (e.g. from ExpressionTypeChecker::type_mismatch_hint()); leave it empty to
// let diag_builders produce the default hint.

[[nodiscard]] inline bool check_argument_type(TypeCheckingServices& tc, std::size_t arg_index,
                                              const TypeInfo& expected, const TypeInfo& actual,
                                              const SourceLocation& loc,
                                              const std::string& hint = {}) {
    if (tc.is_assignable(expected, actual)) {
        return true;
    }

    const auto diag = diag_builders::argument_type_mismatch(arg_index, expected, actual, hint);
    tc.error(diag.message, loc, diag.hint, DiagnosticCode::TypeMismatch);

    return false;
}

// ── unwrap_optional_or_error ─────────────────────────────────────────────────
// Extracts the inner TypeInfo from an optional<T>:
//   - optional<T>       → T             (returns it)
//   - optional<>        → StdlibAny     (degenerate; no error)
//   - StdlibAny/Unknown → StdlibAny     (dynamic type; no error)
//   - anything else     → nullopt        (emits type error)
//
// The "or error" path fires only for concrete non-optional types, e.g. when a
// value that was expected to be optional<T> is actually string or integer.

[[nodiscard]] inline std::optional<TypeInfo> unwrap_optional_or_error(TypeCheckingServices& tc,
                                                                      const TypeInfo& type,
                                                                      const SourceLocation& loc) {
    if (type.kind == TypeInfo::Kind::Optional) {
        if (!type.inner_types.empty()) {
            return type.element_type();
        }
        return TypeInfo::make(TypeInfo::Kind::StdlibAny);
    }

    if (type.kind == TypeInfo::Kind::StdlibAny || type.kind == TypeInfo::Kind::Unknown) {
        return TypeInfo::make(TypeInfo::Kind::StdlibAny);
    }

    tc.error(std::format("expected 'optional<T>', got '{}'", type.to_string()), loc,
             "wrap the value with 'some(value)' or check the type annotation");

    return std::nullopt;
}

// ── check_type_assignable ────────────────────────────────────────────────────
// Returns true when actual is assignable to expected.  Otherwise emits a
// TypeMismatch diagnostic and returns false.
//
// `context` names the construct for the diagnostic message, e.g.
// "cannot assign to variable" or "return type mismatch".
// The conversion hint is resolved automatically via
// diag_builders::type_mismatch_hint.

[[nodiscard]] inline bool check_type_assignable(TypeCheckingServices& tc, const TypeInfo& expected,
                                                const TypeInfo& actual, std::string_view context,
                                                const SourceLocation& loc) {
    if (tc.is_assignable(expected, actual)) {
        return true;
    }

    const auto hint = diag_builders::type_mismatch_hint(expected, actual);
    const auto diag = diag_builders::type_mismatch(expected, actual, context);
    tc.error(diag.message, loc, hint, DiagnosticCode::TypeMismatch);

    return false;
}

// ── check_iterable_type ──────────────────────────────────────────────────────
// Returns true when type is a valid for-loop iterable: array<T>, range,
// string, dictionary<V>, StdlibAny, or Unknown.
// Emits an error and returns false for any other concrete type.

[[nodiscard]] inline bool check_iterable_type(TypeCheckingServices& tc, const TypeInfo& type,
                                              const SourceLocation& loc) {
    if (type.kind == TypeInfo::Kind::Array || type.kind == TypeInfo::Kind::Range ||
        type.kind == TypeInfo::Kind::String || type.kind == TypeInfo::Kind::Dictionary ||
        type.kind == TypeInfo::Kind::StdlibAny || type.kind == TypeInfo::Kind::Unknown) {
        return true;
    }

    tc.error(std::format("for loop requires an iterable (array, range, string, "
                         "or dictionary), got '{}'",
                         type.to_string()),
             loc, "use an array, dictionary, string, or range as the loop target");

    return false;
}

// ── require_boolean_operand ──────────────────────────────────────────────────
// Returns true when `type` is boolean, StdlibAny, or Unknown.
// Emits a DiagnosticCode::InvalidOperand error and returns false for every
// other concrete type.
//
// `context` names the operator or construct for the message, e.g.
// "if condition" or "logical operator".  Call sites that already guard on
// StdlibAny/Unknown do not need to change: the helper handles those cases
// silently regardless.

[[nodiscard]] inline bool require_boolean_operand(TypeCheckingServices& tc, const TypeInfo& type,
                                                  std::string_view context,
                                                  const SourceLocation& loc) {
    if (type.kind == TypeInfo::Kind::Boolean || type.kind == TypeInfo::Kind::StdlibAny ||
        type.kind == TypeInfo::Kind::Unknown) {
        return true;
    }

    tc.error(std::format("{}: expected boolean, got '{}'", context, type.to_string()), loc,
             "use a comparison operator (==, !=, <, >) to produce a boolean value",
             DiagnosticCode::InvalidOperand);

    return false;
}

// ── infer_and_require_kind ───────────────────────────────────────────────────
// Infers the type of `expr` via tc.infer_expression_type(), checks that the resulting kind is
// `expected_kind` (or StdlibAny / Unknown, which are accepted as dynamic
// types), and emits an error when the kind does not match.
// Always returns the inferred type so callers can chain further checks or
// propagate it as an error-recovery type.
//
// `msg_prefix` is prepended to ", got '<type>'" to form the error message.
// For example, with msg_prefix = "await requires a task value", the message
// becomes "await requires a task value, got 'string'".
//
// `loc` should be the location of the outer construct that imposes the
// requirement (e.g. the await expression), not the child sub-expression, so
// that the diagnostic points at the right source position.
//
// Usage:
//   const auto t = type_check_helpers::infer_and_require_kind(
//       tc_, *node.operand, TypeInfo::Kind::Task, node.location,
//       "await requires a task value",
//       "wrap the expression in a task using Task.run() or spawn");
//   if (t.kind == TypeInfo::Kind::Task && !t.inner_types.empty()) { ... }

[[nodiscard]] inline TypeInfo
infer_and_require_kind(TypeCheckingServices& tc, const Expression& expr,
                       TypeInfo::Kind expected_kind, const SourceLocation& loc,
                       std::string_view msg_prefix, std::string_view hint = {},
                       DiagnosticCode code = DiagnosticCode::InvalidOperand) {
    const auto type = tc.infer_expression_type(expr);

    if (type.kind == expected_kind || type.kind == TypeInfo::Kind::StdlibAny ||
        type.kind == TypeInfo::Kind::Unknown) {
        return type;
    }

    tc.error(std::format("{}, got '{}'", msg_prefix, type.to_string()), loc, hint, code);

    return type;
}

// ── bind_type_params ─────────────────────────────────────────────────────────
// Returns an RAII guard that binds each of `params` (a range of TypeParam) to
// the corresponding entry in `args` within the active generic-binding map, and
// restores the prior bindings on destruction.  Binds min(params, args) pairs,
// so a non-generic use (empty `args`) is a harmless no-op.
//
// Keep the returned guard alive for the region where the bindings must hold:
// field and variant types resolved via tc.resolve_type() while it is in scope
// see the concrete type arguments.  Templated on the parameter range so this
// header need not depend on the AST declaration definitions.
template <typename ParamRange>
[[nodiscard]] GenericResolver::ParamGuard bind_type_params(TypeCheckingServices& tc,
                                                           const ParamRange& params,
                                                           const std::vector<TypeInfo>& args) {
    std::vector<std::string> names;
    names.reserve(params.size());
    for (const auto& param : params) {
        names.push_back(param.name);
    }

    return GenericResolver::ParamGuard{tc.generics().bindings(), names, args};
}

} // namespace luma::type_check_helpers
