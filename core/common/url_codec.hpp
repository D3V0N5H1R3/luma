#ifndef LUMA_COMMON_URL_CODEC_HPP
#define LUMA_COMMON_URL_CODEC_HPP

// Shared URL percent-encoding and decoding utilities.
// Used by both the Encoder and Http standard library modules.
// Lives in core/common alongside base64_codec, the sibling encoding codec.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "common/hex_codec.hpp"
#include "common/resource_limits.hpp"

namespace luma {

// Returns true for characters that RFC 3986 considers unreserved
// (i.e. never need percent-encoding).
[[nodiscard]] constexpr bool is_unreserved(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_' || c == '.' || c == '~';
}

// URL percent-encoding expansion: each byte becomes %XX (3 characters).
inline constexpr std::size_t k_url_encoding_expansion = 3;

// Number of bits in a hex nibble (used for high/low nibble extraction).
inline constexpr int k_hex_nibble_bits = 4;

// Percent-encode a string according to RFC 3986.
// Unreserved characters are passed through; all others become %XX.
[[nodiscard]] inline std::string url_encode(std::string_view input) {
    if (input.size() > ResourceLimits::max_string_size / k_url_encoding_expansion) {
        throw std::length_error{"url_encode: input exceeds maximum string size"};
    }

    std::string out{};
    out.reserve(input.size() * k_url_encoding_expansion);

    for (const auto c : input) {
        if (is_unreserved(c)) {
            out += c;
        } else {
            const auto byte = static_cast<std::uint8_t>(c);

            out += '%';
            out += to_hex_digit_upper(byte >> k_hex_nibble_bits);
            out += to_hex_digit_upper(byte & 0x0F);
        }
    }

    return out;
}

// Decode a percent-encoded string.  Also converts '+' to space
// (application/x-www-form-urlencoded).  Returns the decoded string, or
// std::nullopt on malformed input.
[[nodiscard]] inline std::optional<std::string> url_decode(std::string_view input) {
    if (input.size() > ResourceLimits::max_string_size) {
        return std::nullopt;
    }

    std::string out{};
    out.reserve(input.size());

    for (std::size_t i{0}; i < input.size(); ++i) {
        if (input[i] == '%') {
            if (i + 2 >= input.size()) {
                return std::nullopt;
            }

            const int hi = from_hex_digit(input[i + 1]);
            const int lo = from_hex_digit(input[i + 2]);

            if (hi < 0 || lo < 0) {
                return std::nullopt;
            }

            out += static_cast<char>((hi << k_hex_nibble_bits) | lo);

            i += 2;
        } else if (input[i] == '+') {
            out += ' ';
        } else {
            out += input[i];
        }
    }

    return out;
}

} // namespace luma

#endif // LUMA_COMMON_URL_CODEC_HPP
