#ifndef LUMA_STDLIB_STRING_MODULE_HPP
#define LUMA_STDLIB_STRING_MODULE_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "runtime/stdlib/common/stdlib_fwd.hpp"

namespace luma {

void register_string_ns(const EnvPtr& env);

// Internal sub-registration functions (split for readability).
void register_string_search(const EnvPtr& env);
void register_string_transform(const EnvPtr& env);

// Direction for string padding operations.
enum class PaddingDirection {
    start,
    end
};

// Build a string of exactly `codepoint_count` codepoints by cycling the UTF-8
// `fill` pattern (e.g. fill "ab", count 5 → "ababa").  `fill` must be non-empty.
// Shared across the String sub-modules so that pad_left/pad_right (via
// apply_padding) and center use a single fill-cycling implementation.
[[nodiscard]] std::string build_cycled_fill(const std::string& fill, std::size_t codepoint_count);

// Pad input to target_width using fill as the repeating pattern.
// PaddingDirection::start pads on the left, PaddingDirection::end pads on the right.
[[nodiscard]] std::string apply_padding(const std::string& input, const std::string& fill,
                                        std::size_t target_width, PaddingDirection direction);

} // namespace luma

#endif // LUMA_STDLIB_STRING_MODULE_HPP
