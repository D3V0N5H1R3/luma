#ifndef LUMA_COMMON_BYTE_UTILS_HPP
#define LUMA_COMMON_BYTE_UTILS_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace luma {

// Read/write multi-byte integers in big-endian format.
// Used for bytecode serialization, compilation, and verification.

// Read a big-endian unsigned integer of arbitrary width from a raw buffer.
// The buffer must hold at least sizeof(T) bytes; bounds checking is the
// caller's responsibility. This is the single source of the big-endian read
// loop shared by the bytecode serializer's Reader.
template <std::unsigned_integral T>
[[nodiscard]] inline constexpr T read_int_be(const std::uint8_t* data) {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value = static_cast<T>((value << 8) | data[i]);
    }
    return value;
}

// Append a big-endian unsigned integer of arbitrary width to a byte vector.
// This is the single source of the big-endian write loop shared by the
// bytecode serializer's Writer.
template <std::unsigned_integral T>
inline void write_int_be(std::vector<std::uint8_t>& buffer, T value) {
    for (int i = static_cast<int>(sizeof(T)) - 1; i >= 0; --i) {
        buffer.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

[[nodiscard]] inline constexpr std::uint16_t read_u16_be(const std::uint8_t* data) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8) |
                                      static_cast<std::uint16_t>(data[1]));
}

[[nodiscard]] inline constexpr std::uint32_t read_u32_be(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24) |
           (static_cast<std::uint32_t>(data[1]) << 16) |
           (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
}

// Push a u16 value in big-endian byte order onto a byte vector.
inline void write_u16_be(std::vector<std::uint8_t>& buffer, std::uint16_t value) {
    buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

// Push a u32 value in big-endian byte order onto a byte vector.
inline void write_u32_be(std::vector<std::uint8_t>& buffer, std::uint32_t value) {
    buffer.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

// Write a u16 value in big-endian byte order at an existing location.
inline constexpr void patch_u16_be(std::uint8_t* dest, std::uint16_t value) {
    dest[0] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    dest[1] = static_cast<std::uint8_t>(value & 0xFF);
}

// Write a u32 value in big-endian byte order at an existing location.
inline constexpr void patch_u32_be(std::uint8_t* dest, std::uint32_t value) {
    dest[0] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    dest[1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    dest[2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    dest[3] = static_cast<std::uint8_t>(value & 0xFF);
}

// --- Little-endian variants ---
// Some external formats store fixed-width integers least-significant byte
// first (e.g. the gzip trailer's CRC32 and ISIZE, RFC 1952 §2.3.1).  These
// mirror the big-endian helpers above so both endiannesses live in one audited
// place.  Reads take a raw pointer, valid for any contiguous byte buffer;
// writes are generic over the destination container's push_back so they serve
// both the vector<uint8_t> bytecode path and the string-based gzip codec.

// Read a little-endian unsigned integer of arbitrary width from a raw buffer.
// The buffer must hold at least sizeof(T) bytes; bounds checking is the
// caller's responsibility.
template <std::unsigned_integral T>
[[nodiscard]] inline constexpr T read_int_le(const std::uint8_t* data) {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value = static_cast<T>(value | (static_cast<T>(data[i]) << (8 * i)));
    }
    return value;
}

// Append a little-endian unsigned integer of arbitrary width to a byte sink.
// The sink must expose a byte-sized value_type and push_back (e.g.
// std::vector<std::uint8_t> or std::string).
template <typename ByteSink, std::unsigned_integral T>
inline void write_int_le(ByteSink& sink, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        sink.push_back(static_cast<typename ByteSink::value_type>((value >> (8 * i)) & 0xFF));
    }
}

[[nodiscard]] inline constexpr std::uint32_t read_u32_le(const std::uint8_t* data) {
    return read_int_le<std::uint32_t>(data);
}

// Append a u32 value in little-endian byte order onto a byte sink.
template <typename ByteSink> inline void write_u32_le(ByteSink& sink, std::uint32_t value) {
    write_int_le(sink, value);
}

// Bounds-checked single-byte read with default fallback.
// Used for safe bytecode reading in optimizer and verifier.
[[nodiscard]] inline constexpr std::uint8_t read_u8_checked(const std::vector<std::uint8_t>& code,
                                                            std::size_t offset,
                                                            std::uint8_t default_val = 0) {
    return (offset < code.size()) ? code[offset] : default_val;
}

// Return true if code[offset .. offset+size) is within bounds.
// Written to avoid unsigned overflow: checks offset first, then compares
// size against the remaining capacity.
[[nodiscard]] inline bool in_bounds(const std::vector<std::uint8_t>& code, std::size_t offset,
                                    std::size_t size) {
    return offset <= code.size() && size <= code.size() - offset;
}

} // namespace luma

#endif // LUMA_COMMON_BYTE_UTILS_HPP
