#include "common/base64_codec.hpp"

#include <stdexcept>

#include "common/resource_limits.hpp"

namespace luma {

namespace {
// Bit masks for extracting 6-bit groups and 2/4-bit fragments
// used during Base64 encoding and decoding.
constexpr std::uint8_t k_two_bit_mask = 0x03;
constexpr std::uint8_t k_four_bit_mask = 0x0F;
constexpr std::uint8_t k_six_bit_mask = 0x3F;

// Shift amounts for positioning 6-bit groups within a 24-bit triplet.
constexpr int k_shift_2 = 2;
constexpr int k_shift_4 = 4;
constexpr int k_shift_6 = 6;

// Encode up to 3 input bytes into base64 characters and append to `out`.
void encode_triplet(std::string& out, const std::array<std::uint8_t, 3>& bytes,
                    std::size_t remaining, const std::array<char, 64>& alphabet, bool use_padding) {
    out += alphabet[static_cast<std::size_t>(bytes[0] >> k_shift_2)];
    out += alphabet[static_cast<std::size_t>(((bytes[0] & k_two_bit_mask) << k_shift_4) |
                                             (bytes[1] >> k_shift_4))];

    if (remaining == 1) {
        if (use_padding) {
            out += "==";
        }
    } else if (remaining == 2) {
        out += alphabet[static_cast<std::size_t>(bytes[1] & k_four_bit_mask) << k_shift_2];
        if (use_padding) {
            out += '=';
        }
    } else {
        out += alphabet[static_cast<std::size_t>(((bytes[1] & k_four_bit_mask) << k_shift_2) |
                                                 (bytes[2] >> k_shift_6))];
        out += alphabet[static_cast<std::size_t>(bytes[2] & k_six_bit_mask)];
    }
}

// Decode a quad of base64 characters and append the decoded bytes to `out`.
// Returns false if any character is invalid.
[[nodiscard]] bool decode_quad(std::string& out, char c0, char c1, char c2, char c3,
                               Base64DecodeFn decode_char) {
    const auto v0 = decode_char(c0);
    const auto v1 = decode_char(c1);

    if (v0 < 0 || v1 < 0) {
        return false;
    }

    out += static_cast<char>((v0 << k_shift_2) | (v1 >> k_shift_4));

    if (c2 != '=') {
        const auto v2 = decode_char(c2);
        if (v2 < 0) {
            return false;
        }
        out += static_cast<char>(((v1 & k_four_bit_mask) << k_shift_4) | (v2 >> k_shift_2));

        if (c3 != '=') {
            const auto v3 = decode_char(c3);
            if (v3 < 0) {
                return false;
            }
            out += static_cast<char>(((v2 & k_two_bit_mask) << k_shift_6) | v3);
        }
    } else if (c3 != '=') {
        // A padding character in the third position requires the fourth to also
        // be padding; sequences such as "AA=A" are malformed and must be
        // rejected rather than silently decoded as if they were "AA==".
        return false;
    }

    return true;
}
} // namespace

std::string base64_encode_with(const std::string& input, const std::array<char, 64>& alphabet,
                               bool use_padding) {
    const auto output_size = ((input.size() + 2) / 3) * 4;
    if (output_size > ResourceLimits::max_string_size) {
        throw std::length_error{"base64 output exceeds maximum string size"};
    }

    std::string out{};
    out.reserve(output_size);

    const auto len = input.size();

    for (std::size_t i{0}; i < len; i += 3) {
        const std::array<std::uint8_t, 3> bytes = {
            static_cast<std::uint8_t>(input[i]),
            (i + 1 < len) ? static_cast<std::uint8_t>(input[i + 1]) : std::uint8_t{0},
            (i + 2 < len) ? static_cast<std::uint8_t>(input[i + 2]) : std::uint8_t{0},
        };

        encode_triplet(out, bytes, len - i, alphabet, use_padding);
    }

    return out;
}

std::optional<std::string> base64_decode_with(const std::string& input,
                                              Base64DecodeFn decode_char) {
    std::string out{};
    const auto padding_needed = (4 - (input.size() % 4)) % 4;
    const auto padded_size = input.size() + padding_needed;
    out.reserve((padded_size / 4) * 3);

    auto char_at = [&](std::size_t i) -> char {
        return i < input.size() ? input[i] : '=';
    };

    for (std::size_t i{0}; i < padded_size; i += 4) {
        if (!decode_quad(out, char_at(i), char_at(i + 1), char_at(i + 2), char_at(i + 3),
                         decode_char)) {
            return std::nullopt;
        }
    }

    return std::move(out);
}

} // namespace luma
