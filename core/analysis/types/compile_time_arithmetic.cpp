#include "analysis/types/compile_time_arithmetic.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace luma::compile_time_arithmetic {

std::optional<std::string> check_division(std::int64_t divisor, bool /*is_integer_div*/) {
    if (divisor == 0) {
        return "division by zero";
    }

    // Note: INT64_MIN / -1 overflow is checked separately by the caller
    // when both operands are known literals (see check_binary_constant_folding).
    return std::nullopt;
}

std::optional<std::string> check_float_division(double divisor) {
    if (divisor == 0.0) {
        return "division by zero";
    }

    return std::nullopt;
}

std::optional<std::string> check_shift_amount(std::int64_t amount) {
    if (amount < 0 || amount >= k_max_shift_bits) {
        return std::format("shift amount {} out of range — must be between 0 and {}", amount,
                           k_max_shift_bits - 1);
    }

    return std::nullopt;
}

std::optional<std::string> check_string_repeat(std::int64_t count, std::size_t max_length) {
    if (count < 0) {
        return "string repeat count must be non-negative";
    }

    if (static_cast<std::size_t>(count) > max_length) {
        return "string repeat count exceeds maximum";
    }

    return std::nullopt;
}

} // namespace luma::compile_time_arithmetic
