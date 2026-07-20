// Bytecode serializer / deserializer unit tests.
//
// Covers the .lumc round-trip and the deserializer's robustness against
// malformed input. The deserializer is a trust boundary: it decodes arbitrary
// bytes (the .lumc cache and pre-compiled module distribution), so it must
// reject corrupt or adversarial data without crashing or over-allocating.

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "runtime/compiler/bytecode_serializer.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/constant_pool.hpp"
#include "runtime/compiler/name_table.hpp"
#include "runtime/interpreter/value.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Big-endian byte-buffer helpers ───
//
// The .lumc format stores integers big-endian (see BytecodeSerializer::Writer).
// These build a hand-crafted buffer so a test can exercise specific malformed
// fields without depending on the compiler front-end.

static void put_u8(std::vector<std::uint8_t>& b, std::uint8_t v) {
    b.push_back(v);
}

static void put_u16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

static void put_u32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) {
        b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
    }
}

static void put_u64(std::vector<std::uint8_t>& b, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
    }
}

// Builds a minimal valid header followed by a function count of 1.
static void put_header(std::vector<std::uint8_t>& b, std::uint32_t func_count) {
    put_u8(b, 'L');
    put_u8(b, 'U');
    put_u8(b, 'M');
    put_u8(b, 'C');
    put_u32(b, k_bytecode_format_version); // version
    put_u32(b, 0);                         // flags
    put_u64(b, 0);                         // source hash
    put_u64(b, 0);                         // timestamp
    put_u32(b, func_count);
}

// Appends a function whose fixed-size header fields are all empty, up to (but
// not including) the chunk's source-map count, so individual tests can supply a
// crafted source-map count.
static void put_empty_function_prefix(std::vector<std::uint8_t>& b) {
    put_u32(b, 0); // name length
    put_u16(b, 0); // arity
    put_u16(b, 0); // required_arity
    put_u16(b, 0); // upvalue_count
    put_u8(b, 0);  // flags
    put_u16(b, 0); // param_count
    put_u16(b, 0); // local_count
    put_u16(b, 0); // mutable_count
    put_u16(b, 0); // upvalue descriptor count
    // Chunk:
    put_u32(b, 0); // code size
    put_u16(b, 0); // constant count
}

// ─── Round-trip ───

static void test_round_trip_minimal() {
    CompiledFunction top;
    top.name = "<top-level>";

    const auto bytes = BytecodeSerializer::serialize(top, {}, /*source_hash=*/0);
    const auto result = BytecodeSerializer::deserialize(bytes);

    ASSERT_TRUE(static_cast<bool>(result));
    ASSERT_TRUE(result.error == DeserializeError::None);
    ASSERT_EQ(result->top_level.name, std::string{"<top-level>"});
    ASSERT_TRUE(result->functions.empty());
}

// A function's is_test / is_main flags must survive the .lumc round-trip so
// that `--test` can rediscover annotated functions from cached bytecode.
static void test_round_trip_preserves_function_flags() {
    CompiledFunction top;
    top.name = "<top-level>";

    CompiledFunction test_fn;
    test_fn.name = "test_example";
    test_fn.is_test = true;

    CompiledFunction main_fn;
    main_fn.name = "main";
    main_fn.is_main = true;

    std::vector<CompiledFunction> functions;
    functions.push_back(std::move(test_fn));
    functions.push_back(std::move(main_fn));

    const auto bytes = BytecodeSerializer::serialize(top, functions, /*source_hash=*/0);
    const auto result = BytecodeSerializer::deserialize(bytes);

    ASSERT_TRUE(static_cast<bool>(result));
    ASSERT_EQ(result->functions.size(), 2U);
    ASSERT_EQ(result->functions[0].name, std::string{"test_example"});
    ASSERT_TRUE(result->functions[0].is_test);
    ASSERT_FALSE(result->functions[0].is_main);
    ASSERT_TRUE(result->functions[1].is_main);
    ASSERT_FALSE(result->functions[1].is_test);
}

// ─── Malformed input: oversized source-map count ───
//
// Regression test for a fuzz-found out-of-memory: a u32 source-map entry count
// read from untrusted bytes was passed straight to std::vector::reserve. A
// value near 2^32 requested a multi-gigabyte allocation. The deserializer must
// reject a count that exceeds the remaining input instead of allocating.

