#ifndef LUMA_STDLIB_ERROR_MESSAGES_HPP
#define LUMA_STDLIB_ERROR_MESSAGES_HPP

// ═══════════════════════════════════════════════════════════
// Stdlib error message formatting — standard pattern
// ═══════════════════════════════════════════════════════════
//
// Every RuntimeError thrown from stdlib code should follow the
// format "Module.function: descriptive message".  Use one of:
//
//   1. error_msg("Module", "function", "detail")
//      → "Module.function: detail"
//
//   2. ErrorMessages::qualified_helper("Module", "function", ...)
//      → "Module.function: specific detail"
//
//   3. std::format("{}.{}: ...", module, function, ...)
//      for one-off messages not covered by a helper.
//
// Prefer the expect_* helpers in native_function.hpp for type
// validation — they produce consistent messages automatically.
//
// Do NOT use string concatenation (operator+) for error messages.
// Do NOT omit the "Module.function:" prefix.
// ═══════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace luma {

// Machine-readable error code constants used in structured failure results.
// These are the error_code values passed to make_failure_value(msg, code, fn).
namespace error_codes {

constexpr std::string_view empty_container = "empty_container";
constexpr std::string_view index_out_of_bounds = "index_out_of_bounds";
constexpr std::string_view key_not_found = "key_not_found";
constexpr std::string_view type_mismatch = "type_mismatch";
constexpr std::string_view division_by_zero = "division_by_zero";
constexpr std::string_view size_limit_exceeded = "size_limit_exceeded";
constexpr std::string_view parse_error = "parse_error";
constexpr std::string_view io_error = "io_error";
constexpr std::string_view invalid_argument = "invalid_argument";
constexpr std::string_view not_found = "not_found";
constexpr std::string_view invalid_format = "invalid_format";
constexpr std::string_view domain_error = "domain_error";
constexpr std::string_view overflow = "overflow";
constexpr std::string_view underflow = "underflow";
constexpr std::string_view network_error = "network_error";
constexpr std::string_view timeout = "timeout";
constexpr std::string_view connection_refused = "connection_refused";
constexpr std::string_view invalid_url = "invalid_url";
constexpr std::string_view operation_failed = "operation_failed";
constexpr std::string_view permission_denied = "permission_denied";
constexpr std::string_view channel_closed = "channel_closed";

} // namespace error_codes

// Format a qualified error message: "Module.function: detail".
// This is the simplest helper for the most common error pattern used
// throughout the stdlib.  Prefer this over hand-writing the prefix.
[[nodiscard]] inline std::string error_msg(std::string_view module, std::string_view function,
                                           std::string_view detail) {
    return std::format("{}.{}: {}", module, function, detail);
}

// Centralised error messages for stdlib modules.
// Provides consistent, reusable error strings across all stdlib code.
//
// Two styles of helper are provided:
//   1. Unqualified — short messages without a module/function prefix.
//   2. Qualified   — messages prefixed with "Module.function: …".
//
// Prefer the qualified variants in new code so that every error message
// clearly identifies the function that produced it.
class ErrorMessages {
public:
    // ── Unqualified helpers (no Module.function prefix) ─────────────

    [[nodiscard]] static std::string index_out_of_bounds(std::int64_t index, std::size_t size) {
        return std::format("index {} out of bounds for array of length {}", index, size);
    }

    [[nodiscard]] static std::string key_not_found(const std::string& key) {
        return std::format("key '{}' not found", key);
    }

    [[nodiscard]] static std::string type_mismatch(std::string_view expected,
                                                   std::string_view actual) {
        return std::format("expected {}, got '{}'", expected, actual);
    }

    [[nodiscard]] static std::string division_by_zero() {
        return "division by zero";
    }

    [[nodiscard]] static std::string empty_container(std::string_view operation) {
        return std::format("{}: container is empty", operation);
    }

    // ── Qualified helpers (Module.function prefixed) ────────────────

