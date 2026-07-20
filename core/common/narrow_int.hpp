#ifndef LUMA_COMMON_NARROW_INT_HPP
#define LUMA_COMMON_NARROW_INT_HPP

#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>

namespace luma {

// Narrowing cast from int64_t to int — throws on overflow.
// Use for protocol fields where an out-of-range value is a hard error.
[[nodiscard]] inline int narrow_int(int64_t value) {
    if (value < (std::numeric_limits<int>::min)() || value > (std::numeric_limits<int>::max)()) {
        throw std::runtime_error(std::format("Integer value {} out of int range", value));
    }

    return static_cast<int>(value);
}

// Narrowing cast from int64_t to int — clamps to [0, INT_MAX - 1].
// Use for protocol fields (e.g. LSP positions) that are always non-negative.
// The upper bound is INT_MAX - 1 (not INT_MAX) to reserve one unit of headroom:
// callers routinely convert a 0-based protocol position to a 1-based internal
// line/column with `+ 1`, and clamping to INT_MAX would make that increment a
// signed-integer overflow (undefined behaviour) on a hostile INT_MAX position.
[[nodiscard]] constexpr int clamp_to_int(int64_t value) {
    constexpr int k_max = (std::numeric_limits<int>::max)() - 1;

    if (value < 0) {
        return 0;
    }

    if (value > k_max) {
        return k_max;
    }

    return static_cast<int>(value);
}

// Narrowing cast from size_t to int — clamps to [0, INT_MAX - 1].
// Convenience overload for the ubiquitous "container.size() as an int" narrowing
// (DAP/LSP element counts and indices).  Shares the saturating upper bound with
// the int64_t version above so both widths clamp identically.
[[nodiscard]] constexpr int clamp_to_int(std::size_t value) noexcept {
    constexpr std::size_t k_max = static_cast<std::size_t>((std::numeric_limits<int>::max)() - 1);

    return value > k_max ? static_cast<int>(k_max) : static_cast<int>(value);
}

// Narrowing cast from int64_t to int — returns std::nullopt on overflow.
// Use for "safe" accessors that fall back to a default rather than raising a
// hard error (e.g. JsonValue::try_as<int>() / get_or<int>()).
[[nodiscard]] constexpr std::optional<int> try_narrow_int(int64_t value) noexcept {
    if (value < (std::numeric_limits<int>::min)() || value > (std::numeric_limits<int>::max)()) {
        return std::nullopt;
    }

    return static_cast<int>(value);
}

} // namespace luma

#endif // LUMA_COMMON_NARROW_INT_HPP
