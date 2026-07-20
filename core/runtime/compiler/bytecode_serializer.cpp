#include "runtime/compiler/bytecode_serializer.hpp"

#include <bit>
#include <concepts>
#include <cstring>
#include <fstream>

#include "common/hash.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/verifier.hpp"

// Bytecode serialization format:
//   Header: magic bytes (4), format version (u32), flags (u32),
//           source hash (u64), timestamp (u64)
//   Function count (u32): total number of functions including the top-level
//   Functions (repeated):
//     name (length-prefixed string), arity (u16), required_arity (u16),
//     upvalue_count (u16), flags byte (is_main | is_test | is_verified),
//     param names, local names, local mutable flags, upvalue descriptors,
//     chunk (code bytes, constants pool, source map, names)
//   The first function is always the top-level entry point.

namespace luma {

// ─── Writer Implementation ───

void BytecodeSerializer::Writer::write_f64(double v) {
    write_int(std::bit_cast<std::uint64_t>(v));
}

void BytecodeSerializer::Writer::write_bytes(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    buffer_.insert(buffer_.end(), bytes, bytes + size);
}

void BytecodeSerializer::Writer::write_string(std::string_view s) {
    write_u32(static_cast<std::uint32_t>(s.size()));
    write_bytes(s.data(), s.size());
}

// ─── Reader Implementation ───

double BytecodeSerializer::Reader::read_f64() {
    return std::bit_cast<double>(read_int<std::uint64_t>());
}

std::string BytecodeSerializer::Reader::read_string() {
    auto len = read_u32();
    if (error_ || !has_remaining(len)) {
        error_ = true;
        return {};
    }
    std::string s(reinterpret_cast<const char*>(data_ + pos_), len);
    pos_ += len;
    return s;
}

void BytecodeSerializer::Reader::read_bytes(void* dest, std::size_t size) {
    if (!has_remaining(size)) {
        error_ = true;
        return;
    }
    std::memcpy(dest, data_ + pos_, size);
    pos_ += size;
}

// ─── Value Serialization ───

// Value type tags for serialization.
enum class SerializedValueType : std::uint8_t {
    Null = 0,
    Bool = 1,
    Integer = 2,
    Number = 3,
    String = 4,
    // Compound types are not stored in constant pools currently.
};

void BytecodeSerializer::serialize_value(Writer& w, const Value& val) {
    // null is a real serialised type; the identical fallback below intentionally
    // encodes unsupported compound types as null in the constant pool.
    // NOLINTNEXTLINE(bugprone-branch-clone)
    if (val.is_null()) {
        w.write_u8(static_cast<std::uint8_t>(SerializedValueType::Null));
    } else if (val.is_bool()) {
        w.write_u8(static_cast<std::uint8_t>(SerializedValueType::Bool));
        w.write_u8(val.as_bool() ? 1 : 0);
    } else if (val.is_integer()) {
        w.write_u8(static_cast<std::uint8_t>(SerializedValueType::Integer));
        w.write_i64(val.as_integer());
    } else if (val.is_number()) {
        w.write_u8(static_cast<std::uint8_t>(SerializedValueType::Number));
        w.write_f64(val.as_number());
    } else if (val.is_string()) {
        w.write_u8(static_cast<std::uint8_t>(SerializedValueType::String));
        w.write_string(val.as_string());
    } else {
        // Fallback: store as null (compound types in constant pool are rare).
        w.write_u8(static_cast<std::uint8_t>(SerializedValueType::Null));
    }
}

std::optional<Value> BytecodeSerializer::deserialize_value(Reader& r) {
    auto type = static_cast<SerializedValueType>(r.read_u8());
    if (r.error()) {
        return std::nullopt;
    }

    switch (type) {
        case SerializedValueType::Null:
            return Value{NullValue{}};
        case SerializedValueType::Bool:
            return Value{r.read_u8() != 0};
        case SerializedValueType::Integer:
            return Value{r.read_i64()};
        case SerializedValueType::Number:
            return Value{r.read_f64()};
        case SerializedValueType::String:
            return Value{r.read_string()};
        default:
            return std::nullopt;
    }
}

// ─── Chunk Serialization ───

void BytecodeSerializer::serialize_chunk(Writer& w, const Chunk& chunk) {
    // Code bytes.
    w.write_u32(static_cast<std::uint32_t>(chunk.code.size()));
    w.write_bytes(chunk.code.data(), chunk.code.size());

    // Constants.
    w.write_u16(static_cast<std::uint16_t>(chunk.constants.size()));
    for (const auto& c : chunk.constants) {
        serialize_value(w, c);
    }

    // Source map.
    w.write_u32(static_cast<std::uint32_t>(chunk.source_map.size()));
    for (const auto& [offset, loc] : chunk.source_map) {
        w.write_u32(static_cast<std::uint32_t>(offset));
        w.write_u32(static_cast<std::uint32_t>(loc.file_id));
        w.write_u32(static_cast<std::uint32_t>(loc.line));
        w.write_u32(static_cast<std::uint32_t>(loc.column));
    }

    // Names.
    w.write_u16(static_cast<std::uint16_t>(chunk.names.size()));
    for (const auto& name : chunk.names) {
        w.write_string(name);
    }
}

namespace {

// Deserialize a length-prefixed array: read an element count of width CountT,
// accept it via `within_limit` (each section's own bounds/limit policy), reserve
// capacity, then read each element via `read_element` (which returns false to
// abort). Centralizing the security-critical count → validate → reserve shape
// keeps the allocation guard identical across every section, so a hardening fix
// cannot silently lag in one block. The wire format is unchanged.
template <std::unsigned_integral CountT, typename Container, typename LimitFn,
          typename ReadElementFn>
[[nodiscard]] bool read_bounded_array(BytecodeSerializer::Reader& r, Container& out,
                                      LimitFn within_limit, ReadElementFn read_element) {
    const CountT count = r.read_int<CountT>();
    if (r.error() || !within_limit(count)) {
        return false;
    }
    out.reserve(count);
    for (CountT i = 0; i < count; ++i) {
        if (!read_element()) {
            return false;
        }
    }
    return true;
}

} // namespace

bool BytecodeSerializer::deserialize_chunk(Reader& r, Chunk& chunk) {
    // Code bytes.
    auto code_size = r.read_u32();
    // Reject before allocating: the declared size must not exceed the sanity cap
    // (10 MB) and must not exceed the bytes actually left in the stream, so a
    // tiny corrupt or crafted .lumc cannot trigger a large speculative
    // allocation.
    if (r.error() || code_size > SerializerLimits::k_max_code_section_bytes ||
        !r.has_remaining(code_size)) {
        return false;
    }
    chunk.code.resize(code_size);
    r.read_bytes(chunk.code.data(), code_size);

    // Constants.
    if (!read_bounded_array<std::uint16_t>(
            r, chunk.constants,
            [](std::uint16_t count) { return count <= CompilerLimits::k_max_constants; },
            [&] {
                auto val = deserialize_value(r);
                if (!val || r.error()) {
                    return false;
                }
                chunk.constants.push_back(std::move(*val));
                return true;
            })) {
        return false;
    }

    // Source map.
    // Each source-map entry occupies a fixed number of bytes in the stream, so a
    // count larger than the remaining input cannot be valid. Reject it before
    // reserving to prevent a corrupt or adversarial .lumc file from triggering an
    // unbounded allocation (out-of-memory).
    if (!read_bounded_array<std::uint32_t>(
            r, chunk.source_map,
            [&](std::uint32_t count) {
                return r.has_remaining(static_cast<std::size_t>(count) *
                                       SerializerLimits::k_source_map_entry_bytes);
            },
            [&] {
                auto offset = r.read_u32();
                auto file_id = static_cast<int>(r.read_u32());
                auto line = static_cast<int>(r.read_u32());
                auto column = static_cast<int>(r.read_u32());
                if (r.error()) {
                    return false;
                }
                chunk.source_map.append(
                    offset, SourceLocation{.file_id = file_id, .line = line, .column = column});
                return true;
            })) {
        return false;
    }

    // Names.
    if (!read_bounded_array<std::uint16_t>(
            r, chunk.names,
            [&](std::uint16_t count) {
                return r.has_remaining(static_cast<std::size_t>(count) *
                                       SerializerLimits::k_string_length_prefix_bytes);
            },
            [&] {
                auto name = r.read_string();
                if (r.error()) {
                    return false;
                }
                // Re-intern rather than raw-append so the dedup index stays
                // consistent. Well-formed chunks contain only unique names
                // (add_name dedups at compile time), so this reproduces the
                // serialized table exactly.
                (void)chunk.names.add(name);
                return true;
            })) {
        return false;
    }

    return true;
}

// ─── Function Serialization ───

// Bit flags for the per-function flags byte in the serialized format.
enum class SerializedFunctionFlags : std::uint8_t {
    is_main = 0x01,
    is_test = 0x02,
    is_verified = 0x04,
};

// Bit flags for the per-upvalue flags byte in the serialized format.
enum class SerializedUpvalueFlags : std::uint8_t {
    is_local = 0x01,
    is_mutable = 0x02,
};

void BytecodeSerializer::serialize_function(Writer& w, const CompiledFunction& func) {
    w.write_string(func.name);
    w.write_u16(static_cast<std::uint16_t>(func.arity));
    w.write_u16(static_cast<std::uint16_t>(func.required_arity));
    w.write_u16(static_cast<std::uint16_t>(func.upvalue_count));

    // Flags byte: bit 0 = is_main, bit 1 = is_test, bit 2 = is_verified.
    std::uint8_t flags = 0;
    if (func.is_main) {
        flags |= static_cast<std::uint8_t>(SerializedFunctionFlags::is_main);
    }
    if (func.is_test) {
        flags |= static_cast<std::uint8_t>(SerializedFunctionFlags::is_test);
    }
    if (func.is_verified()) {
        flags |= static_cast<std::uint8_t>(SerializedFunctionFlags::is_verified);
    }
    w.write_u8(flags);

    // Param names.
    w.write_u16(static_cast<std::uint16_t>(func.param_names.size()));
    for (const auto& name : func.param_names) {
        w.write_string(name);
    }

    // Local names.
    w.write_u16(static_cast<std::uint16_t>(func.debug_info.local_names.size()));
    for (const auto& name : func.debug_info.local_names) {
        w.write_string(name);
    }

    // Local mutable flags.
    w.write_u16(static_cast<std::uint16_t>(func.debug_info.local_mutable.size()));
    for (const bool m : func.debug_info.local_mutable) {
        w.write_u8(m ? 1 : 0);
    }

    // Upvalue descriptors.
    w.write_u16(static_cast<std::uint16_t>(func.upvalues.size()));
    for (const auto& uv : func.upvalues) {
        w.write_u16(uv.index);
        std::uint8_t uv_flags =
            (uv.is_local ? static_cast<std::uint8_t>(SerializedUpvalueFlags::is_local) : 0) |
            (uv.is_mutable ? static_cast<std::uint8_t>(SerializedUpvalueFlags::is_mutable) : 0);
        w.write_u8(uv_flags);
    }

    // Chunk.
    serialize_chunk(w, func.chunk());
}

std::optional<CompiledFunction> BytecodeSerializer::deserialize_function(Reader& r) {
    CompiledFunction func;

    func.name = r.read_string();
    func.arity = static_cast<int>(r.read_u16());
    func.required_arity = static_cast<int>(r.read_u16());
    // Read upvalue_count — validated below against the explicit upvalue
    // descriptor array, which must contain exactly this many entries.
    func.upvalue_count = static_cast<int>(r.read_u16());

    auto flags = r.read_u8();
    func.is_main = (flags & static_cast<std::uint8_t>(SerializedFunctionFlags::is_main)) != 0;
    func.is_test = (flags & static_cast<std::uint8_t>(SerializedFunctionFlags::is_test)) != 0;
    func.set_is_verified(
        (flags & static_cast<std::uint8_t>(SerializedFunctionFlags::is_verified)) != 0);

    if (r.error()) {
        return std::nullopt;
    }

    // The compiler caps arity, required_arity, and upvalue_count at these limits
    // when it writes the function; a value beyond them means a corrupt or crafted
    // .lumc.  Rejecting here bounds the SmallVector<Value>(arity) argument frame
    // and the upvalue/upvalue_cell vectors the VM builds from these counts,
    // matching the schema-scalar caps the deserializer already enforces on the
    // constant and function tables above.
    if (func.arity > CompilerLimits::k_max_arguments ||
        func.required_arity > CompilerLimits::k_max_arguments ||
        func.upvalue_count > CompilerLimits::k_max_upvalues) {
        return std::nullopt;
    }

    // Param names.
    auto param_count = r.read_u16();
    if (r.error() || !r.has_remaining(static_cast<std::size_t>(param_count) *
                                      SerializerLimits::k_string_length_prefix_bytes)) {
        return std::nullopt;
    }
    // A function's parameter-name table must never exceed its arity.  The VM binds
    // named arguments into an arity-sized frame using the positional indices this
    // table produces (build_param_name_index → param_name_index, consumed by
    // bind_named_args in vm_dispatch_control_flow.cpp), so a param_count > arity
    // from a corrupt or crafted .lumc would let a named argument write past that
    // frame.  Named functions emit exactly `arity` names; lambdas legitimately
    // emit none while still having arity > 0, so any count up to arity is valid.
    if (static_cast<int>(param_count) > func.arity) {
        return std::nullopt;
    }
    func.param_names.reserve(param_count);
    for (std::uint16_t i = 0; i < param_count; ++i) {
        func.param_names.push_back(r.read_string());
    }

    // Local names.
    auto local_count = r.read_u16();
    if (r.error() || !r.has_remaining(static_cast<std::size_t>(local_count) *
                                      SerializerLimits::k_string_length_prefix_bytes)) {
        return std::nullopt;
    }
    func.debug_info.local_names.reserve(local_count);
    for (std::uint16_t i = 0; i < local_count; ++i) {
        func.debug_info.local_names.push_back(r.read_string());
    }

    // Local mutable flags.
    auto mutable_count = r.read_u16();
    if (r.error() || !r.has_remaining(static_cast<std::size_t>(mutable_count) *
                                      SerializerLimits::k_local_mutable_entry_bytes)) {
        return std::nullopt;
    }
    func.debug_info.local_mutable.reserve(mutable_count);
    for (std::uint16_t i = 0; i < mutable_count; ++i) {
        func.debug_info.local_mutable.push_back(r.read_u8() != 0);
    }

    // Upvalue descriptors.
    auto uv_count = r.read_u16();
    // Reject an over-limit count before the reserve/build loop below, matching
    // the upvalue_count cap above; the descriptor array is later required to
    // equal upvalue_count, so a valid file never trips this.
    if (uv_count > CompilerLimits::k_max_upvalues) {
        return std::nullopt;
    }
    if (r.error() || !r.has_remaining(static_cast<std::size_t>(uv_count) *
                                      SerializerLimits::k_upvalue_entry_bytes)) {
        return std::nullopt;
    }
    func.upvalues.reserve(uv_count);
    for (std::uint16_t i = 0; i < uv_count; ++i) {
        CompiledFunction::Upvalue uv;
        uv.index = r.read_u16();
        auto uv_flags = r.read_u8();
        uv.is_local = (uv_flags & static_cast<std::uint8_t>(SerializedUpvalueFlags::is_local)) != 0;
        uv.is_mutable =
            (uv_flags & static_cast<std::uint8_t>(SerializedUpvalueFlags::is_mutable)) != 0;
        func.upvalues.push_back(uv);
    }

    if (r.error()) {
        return std::nullopt;
    }

    // The scalar upvalue_count and the explicit descriptor array must agree.
    // The compiler always emits them in lockstep; a mismatch means a corrupt or
    // crafted .lumc.  Rejecting here keeps the verifier's upvalue-index bound
    // (which uses upvalue_count) consistent with the array the VM actually
    // indexes (upvalues), so a verified function can never read past it.
    if (func.upvalue_count != static_cast<int>(func.upvalues.size())) {
        return std::nullopt;
    }

    // Chunk.
    if (!deserialize_chunk(r, func.mutable_chunk())) {
        return std::nullopt;
    }

    func.build_param_name_index();

    return func;
}

// ─── Top-Level Serialization ───

std::vector<std::uint8_t>
BytecodeSerializer::serialize(const CompiledFunction& top_level,
                              const std::vector<CompiledFunction>& functions,
                              std::uint64_t source_hash, std::uint64_t timestamp) {
    Writer w;

    // Header.
    w.write_bytes(k_bytecode_magic, 4);
    w.write_u32(k_bytecode_format_version);
    w.write_u32(0); // flags
    w.write_u64(source_hash);
    w.write_u64(timestamp);

    // Function count (top_level + functions).
    w.write_u32(static_cast<std::uint32_t>(1 + functions.size()));

    // Top-level function first.
    serialize_function(w, top_level);

    // Remaining functions.
    for (const auto& func : functions) {
        serialize_function(w, func);
    }

    return w.data();
}

DeserializeResult BytecodeSerializer::deserialize(const std::vector<std::uint8_t>& data) {
    if (data.size() < sizeof(BytecodeHeader)) {
        return {.bytecode = {},
                .error = DeserializeError::TooSmall,
                .detail = "bytecode_serializer: buffer smaller than header"};
    }

    Reader r(data.data(), data.size());

    // Read and validate header.
    DeserializedBytecode result;
    r.read_bytes(result.header.magic, 4);
    result.header.version = r.read_u32();
    result.header.flags = r.read_u32();
    result.header.source_hash = r.read_u64();
    result.header.timestamp = r.read_u64();

    if (r.error()) {
        return {.bytecode = {},
                .error = DeserializeError::ReadError,
                .detail = "bytecode_serializer: header read failed"};
    }

    // Validate magic.
    if (std::memcmp(result.header.magic, k_bytecode_magic, 4) != 0) {
        return {.bytecode = {},
                .error = DeserializeError::BadMagic,
                .detail = "bytecode_serializer: invalid magic bytes"};
    }

    // Validate version.
    if (result.header.version != k_bytecode_format_version) {
        return {.bytecode = {},
                .error = DeserializeError::VersionMismatch,
                .detail = "bytecode_serializer: expected version " +
                          std::to_string(k_bytecode_format_version) + ", got " +
                          std::to_string(result.header.version)};
    }

    // Read functions.
    auto func_count = r.read_u32();
    if (r.error() || func_count == 0 || func_count > SerializerLimits::k_max_function_count) {
        return {.bytecode = {},
                .error = DeserializeError::BadFunctionCount,
                .detail =
                    "bytecode_serializer: invalid function count " + std::to_string(func_count)};
    }

    // First function is the top-level.
    auto top = deserialize_function(r);
    if (!top) {
        return {.bytecode = {},
                .error = DeserializeError::CorruptFunction,
                .detail = "bytecode_serializer: corrupt top-level function"};
    }
    result.top_level = std::move(*top);

    // Remaining functions.
    //
    // Each remaining function occupies at least k_min_function_bytes in the
    // stream, so a func_count larger than the remaining input can possibly hold
    // cannot be valid. Reject it before reserving, mirroring the source-map
    // (map_count) and name-table (name_count) guards above: k_max_function_count
    // bounds func_count but still permits a tiny corrupt or adversarial .lumc
    // file to force a multi-megabyte speculative allocation (out-of-memory).
    if (r.error() || !r.has_remaining(static_cast<std::size_t>(func_count - 1) *
                                      SerializerLimits::k_min_function_bytes)) {
        return {.bytecode = {},
                .error = DeserializeError::BadFunctionCount,
                .detail = "bytecode_serializer: function count " + std::to_string(func_count) +
                          " exceeds remaining input"};
    }
    result.functions.reserve(func_count - 1);
    for (std::uint32_t i = 1; i < func_count; ++i) {
        auto func = deserialize_function(r);
        if (!func) {
            return {.bytecode = {},
                    .error = DeserializeError::CorruptFunction,
                    .detail = "bytecode_serializer: corrupt function " + std::to_string(i)};
        }
        result.functions.push_back(std::move(*func));
    }

    // Re-verify every function's bytecode and derive is_verified from OUR own
    // verifier rather than trusting the serialized flag.  The is_verified bit
    // makes the VM skip bounds checks on the hot path (local slots, upvalue and
    // constant indices, stack depth); a crafted or corrupted .lumc file with
    // the bit set but out-of-bounds operands could otherwise drive the VM into
    // out-of-bounds reads/writes.  Verifying here means the fast path is only
    // taken for bytecode we have just proven safe; anything that fails keeps
    // is_verified=false so the VM's runtime bounds checks remain active.  The
    // file is not rejected — unverified bytecode still runs correctly, just
    // without the optimisation.
    {
        BytecodeVerifier verifier;

        result.top_level.set_is_verified(verifier.verify(result.top_level).empty());

        for (auto& func : result.functions) {
            func.set_is_verified(verifier.verify(func).empty());
        }
    }

    return {.bytecode = std::move(result), .error = DeserializeError::None, .detail = {}};
}

// ─── File I/O ───

bool BytecodeSerializer::write_file(const std::filesystem::path& path,
                                    const CompiledFunction& top_level,
                                    const std::vector<CompiledFunction>& functions,
                                    std::uint64_t source_hash, std::uint64_t timestamp) {
    auto data = serialize(top_level, functions, source_hash, timestamp);

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    return file.good();
}

DeserializeResult BytecodeSerializer::read_file(const std::filesystem::path& path,
                                                std::uint64_t expected_source_hash) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {.bytecode = {},
                .error = DeserializeError::FileNotFound,
                .detail = "bytecode_serializer: file not found: " + path.string()};
    }

    auto size = file.tellg();
    if (size <= 0 || static_cast<std::size_t>(size) > SerializerLimits::k_max_bytecode_file_size) {
        return {.bytecode = {},
                .error = DeserializeError::FileTooLarge,
                .detail = "bytecode_serializer: file too large (" +
                          std::to_string(static_cast<long long>(size)) + " bytes)"};
    }

    file.seekg(0);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    if (!file.good()) {
        return {.bytecode = {},
                .error = DeserializeError::FileReadFailed,
                .detail = "bytecode_serializer: file read failed: " + path.string()};
    }

    auto result = deserialize(data);
    if (!result) {
        return result;
    }

    // If an expected hash was provided, verify it matches.
    if (expected_source_hash != 0 && result->header.source_hash != expected_source_hash) {
        return {.bytecode = {},
                .error = DeserializeError::HashMismatch,
                .detail = "bytecode_serializer: hash mismatch, expected " +
                          std::to_string(expected_source_hash) + ", got " +
                          std::to_string(result->header.source_hash)};
    }

    return result;
}

// ─── Hashing ───

std::uint64_t BytecodeSerializer::hash_source(std::string_view source) {
    return fnv1a_hash(source);
}

std::filesystem::path BytecodeSerializer::cache_path_for(const std::filesystem::path& source) {
    auto result = source;
    result.replace_extension(".lumc");
    return result;
}

} // namespace luma
