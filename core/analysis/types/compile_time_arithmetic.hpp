#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

/// Compile-time arithmetic validation utilities.
/// Used by the type checker to detect division by zero, overflow, invalid
/// shift amounts, and excessive string repetition at analysis time.
/// The compiler's ConstantFolder performs the actual folding; these helpers
/// focus on diagnostics that should fire before code generation.
namespace luma::compile_time_arithmetic {

/// The bit width of Luma's integer type.  A left/right shift is only defined
/// for amounts in the range [0, k_max_shift_bits); shifting by
/// k_max_shift_bits or more is out of range.
inline constexpr int k_max_shift_bits = 64;

/// Returns an error message if dividing by the given value would fail.
/// @param divisor  The compile-time divisor value.
/// @param is_integer_div  True for integer division (// or %), false for
///   floating-point division (/).
[[nodiscard]] std::optional<std::string> check_division(std::int64_t divisor, bool is_integer_div);

/// Returns an error message if dividing a floating-point value by the
/// given value would fail.
[[nodiscard]] std::optional<std::string> check_float_division(double divisor);

/// Returns an error message if the shift amount is out of range [0, 63].
[[nodiscard]] std::optional<std::string> check_shift_amount(std::int64_t amount);

/// Returns an error message if string repetition count is invalid
/// (negative or exceeds max_length).
[[nodiscard]] std::optional<std::string> check_string_repeat(std::int64_t count,
                                                             std::size_t max_length);

} // namespace luma::compile_time_arithmetic
