#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostic Builders — Factory Functions for Common Error Patterns
// ─────────────────────────────────────────────────────────────────────────────
// Provides concise factory functions for the most frequently emitted
// diagnostic patterns across the type checker and parser.  Each builder
// returns a DiagnosticText — the message and hint for the diagnostic —
// keeping call sites short and consistent.  The caller chooses the
// DiagnosticCode when it emits the diagnostic.
//
// These helpers produce the *message string* and *hint* for a diagnostic;
// the caller still emits via TypeCheckingServices::error() or the
// DiagnosticEmitter API.  This keeps the builders decoupled from any
// particular emission mechanism.
//
// ─── When to use these builders ─────────────────────────────────────────
//
// Use a diag_builders:: function when the error matches a recurring pattern
// (type mismatch, arity mismatch, undefined symbol, field mismatch).  The
// centralised wording ensures consistency across the codebase and makes
// message changes easy to audit.
//
// Fall back to inline std::format() for unique, one-off messages that do
// not fit any existing builder.  If the same custom message appears in
// three or more call sites, consider adding a new builder here.
//
// ─── Contrast with other diagnostic mechanisms ──────────────────────────
//
// - diag::error().category(...).build() — rich builder in diagnostic_builder.hpp
//   for diagnostics that need multiple source spans, secondary labels, or
//   suggested fixes.  Builders here produce only (message, hint) pairs.
//
// - compiler_errors::ErrorMessage — similar pattern but scoped to the
//   compiler subsystem (compiler_errors.hpp).  These builders serve the
//   type checker and parser.
//
// Usage:
//   #include "analysis/diagnostics/diagnostic_builders.hpp"
//
//   auto [msg, hint] = diag_builders::type_mismatch(expected, actual, "in assignment");
//   tc_.error(msg, loc, hint, DiagnosticCode::TypeMismatch);
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/types/type_info.hpp"

namespace luma::diag_builders {

// DiagnosticText is defined in diagnostic.hpp (shared with compiler_errors).

// ─── Type Mismatch ──────────────────────────────────────────────────────────
// Produces: "type mismatch: expected '<expected>', got '<actual>'"
// or with context: "<context>: expected '<expected>', got '<actual>'"

[[nodiscard]] inline DiagnosticText type_mismatch(const TypeInfo& expected, const TypeInfo& actual,
                                                  std::string_view context = "") {
    std::string message;

    if (context.empty()) {
        message = std::format("type mismatch: expected '{}', got '{}'", expected.to_string(),
                              actual.to_string());
    } else {
        message = std::format("{}: expected '{}', got '{}'", context, expected.to_string(),
                              actual.to_string());
    }

    return {std::move(message), {}};
}

// ─── Argument Type Mismatch ─────────────────────────────────────────────────
// Produces: "argument <n> type mismatch: expected '<expected>', got '<actual>'"

[[nodiscard]] inline DiagnosticText argument_type_mismatch(std::size_t arg_index,
                                                           const TypeInfo& expected,
                                                           const TypeInfo& actual,
                                                           std::string_view hint = "") {
    return {std::format("argument {} type mismatch: expected '{}', got '{}'", arg_index,
                        expected.to_string(), actual.to_string()),
            std::string{hint}};
}

// ─── Named Argument Type Mismatch ───────────────────────────────────────────
// Produces: "named argument '<name>' type mismatch: expected '<expected>', got '<actual>'"

[[nodiscard]] inline DiagnosticText named_argument_type_mismatch(std::string_view name,
                                                                 const TypeInfo& expected,
                                                                 const TypeInfo& actual) {
    return {std::format("named argument '{}' type mismatch: expected '{}', got '{}'", name,
                        expected.to_string(), actual.to_string()),
            {}};
}

// ─── Field Type Mismatch ────────────────────────────────────────────────────
// Produces: "field '<name>': expected '<expected>', got '<actual>'"

[[nodiscard]] inline DiagnosticText field_type_mismatch(std::string_view field_name,
                                                        const TypeInfo& expected,
                                                        const TypeInfo& actual,
                                                        std::string_view hint = "") {
    return {std::format("field '{}': expected '{}', got '{}'", field_name, expected.to_string(),
                        actual.to_string()),
            std::string{hint}};
}

// ─── Undefined Symbol ───────────────────────────────────────────────────────
// Produces: "undefined <kind> '<name>'"

[[nodiscard]] inline DiagnosticText undefined_symbol(std::string_view kind, std::string_view name,
                                                     std::string_view hint = "") {
    return {std::format("undefined {} '{}'", kind, name), std::string{hint}};
}

// ─── Arity Mismatch ─────────────────────────────────────────────────────────
// Produces: "wrong number of arguments: expected <n>, got <m>"

[[nodiscard]] inline DiagnosticText arity_mismatch(
    std::size_t expected, std::size_t actual,
    std::string_view hint = "check the function signature for the expected number of parameters") {
    return {std::format("wrong number of arguments: expected {}, got {}", expected, actual),
            std::string{hint}};
}

// Variant with a range: "wrong number of arguments: expected <min>-<max>, got <m>"
[[nodiscard]] inline DiagnosticText arity_mismatch_range(
    std::size_t min_expected, std::size_t max_expected, std::size_t actual,
    std::string_view hint = "check the function signature — some parameters have default values") {
    return {std::format("wrong number of arguments: expected {}-{}, got {}", min_expected,
                        max_expected, actual),
            std::string{hint}};
}

// ─── Stdlib Arity Mismatch ──────────────────────────────────────────────────
// Produces: "'<name>' expects <n> argument(s), got <m>"

[[nodiscard]] inline DiagnosticText stdlib_arity_mismatch(std::string_view function_name,
                                                          int expected, int actual,
                                                          bool is_variadic = false) {
    if (is_variadic) {
        return {std::format("'{}' expects at least {} argument{}, got {}", function_name, expected,
                            expected == 1 ? "" : "s", actual),
                "check the function signature for required parameters"};
    }

    return {std::format("'{}' expects {} argument{}, got {}", function_name, expected,
                        expected == 1 ? "" : "s", actual),
            "check the function signature for the expected number of parameters"};
}

// ─── Type Mismatch Hint ─────────────────────────────────────────────────────
// Returns a conversion hint string for common type mismatches, e.g. a
// suggestion to call Converter.to_number() when assigning string → number.
// Returns an empty string when no specific hint applies.

[[nodiscard]] std::string type_mismatch_hint(const TypeInfo& expected, const TypeInfo& actual);

// ─── Auto Hint for Diagnostic Code ──────────────────────────────────────────
// Returns a generic hint string for well-known error codes (e.g. type mismatch,
// undefined variable).  Used by the renderer as a fallback when no explicit
// hint was set.  Returns an empty string when no automatic hint applies.

[[nodiscard]] std::string_view auto_hint_for_code(DiagnosticCode code) noexcept;

} // namespace luma::diag_builders