static void test_rejects_oversized_source_map_count() {
    std::vector<std::uint8_t> bytes;
    put_header(bytes, /*func_count=*/1);
    put_empty_function_prefix(bytes);
    put_u32(bytes, 0xFFFFFFFFU); // source-map count — far larger than any input
    // No source-map entries follow: the count is impossible for this input.

    const auto result = BytecodeSerializer::deserialize(bytes);

    ASSERT_FALSE(static_cast<bool>(result));
    ASSERT_TRUE(result.error == DeserializeError::CorruptFunction);
}

// A source-map count that is small enough to allocate but still larger than the
// remaining bytes must also be rejected (no truncated/partial read).

static void test_rejects_truncated_source_map() {
    std::vector<std::uint8_t> bytes;
    put_header(bytes, /*func_count=*/1);
    put_empty_function_prefix(bytes);
    put_u32(bytes, 4); // claims 4 entries (64 bytes) but provides none

    const auto result = BytecodeSerializer::deserialize(bytes);

    ASSERT_FALSE(static_cast<bool>(result));
    ASSERT_TRUE(result.error == DeserializeError::CorruptFunction);
}

// ─── Malformed input: oversized name-table count ───
//
// The name table shares the source map's byte-budget guard (both route through
// the deserializer's read_bounded_array helper): each name occupies at least its
// u32 length prefix, so a count whose implied minimum size exceeds the remaining
// input cannot be valid and must be rejected before the reserve. This mirrors
// test_rejects_oversized_source_map_count for the name-table section so a
// regression in either guard is caught independently.

static void test_rejects_oversized_name_count() {
    std::vector<std::uint8_t> bytes;
    put_header(bytes, /*func_count=*/1);
    put_empty_function_prefix(bytes);
    put_u32(bytes, 0);      // source-map count — none
    put_u16(bytes, 0xFFFF); // name count — 65535 names, impossible for this input
    // No name entries follow: the count is impossible for this input.

    const auto result = BytecodeSerializer::deserialize(bytes);

    ASSERT_FALSE(static_cast<bool>(result));
    ASSERT_TRUE(result.error == DeserializeError::CorruptFunction);
}

//
// The .lumc format stores a scalar upvalue_count and an explicit upvalue
// descriptor array as two independent fields. The bytecode verifier bounds
// upvalue indices against the scalar, but the VM indexes the descriptor array;
// if the two disagree, a verified function could read past the array. The
// deserializer must reject any function whose scalar count does not match the
// number of descriptors actually present.

static void test_rejects_upvalue_count_mismatch() {
    std::vector<std::uint8_t> bytes;
    put_header(bytes, /*func_count=*/1);

    put_u32(bytes, 0); // name length
    put_u16(bytes, 0); // arity
    put_u16(bytes, 0); // required_arity
    put_u16(bytes, 1); // upvalue_count (scalar) — claims one upvalue
    put_u8(bytes, 0);  // flags
    put_u16(bytes, 0); // param_count
    put_u16(bytes, 0); // local_count
    put_u16(bytes, 0); // mutable_count
    put_u16(bytes, 0); // upvalue descriptor count — zero descriptors (mismatch)
    // A complete, otherwise-valid empty chunk follows, so that without the
    // scalar/array consistency check the function would deserialize cleanly.
    put_u32(bytes, 0); // code size
    put_u16(bytes, 0); // constant count
    put_u32(bytes, 0); // source-map count
    put_u16(bytes, 0); // name count

    const auto result = BytecodeSerializer::deserialize(bytes);

    ASSERT_FALSE(static_cast<bool>(result));
    ASSERT_TRUE(result.error == DeserializeError::CorruptFunction);
}

// ─── Malformed input: oversized schema scalars ───
//
// arity, required_arity, and upvalue_count are stored as raw u16 fields. The
// compiler caps them at CompilerLimits (k_max_arguments / k_max_upvalues = 255)
// on write, and the VM allocates argument frames and upvalue vectors sized from
// them, so a crafted .lumc with an over-limit value must be rejected before it
// drives an oversized allocation. The verifier does not range-check these
// scalars, so the deserializer is the guard. Each test below is a complete,
// otherwise-valid single function (func_count = 1, so no extra functions
// follow): without the cap it would deserialize successfully, which is exactly
// what these regressions guard against.

