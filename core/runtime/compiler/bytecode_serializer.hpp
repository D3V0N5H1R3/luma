#ifndef LUMA_COMPILER_BYTECODE_SERIALIZER_HPP
#define LUMA_COMPILER_BYTECODE_SERIALIZER_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Bytecode Serializer — Persistent .lumc File Format
// ─────────────────────────────────────────────────────────────────────────────
// Serializes and deserializes compiled bytecode to/from disk, enabling:
//   - Near-instant startup for unchanged files (skip lexing/parsing/compiling).
//   - Persistent compilation cache across process restarts.
//   - Distribution of pre-compiled Luma modules.
//
// File format (.lumc):
//   Header (24 bytes):
//     - Magic:       "LUMC" (4 bytes)
//     - Version:     u32 (format version, currently 2)
//     - Flags:       u32 (reserved)
//     - Source hash: u64 (FNV-1a hash of the source text for invalidation)
//     - Timestamp:   u64 (modification time of source file, for quick checks)
//
//   Function table:
//     - Function count: u32
//     - For each function:
//       - Name length: u32, name bytes
//       - Arity: u16
//       - Required arity: u16
//       - Upvalue count: u16
//       - Flags: u8 (is_main, is_test, is_verified)
//       - Param names: count + strings
//       - Local names: count + strings
//       - Local mutable: count + bools
//       - Upvalue descriptors: count + (index: u16, is_local: u8)
//       - Chunk:
//         - Code: length: u32, bytes
//         - Constants: count: u16, for each: type tag + payload
//         - Source map: entry count: u32, for each: (offset: u32, loc)
//         - Names: count: u16, for each: length: u32, bytes
// ─────────────────────────────────────────────────────────────────────────────

#include <cassert>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "common/byte_utils.hpp"
#include "runtime/compiler/chunk.hpp"

namespace luma {

// Serialized bytecode format version.  Bump this whenever the binary format
// changes incompatibly.
//
// Version migration strategy (RT-23):
//   Currently there is no forward/backward migration path — a version
//   mismatch causes the cached .lumc file to be rejected (DeserializeError::
//   VersionMismatch) and the source is recompiled from scratch.  This is
//   acceptable because .lumc files are a transparent cache, not a
//   distribution format, so recompilation is always possible.  If the
//   format is ever used for distributing pre-compiled modules, a versioned
//   migration layer (or a multi-version reader) should be added here.
//
// Version history:
//   1 → 2: widened each serialized string's length prefix from u16 to u32 so
//          that a string constant or name of 65 536 bytes or more no longer
//          wraps its length prefix and desyncs the rest of the stream.
static constexpr std::uint32_t k_bytecode_format_version = 2;

// Magic bytes at the start of a .lumc file.
static constexpr char k_bytecode_magic[4] = {'L', 'U', 'M', 'C'};

struct BytecodeHeader {
    char magic[4]{'L', 'U', 'M', 'C'};
    std::uint32_t version{k_bytecode_format_version};
    std::uint32_t flags{0};
    std::uint64_t source_hash{0};
    std::uint64_t timestamp{0};
};

// Ensure the header fits in a known bound (exact size depends on alignment/packing).
static_assert(sizeof(BytecodeHeader) <= 32);

// Result of deserialization.
struct DeserializedBytecode {
    BytecodeHeader header;
    CompiledFunction top_level;
    std::vector<CompiledFunction> functions;
};

// Reasons a .lumc deserialization can fail.
enum class DeserializeError {
    None,
    TooSmall,
    ReadError,
    BadMagic,
    VersionMismatch,
    BadFunctionCount,
    CorruptFunction,
    FileNotFound,
    FileTooLarge,
    FileReadFailed,
    HashMismatch,
};

// Typed result from deserialize / read_file with an error code and optional
// human-readable detail string.
struct DeserializeResult {
    std::optional<DeserializedBytecode> bytecode;
    DeserializeError error{DeserializeError::None};
    std::string detail;

    [[nodiscard]] explicit operator bool() const noexcept {
        return bytecode.has_value();
    }

    // These accessors deliberately mirror std::optional's contract: the caller must
    // confirm the result is engaged (via operator bool) before dereferencing.  A debug
    // assert guards misuse, and every call site checks first, so the access is safe.
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    [[nodiscard]] const DeserializedBytecode& operator*() const noexcept {
        assert(bytecode.has_value() && "dereferencing empty DeserializeResult");
        return *bytecode;
    }

    [[nodiscard]] DeserializedBytecode& operator*() noexcept {
        assert(bytecode.has_value() && "dereferencing empty DeserializeResult");
        return *bytecode;
    }

    [[nodiscard]] const DeserializedBytecode* operator->() const noexcept {
        assert(bytecode.has_value() && "dereferencing empty DeserializeResult");
        return &*bytecode;
    }

