#ifndef LUMA_STDLIB_NUMERIC_HELPERS_HPP
#define LUMA_STDLIB_NUMERIC_HELPERS_HPP

// ═══════════════════════════════════════════════════════════
// Shared numeric-validation helpers for stdlib modules
// ═══════════════════════════════════════════════════════════
//
// Centralised NaN/Inf checks and double→int64 range validation
// used by math_module, function_builder, and other stdlib code.

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace luma::stdlib {

/// Return true when \p value is a finite real number (not NaN, not ±Inf).
[[nodiscard]] inline bool is_valid_numeric(double value) noexcept {
    return !std::isnan(value) && !std::isinf(value);
}

/// Try to convert a double to int64_t.
/// Returns std::nullopt when the value is NaN, Inf, or outside the
/// representable int64_t range.
[[nodiscard]] inline std::optional<std::int64_t> safe_to_int64(double value) noexcept {
    if (!is_valid_numeric(value)) {
        return std::nullopt;
    }

    if (value < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        value >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }

    return static_cast<std::int64_t>(value);
}

/// Return true when the value is within the representable int64_t range.
[[nodiscard]] inline bool is_in_int64_range(double value) noexcept {
    return is_valid_numeric(value) &&
           value >= static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
           value < static_cast<double>(std::numeric_limits<std::int64_t>::max());
}

/// Return true when the value is a finite positive number (> 0).
[[nodiscard]] inline bool is_positive(double value) noexcept {
    return is_valid_numeric(value) && value > 0.0;
}

/// Return true when the value is a finite non-negative number (>= 0).
[[nodiscard]] inline bool is_non_negative(double value) noexcept {
    return is_valid_numeric(value) && value >= 0.0;
}

} // namespace luma::stdlib

#endif // LUMA_STDLIB_NUMERIC_HELPERS_HPP
