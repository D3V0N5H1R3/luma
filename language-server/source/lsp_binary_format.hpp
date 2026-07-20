#ifndef LUMA_LSP_BINARY_FORMAT_HPP
#define LUMA_LSP_BINARY_FORMAT_HPP

#include <array>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace luma::lsp {
namespace binary_format {

// ═══════════════════════════════════════════════════════════
// Binary serialisation helpers for the persisted index format.
//
// All multi-byte integers are stored in big-endian order.
// Strings are length-prefixed with a u32 byte count.
// ═══════════════════════════════════════════════════════════

// Sanity limits for data read from persisted binary files.

// Maximum string size (10 MB) — prevents out-of-memory from malformed persisted index files.
// Typical workspace source files are well under 1 MB.
inline constexpr std::size_t k_max_string_size = 10'000'000;

// Maximum items per serialised collection (100K) — a large workspace typically has <10K files.
// This guards against corrupt data causing unbounded allocation.
inline constexpr std::size_t k_max_collection_count = 100'000;

// ─── Generic big-endian helpers ───

// Generic big-endian write for unsigned integer types.
template <typename T>
    requires std::is_unsigned_v<T>
inline void write_big_endian(std::ostream& out, T value) {
    constexpr auto byte_count = sizeof(T);
    std::array<char, byte_count> bytes{};
    for (std::size_t i = 0; i < byte_count; ++i) {
        bytes[i] = static_cast<char>((value >> (8 * (byte_count - 1 - i))) & 0xFF);
    }
    out.write(bytes.data(), byte_count);
}

// Generic big-endian read for unsigned integer types.
template <typename T>
    requires std::is_unsigned_v<T>
[[nodiscard]] inline T read_big_endian(std::istream& in) {
    constexpr auto byte_count = sizeof(T);
    T value = 0;
    for (std::size_t i = 0; i < byte_count; ++i) {
        char byte = 0;
        in.read(&byte, 1);
        value = (value << 8) | static_cast<std::uint8_t>(byte);
    }
    return value;
}

// ─── Writers ───

inline void write_u32(std::ostream& out, std::uint32_t v) {
    write_big_endian(out, v);
}

inline void write_u64(std::ostream& out, std::uint64_t v) {
    write_big_endian(out, v);
}

inline void write_string(std::ostream& out, std::string_view s) {
    write_u32(out, static_cast<std::uint32_t>(s.size()));
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

inline void write_string_vec(std::ostream& out, const std::vector<std::string>& vec) {
    write_u32(out, static_cast<std::uint32_t>(vec.size()));
    for (const auto& s : vec) {
        write_string(out, s);
    }
}

// ─── Readers ───

[[nodiscard]] inline std::uint32_t read_u32(std::istream& in) {
    return read_big_endian<std::uint32_t>(in);
}

[[nodiscard]] inline std::uint64_t read_u64(std::istream& in) {
    return read_big_endian<std::uint64_t>(in);
}

[[nodiscard]] inline std::string read_string(std::istream& in) {
    auto len = read_u32(in);
    if (len > k_max_string_size) {
        // Mark the stream as failed so the caller's good() check detects the
        // malformed length instead of silently treating it as an empty string.
        in.setstate(std::ios::failbit);
        return {};
    }
    std::string s(len, '\0');
    in.read(s.data(), static_cast<std::streamsize>(len));
    return s;
}

[[nodiscard]] inline std::vector<std::string> read_string_vec(std::istream& in) {
    auto count = read_u32(in);
    if (count > k_max_collection_count) {
        // Mark the stream as failed so the caller's good() check detects the
        // malformed count instead of silently returning a short vector.
        in.setstate(std::ios::failbit);
        return {};
    }
    std::vector<std::string> result;
    result.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        result.push_back(read_string(in));
    }
    return result;
}

} // namespace binary_format

// Simple FNV-1a hash for binary data integrity checking.
[[nodiscard]] inline constexpr std::uint32_t fnv1a_hash(const char* data, std::size_t length) {
    std::uint32_t hash = 0x811c9dc5u;
    for (std::size_t i = 0; i < length; ++i) {
        hash ^= static_cast<std::uint8_t>(data[i]);
        hash *= 0x01000193u;
    }
    return hash;
}

} // namespace luma::lsp

#endif // LUMA_LSP_BINARY_FORMAT_HPP
