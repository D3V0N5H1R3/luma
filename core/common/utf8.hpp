#ifndef LUMA_COMMON_UTF8_HPP
#define LUMA_COMMON_UTF8_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace luma {

// UTF-8 byte boundaries used for character classification and column tracking.
inline constexpr unsigned char k_utf8_ascii_max = 0x80;
inline constexpr unsigned char k_utf8_leading_byte_min = 0xC0;

// ─── UTF-8 byte masks ───
// Mask selecting the two high bits that identify a continuation byte
// (10xxxxxx, RFC 3629 §3).
constexpr unsigned char k_utf8_2byte_mask = 0xC0;

// ─── UTF-8 helpers ───
// Shared between the string module, the VM, and any other
// component that needs Unicode-aware string handling.

// Return true if `b` is a valid UTF-8 continuation byte (10xxxxxx).
[[nodiscard]] constexpr bool is_utf8_continuation(unsigned char b) noexcept {
    return (b & k_utf8_2byte_mask) == 0x80;
}

// U+FFFD REPLACEMENT CHARACTER — the codepoint reported for any byte that does
// not start a well-formed UTF-8 sequence.
inline constexpr std::uint32_t k_utf8_replacement_char = 0xFFFD;

// Return the byte length of the UTF-8 sequence announced by lead byte `byte`.
//
// Only lead bytes that can start a well-formed sequence (RFC 3629 §4) report
// a multi-byte length: C2–DF → 2, E0–EF → 3, F0–F4 → 4.  ASCII, bare
// continuation bytes (80–BF), the overlong-only leads C0/C1, and F5–FF all
// report 1.  This looks at the lead byte alone; when the following bytes are
// available prefer utf8_decode(), whose length also accounts for malformed
// or truncated continuation bytes.
[[nodiscard]] constexpr int utf8_codepoint_len(std::uint8_t byte) noexcept {
    if (byte >= 0xC2 && byte <= 0xDF) {
        return 2;
    }
    if (byte >= 0xE0 && byte <= 0xEF) {
        return 3;
    }
    if (byte >= 0xF0 && byte <= 0xF4) {
        return 4;
    }

    return 1; // ASCII or an invalid leading byte — treat as single byte.
}

// A decoded codepoint and the number of bytes it occupied.
struct Utf8Decoded {
    std::uint32_t codepoint;
    std::size_t length;
};

// Strictly decode the UTF-8 sequence starting at byte `pos` of `s` (RFC 3629
// §4, Unicode Table 3-7).  Accepts std::string, std::string_view, or any
// contiguous container with size() and operator[] returning a char-like type.
//
// Any ill-formed sequence — a bare continuation byte, a C0/C1 or F5–FF lead,
// an overlong form, a UTF-16 surrogate (U+D800–U+DFFF), a value above
// U+10FFFF, a missing continuation byte, or a sequence truncated by the end
// of the input — decodes as {U+FFFD, 1}, so a caller that advances by
// `length` resynchronises on the very next byte and never skips valid text.
// `length` never extends past s.size(); when `pos >= s.size()` the result
// is {U+FFFD, 0}.
template <typename StringLike>
[[nodiscard]] constexpr Utf8Decoded utf8_decode(const StringLike& s, std::size_t pos) noexcept {
    constexpr Utf8Decoded k_invalid{.codepoint = k_utf8_replacement_char, .length = 1};

    if (pos >= s.size()) {
        return {.codepoint = k_utf8_replacement_char, .length = 0};
    }

    const auto byte_at = [&s, pos](std::size_t offset) {
        return static_cast<std::uint8_t>(s[pos + offset]);
    };
    const std::uint8_t lead = byte_at(0);

    if (lead < 0x80) {
        return {.codepoint = lead, .length = 1};
    }

    const auto len = static_cast<std::size_t>(utf8_codepoint_len(lead));

    if (len == 1 || pos + len > s.size()) {
        return k_invalid;
    }

    // The second byte's valid range depends on the lead byte; this is what
    // rules out overlong forms (E0, F0), surrogates (ED), and values above
    // U+10FFFF (F4).  Every other continuation byte is the plain 80–BF range.
    std::uint8_t second_min = 0x80;
    std::uint8_t second_max = 0xBF;

    if (lead == 0xE0) {
        second_min = 0xA0;
    } else if (lead == 0xED) {
        second_max = 0x9F;
    } else if (lead == 0xF0) {
        second_min = 0x90;
    } else if (lead == 0xF4) {
        second_max = 0x8F;
    }

    const std::uint8_t second = byte_at(1);

    if (second < second_min || second > second_max) {
        return k_invalid;
    }

    for (std::size_t k = 2; k < len; ++k) {
        if (!is_utf8_continuation(byte_at(k))) {
            return k_invalid;
        }
    }

    constexpr std::uint8_t k_payload_mask = 0x3F;
    std::uint32_t cp = 0;

    if (len == 2) {
        cp = static_cast<std::uint32_t>(lead & 0x1F);
    } else if (len == 3) {
        cp = static_cast<std::uint32_t>(lead & 0x0F);
    } else {
        cp = static_cast<std::uint32_t>(lead & 0x07);
    }

    for (std::size_t k = 1; k < len; ++k) {
        cp = (cp << 6) | static_cast<std::uint32_t>(byte_at(k) & k_payload_mask);
    }

    return {.codepoint = cp, .length = len};
}