    [[nodiscard]] DeserializedBytecode* operator->() noexcept {
        assert(bytecode.has_value() && "dereferencing empty DeserializeResult");
        return &*bytecode;
    }

    // NOLINTEND(bugprone-unchecked-optional-access)
};

class BytecodeSerializer {
public:
    // Serialize compiled functions to a byte buffer.
    [[nodiscard]] static std::vector<std::uint8_t>
    serialize(const CompiledFunction& top_level, const std::vector<CompiledFunction>& functions,
              std::uint64_t source_hash, std::uint64_t timestamp = 0);

    // Deserialize from a byte buffer.  Returns a DeserializeResult with a
    // specific error code on failure.
    [[nodiscard]] static DeserializeResult deserialize(const std::vector<std::uint8_t>& data);

    // Write serialized bytecode to a .lumc file.
    // Returns true on success.
    [[nodiscard]] static bool write_file(const std::filesystem::path& path,
                                         const CompiledFunction& top_level,
                                         const std::vector<CompiledFunction>& functions,
                                         std::uint64_t source_hash, std::uint64_t timestamp = 0);

    // Read and deserialize a .lumc file.
    // Returns a DeserializeResult with a specific error code on failure.
    [[nodiscard]] static DeserializeResult read_file(const std::filesystem::path& path,
                                                     std::uint64_t expected_source_hash = 0);

    // Compute a content hash for invalidation.
    [[nodiscard]] static std::uint64_t hash_source(std::string_view source);

    // Get the .lumc path for a given .luma source path.
    [[nodiscard]] static std::filesystem::path cache_path_for(const std::filesystem::path& source);

private:
    // Binary serialization helpers.
    class Writer {
    public:
        // Generic big-endian integer writer for any unsigned integral type.
        template <std::unsigned_integral T> void write_int(T value) {
            write_int_be(buffer_, value);
        }

        void write_u8(std::uint8_t v) {
            write_int(v);
        }

        void write_u16(std::uint16_t v) {
            write_int(v);
        }

        void write_u32(std::uint32_t v) {
            write_int(v);
        }

        void write_u64(std::uint64_t v) {
            write_int(v);
        }

        void write_i64(std::int64_t v) {
            write_int(static_cast<std::uint64_t>(v));
        }

        void write_f64(double v);
        void write_bytes(const void* data, std::size_t size);
        void write_string(std::string_view s);

        [[nodiscard]] const std::vector<std::uint8_t>& data() const {
            return buffer_;
        }

    private:
        std::vector<std::uint8_t> buffer_;
    };

    class Reader {
    public:
        explicit Reader(const std::uint8_t* data, std::size_t size)
            : data_{data}, size_{size}, pos_{0} {}

        [[nodiscard]] bool has_remaining(std::size_t n) const {
            // Overflow-safe form: pos_ <= size_ is an invariant (the cursor never
            // advances past the end), so size_ - pos_ never underflows, whereas
            // pos_ + n could wrap on a 32-bit size_t for a crafted length.
            return n <= size_ - pos_;
        }

        // Generic big-endian integer reader for any unsigned integral type.
        template <std::unsigned_integral T> [[nodiscard]] T read_int() {
            if (!has_remaining(sizeof(T))) {
                error_ = true;
                return 0;
            }
            const T value = read_int_be<T>(data_ + pos_);
            pos_ += sizeof(T);
            return value;
        }

        [[nodiscard]] std::uint8_t read_u8() {
            return read_int<std::uint8_t>();
        }

        [[nodiscard]] std::uint16_t read_u16() {
            return read_int<std::uint16_t>();
        }

        [[nodiscard]] std::uint32_t read_u32() {
            return read_int<std::uint32_t>();
        }

        [[nodiscard]] std::uint64_t read_u64() {
            return read_int<std::uint64_t>();
        }

        [[nodiscard]] std::int64_t read_i64() {
            return static_cast<std::int64_t>(read_int<std::uint64_t>());
        }

        [[nodiscard]] double read_f64();
        [[nodiscard]] std::string read_string();
        void read_bytes(void* dest, std::size_t size);

        [[nodiscard]] bool error() const {
            return error_;
        }

    private:
        const std::uint8_t* data_;
        std::size_t size_;
        std::size_t pos_;
        bool error_{false};
    };

    static void serialize_function(Writer& w, const CompiledFunction& func);
    [[nodiscard]] static std::optional<CompiledFunction> deserialize_function(Reader& r);
    static void serialize_chunk(Writer& w, const Chunk& chunk);
    [[nodiscard]] static bool deserialize_chunk(Reader& r, Chunk& chunk);
    static void serialize_value(Writer& w, const Value& val);
    [[nodiscard]] static std::optional<Value> deserialize_value(Reader& r);
};

} // namespace luma

#endif // LUMA_COMPILER_BYTECODE_SERIALIZER_HPP
