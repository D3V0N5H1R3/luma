#ifndef LUMA_COMMON_CRC32_HPP
#define LUMA_COMMON_CRC32_HPP

// CRC32 (ISO 3309 / ITU-T V.42) — polynomial-based checksum.
//
// Use cases:
//   - Luma stdlib Hash module (exposed to user programs as Hash.crc32)
//   - Luma stdlib Compression module (integrity verification)
//   - Data integrity checking where CRC32 is required by a protocol/format
//
// Do NOT use for:
//   - Internal hash tables or cache keys (use fnv1a_hash from hash.hpp — faster)
//   - Cryptographic purposes (CRC32 is trivially reversible)
//
// See also: hash.hpp for FNV-1a (internal hash tables and content hashing).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace luma {

namespace detail::crc32 {

// Build the 256-entry CRC32 lookup table at compile time.
// Uses the standard IEEE 802.3 (ISO 3309 / ITU-T V.42) polynomial
// 0xEDB88320, which is the bit-reversed representation of the
// generator polynomial x^32 + x^26 + x^23 + x^22 + x^16 + x^12 +
// x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x + 1.
constexpr std::array<uint32_t, 256> make_table() {
    // Reversed polynomial for CRC-32/ISO-HDLC.
    constexpr std::uint32_t k_polynomial = 0xEDB88320;

    std::array<uint32_t, 256> table{};

    for (uint32_t i{0}; i < 256; ++i) {
        uint32_t crc{i};

        for (int j{0}; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ k_polynomial;
            } else {
                crc >>= 1;
            }
        }

        table[i] = crc;
    }

    return table;
}

constexpr auto table = make_table();

// Initial CRC register value and final XOR mask (standard CRC-32/ISO-HDLC).
constexpr std::uint32_t k_init = 0xFFFFFFFF;
constexpr std::uint32_t k_xor_out = 0xFFFFFFFF;

} // namespace detail::crc32

// CRC32 checksum — matches fnv1a_hash() naming convention.
// Takes a string_view so callers can hash string literals or substrings without
// materialising a std::string.  See file-level comment for use cases.
[[nodiscard]] inline std::uint32_t crc32_hash(std::string_view data) {
    std::uint32_t crc{detail::crc32::k_init};

    for (const auto byte : data) {
        crc = detail::crc32::table[(crc ^ static_cast<std::uint8_t>(byte)) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ detail::crc32::k_xor_out;
}

} // namespace luma

#endif // LUMA_COMMON_CRC32_HPP
