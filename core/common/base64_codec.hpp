#ifndef LUMA_COMMON_BASE64_CODEC_HPP
#define LUMA_COMMON_BASE64_CODEC_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace luma {

namespace detail {

constexpr std::array<char, 64> k_base64_chars = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

consteval std::array<char, 64> make_base64url_chars() noexcept {
    auto arr = k_base64_chars;
    arr[62] = '-';
    arr[63] = '_';
    return arr;
}

constexpr std::array<char, 64> k_base64url_chars = make_base64url_chars();

} // namespace detail

// Encode input using the given 64-character alphabet. If use_padding is true, pads with '='.
[[nodiscard]] std::string base64_encode_with(const std::string& input,
                                             const std::array<char, 64>& alphabet,
                                             bool use_padding);

// Standard base64 encoding with padding.
[[nodiscard]] inline std::string base64_encode(const std::string& input) {
    return base64_encode_with(input, detail::k_base64_chars, true);
}

// URL-safe base64 encoding without padding.
[[nodiscard]] inline std::string base64url_encode(const std::string& input) {
    return base64_encode_with(input, detail::k_base64url_chars, false);
}

// Decode a single base64 character with the given special characters for positions 62 and 63.
[[nodiscard]] constexpr int base64_decode_char(char c, char char62, char char63) noexcept {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == char62) {
        return 62;
    }
    if (c == char63) {
        return 63;
    }
    return -1;
}

// Decode base64 input using the given single-character decoder function.
using Base64DecodeFn = int (*)(char) noexcept;

[[nodiscard]] std::optional<std::string> base64_decode_with(const std::string& input,
                                                            Base64DecodeFn decode_char);

// Standard base64 decoding (with padding support).
[[nodiscard]] inline std::optional<std::string> base64_decode(const std::string& input) {
    return base64_decode_with(
        input, [](char c) noexcept -> int { return base64_decode_char(c, '+', '/'); });
}

// URL-safe base64 decoding (with padding support).
[[nodiscard]] inline std::optional<std::string> base64url_decode(const std::string& input) {
    return base64_decode_with(
        input, [](char c) noexcept -> int { return base64_decode_char(c, '-', '_'); });
}

} // namespace luma

#endif // LUMA_COMMON_BASE64_CODEC_HPP