// Appends a complete, otherwise-valid function whose only potential defect is
// the caller-supplied arity / required_arity / upvalue_count. `uv_desc`
// zero-filled descriptors (index 0, no flags) are emitted so the scalar
// upvalue_count and the descriptor array agree.
static void put_function(std::vector<std::uint8_t>& b, std::uint16_t arity,
                         std::uint16_t required_arity, std::uint16_t upvalue_count,
                         std::uint16_t uv_desc) {
    put_u32(b, 0);              // name length
    put_u16(b, arity);          // arity
    put_u16(b, required_arity); // required_arity
    put_u16(b, upvalue_count);  // upvalue_count (scalar)
    put_u8(b, 0);               // flags
    put_u16(b, 0);              // param_count
    put_u16(b, 0);              // local_count
    put_u16(b, 0);              // mutable_count
    put_u16(b, uv_desc);        // upvalue descriptor count
    for (std::uint16_t i = 0; i < uv_desc; ++i) {
        put_u16(b, 0); // upvalue index
        put_u8(b, 0);  // upvalue flags
    }
    // Empty chunk.
    put_u32(b, 0); // code size
    put_u16(b, 0); // constant count
    put_u32(b, 0); // source-map count
    put_u16(b, 0); // name count
}

static void test_rejects_oversized_arity() {
    std::vector<std::uint8_t> bytes;
    put_header(bytes, /*func_count=*/1);
    put_function(bytes, /*arity=*/65535, /*required_arity=*/0, /*upvalue_count=*/0, /*uv_desc=*/0);

    const auto result = BytecodeSerializer::deserialize(bytes);

    ASSERT_FALSE(static_cast<bool>(result));
    ASSERT_TRUE(result.error == DeserializeError::CorruptFunction);
}

static void test_rejects_oversized_required_arity() {
    std::vector<std::uint8_t> bytes;
    put_header(bytes, /*func_count=*/1);
    put_function(bytes, /*arity=*/0, /*required_arity=*/65535, /*upvalue_count=*/0, /*uv_desc=*/0);

    const auto result = BytecodeSerializer::deserialize(bytes);

    ASSERT_FALSE(static_cast<bool>(result));
    ASSERT_TRUE(result.error == DeserializeError::CorruptFunction);
}

static void test_rejects_oversized_upvalue_count() {
    // upvalue_count = 256 with a matching 256-descriptor array: a complete,
    // internally-consistent function that only exceeds k_max_upvalues (255).
    // The scalar/descriptor consistency check passes, so without the cap this
    // would deserialize and the VM would size upvalue vectors from the crafted
    // count.
    std::vector<std::uint8_t> bytes;
    put_header(bytes, /*func_count=*/1);
    put_function(bytes, /*arity=*/0, /*required_arity=*/0, /*upvalue_count=*/256, /*uv_desc=*/256);

    const auto result = BytecodeSerializer::deserialize(bytes);

    ASSERT_FALSE(static_cast<bool>(result));
    ASSERT_TRUE(result.error == DeserializeError::CorruptFunction);
}

// ─── Malformed input: parameter-name table larger than arity ───
//
// The VM binds named arguments into an argument frame sized to the function's
// arity, using the positional index each name maps to (build_param_name_index →
// bind_named_args). A crafted .lumc that declares more parameter names than the
// arity would produce an index past the end of that frame — an out-of-bounds
// write of an attacker-controlled value. The deserializer must reject any
// function whose param_count exceeds its arity. (Lambdas legitimately emit zero
// names with arity > 0, so only counts strictly greater than arity are corrupt.)

