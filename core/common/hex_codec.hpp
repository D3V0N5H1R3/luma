#ifndef LUMA_COMMON_HEX_CODEC_HPP
#define LUMA_COMMON_HEX_CODEC_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace luma {

namespace detail {
constexpr std::string_view k_hex_digits_lower = "0123456789abcdef";
constexpr std::string_view k_hex_digits_upper = "0123456789ABCDEF";
} // namespace detail

// Convert a single nibble (0-15) to an uppercase hex character.
[[nodiscard]] constexpr char to_hex_digit_upper(int nibble) noexcept {
    return detail::k_hex_digits_upper[nibble & 0x0F];
}

// Convert a single nibble (0-15) to a lowercase hex character.
[[nodiscard]] constexpr char to_hex_digit(int nibble) noexcept {
    return detail::k_hex_digits_lower[nibble & 0x0F];
}

// Convert raw bytes to lowercase hex string.
[[nodiscard]] inline std::string to_hex(const unsigned char* data, std::size_t len) {
    std::string out{};
    out.reserve(len * 2);
    for (std::size_t i{0}; i < len; ++i) {
        out += to_hex_digit(data[i] >> 4);
        out += to_hex_digit(data[i] & 0x0F);
    }
    return out;
}

// Convert a string's bytes to hex.
[[nodiscard]] inline std::string to_hex(std::string_view input) {
    return to_hex(reinterpret_cast<const unsigned char*>(input.data()), input.size());
}

// Decode a hex character to its integer value (0-15). Returns -1 on invalid input.
[[nodiscard]] constexpr int from_hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

} // namespace luma

#endif // LUMA_COMMON_HEX_CODEC_HPP
