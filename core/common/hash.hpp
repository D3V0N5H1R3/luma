#ifndef LUMA_COMMON_HASH_HPP
#define LUMA_COMMON_HASH_HPP

#include <cstdint>
#include <string_view>

namespace luma {

// FNV-1a 64-bit hash — fast, non-cryptographic hash function.
//
// Use cases:
//   - Internal hash tables and cache keys (string interning, LRU caches)
//   - Content-based change detection (file hashing for incremental analysis)
//   - General-purpose hashing where speed matters more than distribution quality
//
// Do NOT use for:
//   - Cryptographic purposes (use SHA-256 or similar)
//   - Checksums where CRC32 is expected by an external protocol
//
// See also: crc32.hpp for polynomial-based checksums (Hash/Compression stdlib modules).
[[nodiscard]] inline std::uint64_t fnv1a_hash(std::string_view data) noexcept {
    constexpr std::uint64_t k_fnv_offset_basis = 14695981039346656037ULL;
    constexpr std::uint64_t k_fnv_prime = 1099511628211ULL;

    std::uint64_t hash = k_fnv_offset_basis;

    for (const char c : data) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= k_fnv_prime;
    }

    return hash;
}

} // namespace luma

#endif // LUMA_COMMON_HASH_HPP
