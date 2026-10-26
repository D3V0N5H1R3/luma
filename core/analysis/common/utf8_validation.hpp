#ifndef LUMA_ANALYSIS_COMMON_UTF8_VALIDATION_HPP
#define LUMA_ANALYSIS_COMMON_UTF8_VALIDATION_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace luma {

// Byte length of the well-formed UTF-8 sequence starting at `text[pos]`, or 0
// when the bytes there are malformed.  Follows the RFC 3629 §4 table exactly,
// so overlong encodings, UTF-16 surrogate halves (U+D800–U+DFFF), code points
// above U+10FFFF, stray continuation bytes and truncated sequences are all
// rejected.  ASCII bytes have length 1.
[[nodiscard]] constexpr std::size_t well_formed_utf8_length(std::string_view text,
                                                            std::size_t pos) noexcept {
    if (pos >= text.size()) {
        return 0;
    }

    const auto byte_at = [&text](std::size_t index) {
        return static_cast<std::uint8_t>(text[index]);
    };
    const auto in_range = [](std::uint8_t value, std::uint8_t low, std::uint8_t high) {
        return value >= low && value <= high;
    };

    const std::uint8_t lead = byte_at(pos);

    if (lead < 0x80) {
        return 1;
    }

    // [low, high] bounds of the second byte, and the total sequence length,
    // per RFC 3629's well-formed byte-sequence table.
    std::uint8_t second_low{0x80};
    std::uint8_t second_high{0xBF};
    std::size_t length{0};

    if (in_range(lead, 0xC2, 0xDF)) {
        length = 2;
    } else if (lead == 0xE0) {
        length = 3;
        second_low = 0xA0;
    } else if (in_range(lead, 0xE1, 0xEC) || in_range(lead, 0xEE, 0xEF)) {
        length = 3;
    } else if (lead == 0xED) {
        length = 3;
        second_high = 0x9F;
    } else if (lead == 0xF0) {
        length = 4;
        second_low = 0x90;
    } else if (in_range(lead, 0xF1, 0xF3)) {
        length = 4;
    } else if (lead == 0xF4) {
        length = 4;
        second_high = 0x8F;
    } else {
        return 0; // 0x80–0xC1 and 0xF5–0xFF never start a sequence.
    }

    if (pos + length > text.size() || !in_range(byte_at(pos + 1), second_low, second_high)) {
        return 0;
    }

    for (std::size_t offset{2}; offset < length; ++offset) {
        if (!in_range(byte_at(pos + offset), 0x80, 0xBF)) {
            return 0;
        }
    }

    return length;
}

// Decode the code point of a sequence already validated by
// well_formed_utf8_length() (`length` is that function's result).
[[nodiscard]] constexpr char32_t decode_well_formed_utf8(std::string_view text, std::size_t pos,
                                                         std::size_t length) noexcept {
    constexpr std::uint8_t k_continuation_payload = 0x3F;
    constexpr std::array<std::uint8_t, 4> k_lead_payload{0x7F, 0x1F, 0x0F, 0x07};

    auto code_point =
        static_cast<char32_t>(static_cast<std::uint8_t>(text[pos]) & k_lead_payload[length - 1]);

    for (std::size_t offset{1}; offset < length; ++offset) {
        code_point = (code_point << 6) |
                     (static_cast<std::uint8_t>(text[pos + offset]) & k_continuation_payload);
    }

    return code_point;
}

// True for code points that render as nothing or as blank space, or that a
// terminal treats as control: C0/C1 controls, DEL, and the Unicode space
// separators, line/paragraph separators and the BOM / zero-width no-break
// space.  None of these may appear in an identifier.
[[nodiscard]] constexpr bool is_unicode_space_or_control(char32_t code_point) noexcept {
    return code_point < 0x20 || (code_point >= 0x7F && code_point <= 0xA0) ||
           code_point == 0x1680 || (code_point >= 0x2000 && code_point <= 0x200A) ||
           code_point == 0x2028 || code_point == 0x2029 || code_point == 0x202F ||
           code_point == 0x205F || code_point == 0x3000 || code_point == 0xFEFF;
}

} // namespace luma

#endif // LUMA_ANALYSIS_COMMON_UTF8_VALIDATION_HPP