    // "Module.function: expected X, got 'Y'"
    [[nodiscard]] static std::string expected_type(std::string_view module,
                                                   std::string_view function,
                                                   std::string_view expected_type_name,
                                                   std::string_view actual_type_name) {
        return std::format("{}.{}: expected {}, got '{}'", module, function, expected_type_name,
                           actual_type_name);
    }

    // "Module.function: container is empty"
    [[nodiscard]] static std::string empty_container(std::string_view module,
                                                     std::string_view function) {
        return std::format("{}.{}: container is empty", module, function);
    }

    // "Module.function: index N out of bounds (size M)"
    [[nodiscard]] static std::string index_out_of_bounds(std::string_view module,
                                                         std::string_view function,
                                                         std::int64_t index, std::size_t size) {
        return std::format("{}.{}: index {} out of bounds (size {})", module, function, index,
                           size);
    }

    // "Module.function: key 'X' not found"
    [[nodiscard]] static std::string
    key_not_found(std::string_view module, std::string_view function, std::string_view key) {
        return std::format("{}.{}: key '{}' not found", module, function, key);
    }

    // "Module.function: division by zero"
    [[nodiscard]] static std::string division_by_zero(std::string_view module,
                                                      std::string_view function) {
        return std::format("{}.{}: division by zero", module, function);
    }

    // ── Value-out-of-range helpers ──────────────────────────────────

    // "Module.function: value out of range"
    [[nodiscard]] static std::string value_out_of_range(std::string_view module,
                                                        std::string_view function) {
        return std::format("{}.{}: value out of range", module, function);
    }

    // "Module.function: value out of range (range_description)"
    [[nodiscard]] static std::string value_out_of_range(std::string_view module,
                                                        std::string_view function,
                                                        std::string_view range_description) {
        return std::format("{}.{}: value out of range ({})", module, function, range_description);
    }

    // ── Size-limit helpers ──────────────────────────────────────────

    // "Module.function: exceeds maximum size"
    [[nodiscard]] static std::string size_limit_exceeded(std::string_view module,
                                                         std::string_view function) {
        return std::format("{}.{}: exceeds maximum size", module, function);
    }

    // "Module.function: result exceeds maximum size"
    [[nodiscard]] static std::string result_exceeds_maximum_size(std::string_view module,
                                                                 std::string_view function) {
        return std::format("{}.{}: result exceeds maximum size", module, function);
    }

    // ── Constraint helpers ──────────────────────────────────────────

    // "Module.function: parameter must be non-negative"
    [[nodiscard]] static std::string must_be_non_negative(std::string_view module,
                                                          std::string_view function,
                                                          std::string_view parameter) {
        return std::format("{}.{}: {} must be non-negative", module, function, parameter);
    }

    // "Module.function: parameter must be non-negative, got N"
    [[nodiscard]] static std::string must_be_non_negative(std::string_view module,
                                                          std::string_view function,
                                                          std::string_view parameter,
                                                          std::int64_t actual) {
        return std::format("{}.{}: {} must be non-negative, got {}", module, function, parameter,
                           actual);
    }

    // "Module.function: parameter must be positive"
    [[nodiscard]] static std::string must_be_positive(std::string_view module,
                                                      std::string_view function,
                                                      std::string_view parameter) {
        return std::format("{}.{}: {} must be positive", module, function, parameter);
    }

    // "Module.function: parameter must be positive, got N"
    [[nodiscard]] static std::string must_be_positive(std::string_view module,
                                                      std::string_view function,
                                                      std::string_view parameter,
                                                      std::int64_t actual) {
        return std::format("{}.{}: {} must be positive, got {}", module, function, parameter,
                           actual);
    }

    // ── Conversion / parse / IO helpers ─────────────────────────────

    // "cannot convert 'value' to target_type"
    [[nodiscard]] static std::string conversion_error(std::string_view value,
                                                      std::string_view target_type) {
        return std::format("cannot convert '{}' to {}", value, target_type);
    }
};

} // namespace luma

#endif // LUMA_STDLIB_ERROR_MESSAGES_HPP
