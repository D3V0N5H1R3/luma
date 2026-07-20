// ─────────────────────────────────────────────────────────────────────────────
// Compiler Error Messages
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Centralised compiler error message factories.
//
// Each function returns a pair of {message, hint} strings ready for passing
// to Compiler::error().  Grouping messages here makes them easy to review,
// keeps call sites concise, and prevents accidental divergence of wording.
//
// This is header-only — no .cpp file required.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_COMPILER_ERRORS_HPP
#define LUMA_COMPILER_COMPILER_ERRORS_HPP

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include "analysis/diagnostics/diagnostic.hpp"

namespace luma::compiler_errors {

// All message factories return DiagnosticText (defined in diagnostic.hpp),
// shared with diag_builders for consistency.

// ─── Scope / variable errors ────────────────────────────────────────────────

[[nodiscard]] inline DiagnosticText variable_already_declared(std::string_view name) {
    return {std::format("Variable '{}' already declared in this scope", name),
            "choose a different variable name"};
}

[[nodiscard]] inline DiagnosticText namespace_not_found(std::string_view name) {
    return {std::format("namespace '{}' not found", name),
            "check the namespace name or add the missing namespace declaration"};
}

// ─── Control-flow errors ────────────────────────────────────────────────────

[[nodiscard]] inline DiagnosticText break_outside_loop() {
    return {"'break' outside of loop", "'break' can only be used inside for or while loops"};
}

[[nodiscard]] inline DiagnosticText continue_outside_loop() {
    return {"'continue' outside of loop", "'continue' can only be used inside for or while loops"};
}

// ─── Internal / unknown-operator errors ─────────────────────────────────────

inline constexpr std::string_view k_internal_error_hint =
    "this is an internal compiler error — please report it";

[[nodiscard]] inline DiagnosticText unknown_unary_operator() {
    return {"Unknown unary operator", std::string{k_internal_error_hint}};
}

[[nodiscard]] inline DiagnosticText unknown_binary_operator() {
    return {"Unknown binary operator", std::string{k_internal_error_hint}};
}

[[nodiscard]] inline DiagnosticText unknown_compound_assignment_operator() {
    return {"Unknown compound assignment operator", std::string{k_internal_error_hint}};
}

// ─── Limit-exceeded errors ──────────────────────────────────────────────────
//
// These return an DiagnosticText with the generic "too many X (maximum Y)"
// pattern.  The caller passes the result to Compiler::error() rather than
// calling Compiler::error_limit_exceeded() directly.

[[nodiscard]] inline DiagnosticText limit_exceeded(std::string_view description,
                                                   std::size_t maximum, std::string_view hint) {
    return {std::format("too many {} (maximum {})", description, maximum), std::string{hint}};
}

[[nodiscard]] inline DiagnosticText too_many_local_variables(std::size_t maximum) {
    return limit_exceeded(
        "local variables in function", maximum,
        "refactor the function to use fewer local variables, or split it into smaller functions");
}

[[nodiscard]] inline DiagnosticText too_many_upvalues(std::size_t maximum) {
    return limit_exceeded("upvalues", maximum,
                          "reduce the number of captured variables, or pass them as parameters");
}

[[nodiscard]] inline DiagnosticText too_many_functions(std::size_t maximum) {
    return limit_exceeded(
        "functions in program", maximum,
        "reduce the number of functions or lambdas, or split the program into smaller modules");
}

[[nodiscard]] inline DiagnosticText too_many_positional_arguments(std::size_t maximum) {
    return limit_exceeded(
        "positional arguments", maximum,
        "reduce the number of arguments, or use a record to group related values");
}

[[nodiscard]] inline DiagnosticText too_many_named_arguments(std::size_t maximum) {
    return limit_exceeded("named arguments", maximum, "reduce the number of named arguments");
}

[[nodiscard]] inline DiagnosticText too_many_interpolation_parts(std::size_t maximum) {
    return limit_exceeded("string interpolation parts", maximum,
                          "split the string into multiple concatenated strings");
}

[[nodiscard]] inline DiagnosticText too_many_spawn_arguments(std::size_t maximum) {
    return limit_exceeded(
        "arguments to spawned function", maximum,
        "reduce the number of arguments, or use a record to group related values");
}

[[nodiscard]] inline DiagnosticText too_many_tail_call_arguments(std::size_t maximum) {
    return limit_exceeded("arguments for tail call", maximum, "reduce the number of arguments");
}

[[nodiscard]] inline DiagnosticText too_many_pipe_arguments(std::size_t maximum) {
    return limit_exceeded("positional arguments", maximum,
                          "reduce the number of arguments in the pipe chain");
}

[[nodiscard]] inline DiagnosticText too_many_error_pipe_arguments(std::size_t maximum) {
    return limit_exceeded("positional arguments", maximum,
                          "reduce the number of arguments in the error pipe chain");
}

[[nodiscard]] inline DiagnosticText too_many_record_fields(std::size_t maximum) {
    return limit_exceeded("record fields", maximum,
                          "split the record into smaller, composable records");
}

[[nodiscard]] inline DiagnosticText too_many_record_overrides(std::size_t maximum) {
    return limit_exceeded("record overrides", maximum, "reduce the number of field overrides");
}

[[nodiscard]] inline DiagnosticText too_many_choice_variant_fields(std::size_t maximum) {
    return limit_exceeded("choice variant fields", maximum, "split into multiple variants");
}

// ─── Warning messages ────────────────────────────────────────────────────────

[[nodiscard]] inline DiagnosticText record_type_not_found(std::string_view name) {
    return {std::format("Record type '{}' not found at compile time", name),
            "Record creation will use only explicit fields"};
}

} // namespace luma::compiler_errors

#endif // LUMA_COMPILER_COMPILER_ERRORS_HPP
