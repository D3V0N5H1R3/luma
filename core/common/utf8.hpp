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
// Named constants for the leading-byte mask values used to classify
// multi-byte UTF-8 sequences (RFC 3629 §3).
constexpr unsigned char k_utf8_2byte_mask = 0xC0;
constexpr unsigned char k_utf8_3byte_mask = 0xE0;
constexpr unsigned char k_utf8_4byte_mask = 0xF0;
constexpr unsigned char k_utf8_5byte_mask = 0xF8;

// ─── UTF-8 helpers ───
// Shared between the string module, the VM, and any other
// component that needs Unicode-aware string handling.

// Return true if `b` is a valid UTF-8 continuation byte (10xxxxxx).
[[nodiscard]] constexpr bool is_utf8_continuation(unsigned char b) noexcept {
    return (b & k_utf8_2byte_mask) == 0x80;
}

// Leading-byte classification for multi-byte UTF-8 sequences.
[[nodiscard]] constexpr bool is_2byte_utf8(std::uint8_t b) noexcept {
    return (b & k_utf8_3byte_mask) == k_utf8_2byte_mask;
}

[[nodiscard]] constexpr bool is_3byte_utf8(std::uint8_t b) noexcept {
    return (b & k_utf8_4byte_mask) == k_utf8_3byte_mask;
}

[[nodiscard]] constexpr bool is_4byte_utf8(std::uint8_t b) noexcept {
    return (b & k_utf8_5byte_mask) == k_utf8_4byte_mask;
}

// Return the byte length of the UTF-8 codepoint starting at `byte`.
[[nodiscard]] constexpr int utf8_codepoint_len(std::uint8_t byte) noexcept {
    if (byte < 0x80) {
        return 1;
    }
    if (is_2byte_utf8(byte)) {
        return 2;
    }
    if (is_3byte_utf8(byte)) {
        return 3;
    }
    if (is_4byte_utf8(byte)) {
        return 4;
    }

    return 1; // Invalid leading byte — treat as single byte.
}

// Count the number of Unicode codepoints in a UTF-8 string.  Continuation
// bytes (10xxxxxx) are not counted, so the result is the codepoint length —
// the value editors use for column/character positions, not the byte length.
// Malformed lead bytes advance by one, so the loop always terminates.
[[nodiscard]] inline constexpr int utf8_codepoint_count(std::string_view s) noexcept {
    int count{0};
    for (std::size_t pos{0}; pos < s.size();
         pos += static_cast<std::size_t>(utf8_codepoint_len(static_cast<std::uint8_t>(s[pos])))) {
        ++count;
    }
    return count;
}

// Return the byte length of the codepoint at position `pos` in `s`.
[[nodiscard]] inline std::size_t utf8_advance(const std::string& s, std::size_t pos) noexcept {
    return static_cast<std::size_t>(utf8_codepoint_len(static_cast<std::uint8_t>(s[pos])));
}

// Extract the single UTF-8 character (as a std::string) at byte position `pos`.
[[nodiscard]] inline std::string utf8_char_at_byte(const std::string& s, std::size_t pos) {
    if (pos >= s.size()) {
        return {};
    }

    const auto len = utf8_advance(s, pos);

    if (pos + len > s.size()) {
        return s.substr(pos);
    }

    return s.substr(pos, len);
}

// Extract the continuation-byte payload at `s[pos + offset]`.
// Accepts std::string, std::string_view, or any contiguous container with
// operator[] returning a char-like type.
template <typename StringLike>
[[nodiscard]] inline std::uint32_t utf8_cont(const StringLike& s, std::size_t pos,
                                             std::size_t offset) {
    return static_cast<std::uint8_t>(s[pos + offset]) & 0x3F;
}

// Decode the Unicode codepoint at byte position `pos` in `s`.
// Accepts std::string, std::string_view, or any contiguous container.
//
// Returns the raw byte value for invalid sequences, treating them as
// single-byte characters.  This matches the "replacement" strategy used
// by most text editors and avoids throwing on malformed input.
template <typename StringLike>
[[nodiscard]] inline std::uint32_t utf8_decode_at(const StringLike& s, std::size_t pos) {
    const auto byte = static_cast<std::uint8_t>(s[pos]);

    if (byte < 0x80) {
        return byte;
    }

    if (is_2byte_utf8(byte) && pos + 1 < s.size()) {
        return (static_cast<std::uint32_t>(byte & 0x1F) << 6) | utf8_cont(s, pos, 1);
    }

    if (is_3byte_utf8(byte) && pos + 2 < s.size()) {
        return (static_cast<std::uint32_t>(byte & 0x0F) << 12) | (utf8_cont(s, pos, 1) << 6) |
               utf8_cont(s, pos, 2);
    }

    if (is_4byte_utf8(byte) && pos + 3 < s.size()) {
        return (static_cast<std::uint32_t>(byte & 0x07) << 18) | (utf8_cont(s, pos, 1) << 12) |
               (utf8_cont(s, pos, 2) << 6) | utf8_cont(s, pos, 3);
    }

    return byte;
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