static void test_rejects_param_count_exceeding_arity() {
    std::vector<std::uint8_t> bytes;
    put_header(bytes, /*func_count=*/1);

    put_u32(bytes, 0); // name length
    put_u16(bytes, 1); // arity — one-slot argument frame
    put_u16(bytes, 1); // required_arity
    put_u16(bytes, 0); // upvalue_count (scalar)
    put_u8(bytes, 0);  // flags
    put_u16(bytes, 2); // param_count — two names, exceeding arity (the defect)
    put_u32(bytes, 1); // param name[0] length
    put_u8(bytes, 'a');
    put_u32(bytes, 1); // param name[1] length — maps to index 1, past the frame
    put_u8(bytes, 'b');
    put_u16(bytes, 0); // local_count
    put_u16(bytes, 0); // mutable_count
    put_u16(bytes, 0); // upvalue descriptor count
    // A complete, otherwise-valid empty chunk follows, so that without the
    // param_count ≤ arity check the function would deserialize cleanly.
    put_u32(bytes, 0); // code size
    put_u16(bytes, 0); // constant count
    put_u32(bytes, 0); // source-map count
    put_u16(bytes, 0); // name count

    const auto result = BytecodeSerializer::deserialize(bytes);

    ASSERT_FALSE(static_cast<bool>(result));
    ASSERT_TRUE(result.error == DeserializeError::CorruptFunction);
}

// ─── Malformed input: function count larger than the remaining input ───
//
// func_count is a u32 read from untrusted bytes, bounded only by
// k_max_function_count (100,000). The deserializer reserves the functions
// vector from it before reading any function body, so a tiny crafted .lumc that
// declares a large count forces a multi-megabyte speculative allocation
// (CompiledFunction is a heavy value type) even though the file cannot possibly
// contain that many functions. The deserializer must reject a func_count whose
// implied minimum size (func_count × k_min_function_bytes) exceeds the remaining
// input before reserving — the same has_remaining guard the source-map and
// name-table paths apply. The distinguishing symptom is the error code: with the
// guard the file is rejected as BadFunctionCount before the reservation; without
// it the reservation runs and the subsequent truncated read fails later as
// CorruptFunction.

static void test_rejects_function_count_exceeding_input() {
    std::vector<std::uint8_t> bytes;
    put_header(bytes, /*func_count=*/SerializerLimits::k_max_function_count);
    // One valid top-level function, then nothing: the declared 99,999 remaining
    // functions cannot fit in the (now-exhausted) input.
    put_function(bytes, /*arity=*/0, /*required_arity=*/0, /*upvalue_count=*/0, /*uv_desc=*/0);

    const auto result = BytecodeSerializer::deserialize(bytes);

    ASSERT_FALSE(static_cast<bool>(result));
    // Rejected by the remaining-input guard before the vector reservation, not by
    // a per-function read failing afterwards.
    ASSERT_TRUE(result.error == DeserializeError::BadFunctionCount);
}

// ─── Constant-pool / name-table cap vs. the u16 wire count ───
//
// The .lumc format stores the per-chunk constant and name counts as u16 fields.
// The pools must therefore be capped so a full chunk's count still fits that
// field: a cap of 65 536 would let serialize_chunk write static_cast<u16>(65536)
// == 0, load zero entries on the next run, and desync the stream (a silent
// recompile at best, an out-of-bounds pool read in the VM at worst). These tests
// pin the cap at the u16 ceiling — the full pool round-trips, and one more entry
// is rejected with a clean overflow.

static void test_constant_pool_cap_round_trips_within_u16_count() {
    CompiledFunction top;
    top.name = "<top-level>";
    auto& constants = top.mutable_chunk().constants;

    // Fill to the cap with distinct integer constants (dedup keeps them distinct).
    for (std::size_t i = 0; i < ConstantPool::max_size; ++i) {
        (void)constants.add(Value{static_cast<std::int64_t>(i)});
    }
    ASSERT_EQ(constants.size(), ConstantPool::max_size);

    // One more distinct constant must overflow rather than push the count past
    // what the u16 wire field can represent.
    bool overflowed = false;
    try {
        (void)constants.add(Value{static_cast<std::int64_t>(ConstantPool::max_size)});
    } catch (const std::overflow_error&) {
        overflowed = true;
    }
    ASSERT_TRUE(overflowed);

    // The full pool must survive the round-trip intact; a wrapped count would
    // load zero constants.
    const auto bytes = BytecodeSerializer::serialize(top, {}, /*source_hash=*/0);
    const auto result = BytecodeSerializer::deserialize(bytes);
    ASSERT_TRUE(static_cast<bool>(result));
    ASSERT_EQ(result->top_level.chunk().constants.size(), ConstantPool::max_size);
    const auto last = ConstantPool::max_size - 1;
    ASSERT_TRUE(result->top_level.chunk().constants[last].is_integer());
    ASSERT_EQ(result->top_level.chunk().constants[last].as_integer(),
              static_cast<std::int64_t>(last));
}

