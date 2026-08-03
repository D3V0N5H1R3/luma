#ifndef LUMA_COMMON_INDEX_VALIDATOR_HPP
#define LUMA_COMMON_INDEX_VALIDATOR_HPP

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace luma {

// Index validation and range slicing utilities.
//
// Extracts duplicated bounds-checking and slice-computation logic
// used by the VM's collection dispatch (arrays, strings, tuples).

struct SliceRange {
    std::int64_t start;
    std::int64_t end;
};

// Whether compute_slice_range treats the end bound as inclusive (the end
// element is part of the slice) or exclusive (the end element is not).
// Used in place of a bare bool so call sites document their intent.
enum class RangeEnd {
    Exclusive,
    Inclusive
};

// Returns true if `index` is outside [0, size).
// Pure predicate — callers choose their own error-reporting mechanism.
[[nodiscard]] constexpr bool is_index_out_of_bounds(std::int64_t index, std::size_t size) noexcept {
    return index < 0 || index >= static_cast<std::int64_t>(size);
}

// Validates that `index` is within [0, size). Throws a descriptive
// std::out_of_range if the index is out of bounds.
inline void validate_index(std::int64_t index, std::size_t size,
                           std::string_view context = "Index") {
    if (is_index_out_of_bounds(index, size)) {
        throw std::out_of_range(
            std::format("{} {} is out of bounds for size {}.", context, index, size));
    }
}

// Converts a possibly-negative index to a non-negative index using
// Python-style semantics: -1 maps to size-1, -2 to size-2, etc.
// Does NOT validate that the result is in bounds.
[[nodiscard]] constexpr std::int64_t normalize_index(std::int64_t index,
                                                     std::size_t size) noexcept {
    if (index < 0) {
        return index + static_cast<std::int64_t>(size);
    }

    return index;
}

// Computes a clamped [start, end) slice range from raw range bounds.
//
// `end_kind` controls whether `raw_end` is treated as inclusive (the
// end value is incremented by one before clamping) or exclusive. Both
// `raw_start` and `raw_end` support negative (Python-style) indices.
//
// The returned range is clamped to [0, size] so callers can iterate
// directly without additional bounds checks.
[[nodiscard]] constexpr SliceRange compute_slice_range(std::int64_t raw_start, std::int64_t raw_end,
                                                       std::int64_t size,
                                                       RangeEnd end_kind) noexcept {
    assert(size >= 0 && "compute_slice_range: size must be non-negative");

    auto start = normalize_index(raw_start, static_cast<std::size_t>(size));
    auto end = normalize_index(raw_end, static_cast<std::size_t>(size));

    // Apply the inclusive adjustment AFTER normalizing negatives.  Doing it
    // before (raw_end + 1, then normalize) is wrong for raw_end == -1: it
    // yields normalize(0) == 0 (an empty slice) instead of normalize(-1) + 1
    // == size (the full tail).  The two orderings agree for every other value,
    // so `x[a..=-1]` — "through the last element, inclusive" — is the only case
    // this corrects.
    if (end_kind == RangeEnd::Inclusive && end < std::numeric_limits<std::int64_t>::max()) {
        end += 1;
    }

    start = std::max(start, std::int64_t{0});
    end = std::clamp(end, std::int64_t{0}, size);

    return {start, end};
}

} // namespace luma

#endif // LUMA_COMMON_INDEX_VALIDATOR_HPP