// Count the number of Unicode codepoints in a UTF-8 string — the value
// editors use for column/character positions, not the byte length.  Steps by
// utf8_decode() lengths, so every ill-formed byte counts as one codepoint
// (exactly as UTF8Iterator sees it) and the loop always terminates.
[[nodiscard]] inline constexpr int utf8_codepoint_count(std::string_view s) noexcept {
    int count{0};
    for (std::size_t pos{0}; pos < s.size(); pos += utf8_decode(s, pos).length) {
        ++count;
    }
    return count;
}

// Return the byte length of the codepoint at position `pos` in `s`.
// Agrees with utf8_decode(): ill-formed or truncated sequences are 1 byte,
// and the result never extends past s.size() (0 when `pos >= s.size()`).
[[nodiscard]] inline std::size_t utf8_advance(const std::string& s, std::size_t pos) noexcept {
    return utf8_decode(s, pos).length;
}

// Extract the single UTF-8 character (as a std::string) at byte position `pos`.
[[nodiscard]] inline std::string utf8_char_at_byte(const std::string& s, std::size_t pos) {
    if (pos >= s.size()) {
        return {};
    }

    return s.substr(pos, utf8_advance(s, pos));
}

// Decode the Unicode codepoint at byte position `pos` in `s`.
// Accepts std::string, std::string_view, or any contiguous container.
//
// Returns U+FFFD (REPLACEMENT CHARACTER) for any ill-formed sequence — see
// utf8_decode(), which also reports how many bytes the codepoint occupied.
template <typename StringLike>
[[nodiscard]] inline std::uint32_t utf8_decode_at(const StringLike& s, std::size_t pos) {
    return utf8_decode(s, pos).codepoint;
}

// Combine a UTF-16 surrogate pair into a single Unicode codepoint.
// `high` must be in [0xD800, 0xDBFF] and `low` in [0xDC00, 0xDFFF].
[[nodiscard]] constexpr char32_t decode_surrogate_pair(char32_t high, char32_t low) noexcept {
    return 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
}

// Encode a Unicode codepoint as a UTF-8 string.
//
// Returns an empty string for invalid codepoints (> U+10FFFF or surrogate
// halves U+D800–U+DFFF).  Callers should validate codepoints before encoding
// if error reporting is needed.
[[nodiscard]] inline std::string utf8_encode(std::uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return "";
    }

    std::string result{};

    if (cp < 0x80) {
        result += static_cast<char>(cp);
    } else if (cp < 0x800) {
        result += static_cast<char>(0xC0 | (cp >> 6));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        result += static_cast<char>(0xE0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        result += static_cast<char>(0xF0 | (cp >> 18));
        result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    }

    return result;
}

// Returns true if the bytes at [pos, pos+seq_len) in `line` form a valid
// UTF-8 sequence (all continuation bytes after the lead byte have 10xxxxxx).
[[nodiscard]] inline bool is_valid_utf8_run(std::string_view line, std::size_t pos,
                                            std::size_t seq_len) noexcept {
    for (std::size_t k = 1; k < seq_len; ++k) {
        if (!is_utf8_continuation(static_cast<unsigned char>(line[pos + k]))) {
            return false;
        }
    }
    return true;
}

} // namespace luma

#endif // LUMA_COMMON_UTF8_HPP