static void test_name_table_cap_round_trips_within_u16_count() {
    CompiledFunction top;
    top.name = "<top-level>";
    auto& names = top.mutable_chunk().names;

    for (std::size_t i = 0; i < NameTable::k_max_size; ++i) {
        (void)names.add("n" + std::to_string(i));
    }
    ASSERT_EQ(names.size(), NameTable::k_max_size);

    bool overflowed = false;
    try {
        (void)names.add("n" + std::to_string(NameTable::k_max_size));
    } catch (const std::overflow_error&) {
        overflowed = true;
    }
    ASSERT_TRUE(overflowed);

    const auto bytes = BytecodeSerializer::serialize(top, {}, /*source_hash=*/0);
    const auto result = BytecodeSerializer::deserialize(bytes);
    ASSERT_TRUE(static_cast<bool>(result));
    ASSERT_EQ(result->top_level.chunk().names.size(), NameTable::k_max_size);
}

// ─── Round-trip: a string constant past the old u16 length ceiling ───
//
// Regression test for a stream desync: Writer::write_string prefixed each string
// with a u16 length but always wrote every byte, so a string constant or name of
// 65 536 bytes or more wrapped its length prefix (e.g. 70 000 → 4 464) while its
// full bytes stayed in the stream. read_string then consumed only the wrapped
// count, and every following field was misaligned — a silent recompile (the
// source hash still matched the truncated read) at best, loading a different
// cached program at worst. The prefix is now u32; a string past the old u16
// ceiling must round-trip byte-for-byte, and a constant serialized after it must
// still decode, proving the stream stays aligned.
static void test_round_trip_large_string_constant() {
    CompiledFunction top;
    top.name = "<top-level>";
    auto& constants = top.mutable_chunk().constants;

    // 70 000 bytes: past the u16 ceiling (65 535) so the old prefix would have
    // wrapped to 70000 - 65536 = 4464.
    const std::string large(70000, 'x');
    const auto large_index = constants.add(Value{large});
    // A distinct constant serialized after the large string: had the large
    // string's length wrapped, these bytes would be misread and the stream would
    // desync, so an intact round-trip here proves alignment is preserved.
    const auto sentinel_index = constants.add(Value{std::string{"sentinel"}});

    const auto bytes = BytecodeSerializer::serialize(top, {}, /*source_hash=*/0);
    const auto result = BytecodeSerializer::deserialize(bytes);

    ASSERT_TRUE(static_cast<bool>(result));
    ASSERT_TRUE(result.error == DeserializeError::None);
    const auto& round_tripped = result->top_level.chunk().constants;
    ASSERT_EQ(round_tripped.size(), 2U);
    ASSERT_TRUE(round_tripped[large_index].is_string());
    ASSERT_EQ(round_tripped[large_index].as_string(), large);
    ASSERT_TRUE(round_tripped[sentinel_index].is_string());
    ASSERT_EQ(round_tripped[sentinel_index].as_string(), std::string{"sentinel"});
}

// ─── main ───

int main() {
    RUN(test_round_trip_minimal);
    RUN(test_round_trip_preserves_function_flags);
    RUN(test_rejects_oversized_source_map_count);
    RUN(test_rejects_truncated_source_map);
    RUN(test_rejects_oversized_name_count);
    RUN(test_rejects_upvalue_count_mismatch);
    RUN(test_rejects_param_count_exceeding_arity);
    RUN(test_rejects_oversized_arity);
    RUN(test_rejects_oversized_required_arity);
    RUN(test_rejects_oversized_upvalue_count);
    RUN(test_rejects_function_count_exceeding_input);
    RUN(test_constant_pool_cap_round_trips_within_u16_count);
    RUN(test_name_table_cap_round_trips_within_u16_count);
    RUN(test_round_trip_large_string_constant);
    return SUMMARY();
}
