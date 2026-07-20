#ifndef LUMA_COMPILER_COMPILER_LIMITS_HPP
#define LUMA_COMPILER_COMPILER_LIMITS_HPP

#include <cstddef>
#include <cstdint>

namespace luma {

// Compile-time limits for the bytecode compiler.
// Centralised in one header so that the compiler, optimizer, verifier,
// and any other consumer share the same values.
struct CompilerLimits {
    // Maximum local variables per function scope (16-bit operand addressing).
    static constexpr std::uint16_t k_max_locals = 65535;
    // Maximum function parameters (8-bit operand in call instructions).
    static constexpr std::uint8_t k_max_arguments = 255;
    // Maximum constants per chunk (16-bit constant pool index).
    static constexpr std::uint16_t k_max_constants = 65535;
    // Maximum upvalues per closure (8-bit operand in closure instructions).
    static constexpr std::uint8_t k_max_upvalues = 255;
    // Compile-time element limit for array/dict literals (16-bit operand).
    // Runtime limit is ResourceLimits::max_array_size.
    static constexpr std::uint16_t k_max_array_elements = 65535;
    // Compile-time entry limit for dictionary literals (16-bit operand).
    static constexpr std::uint16_t k_max_dict_entries = 65535;
    // Maximum entries in the name table (global/field names) per chunk. Capped at
    // 65 535, not the full 16-bit index space, because the .lumc format serialises
    // the per-chunk name count as a u16 — a 65 536th entry would wrap the count to 0.
    static constexpr std::size_t k_max_names = 65535;
    // Maximum compiled functions per program. Each nested function/lambda is
    // referenced by a 16-bit MakeClosure operand, so the function table cannot
    // exceed the 16-bit index space (the highest valid index is k_max_functions - 1).
    static constexpr std::size_t k_max_functions = 65536;
    // Maximum upvalue resolution depth to prevent stack overflow during compilation.
    static constexpr int k_max_upvalue_depth = 512;

    // Slot index of the first parameter in a function frame.
    // Slot 0 is the function object itself; parameters start at slot 1.
    static constexpr int k_first_parameter_slot = 1;

    // ─── Constant folding limits ───

    // Width of a std::int64_t in bits; used to validate shift amounts.
    static constexpr int k_int64_bits = 64;

    // ─── Special integer values for opcode optimisation ───

    // Integer literals that have dedicated single-byte opcodes (Op::Zero,
    // Op::One) instead of a full Constant instruction.
    static constexpr std::int64_t k_integer_zero = 0;
    static constexpr std::int64_t k_integer_one = 1;

    // ─── Well-known names ───

    // Maximum stack depth allowed during bytecode verification.
    static constexpr int k_max_stack_depth = 65536;

    // Name used for the top-level "script" function compiled by the compiler.
    static constexpr const char* k_top_level_name = "<top-level>";
};

// Inter-dependent limit validation.
static_assert(CompilerLimits::k_max_locals <= CompilerLimits::k_max_names,
              "Local slot indices must fit within the name table address space");
static_assert(CompilerLimits::k_max_constants <= CompilerLimits::k_max_names,
              "Constant pool indices must fit within the name table address space");
static_assert(CompilerLimits::k_max_upvalues <= CompilerLimits::k_max_arguments,
              "Upvalue count must fit in a u8 operand like argument counts");
static_assert(CompilerLimits::k_max_functions - 1 <= 0xFFFF,
              "Function indices must fit in the u16 MakeClosure operand");

// Bytecode instruction layout constants.
// These describe the fixed sizes of opcode bytes and operand encodings
// so that the compiler, optimizer, and verifier share the same values.
struct InstructionLayout {
    // Size of the opcode byte itself.
    static constexpr std::size_t k_opcode_size = 1;
    // Size of a u8 operand.
    static constexpr std::size_t k_u8_operand_size = 1;
    // Size of a u16 operand (big-endian).
    static constexpr std::size_t k_u16_operand_size = 2;
    // Size of a u32 operand (big-endian).
    static constexpr std::size_t k_u32_operand_size = 4;
    // Total size of a Constant instruction (opcode + u16 index).
    static constexpr std::size_t k_constant_instruction_size =
        k_opcode_size + k_u16_operand_size; // 3
    // Total size of a jump/loop instruction (opcode + u32 offset).
    static constexpr std::size_t k_jump_instruction_size = k_opcode_size + k_u32_operand_size; // 5
    // Total size of a u8-operand instruction (e.g. Call, Interpolate).
    static constexpr std::size_t k_u8_instruction_size = k_opcode_size + k_u8_operand_size; // 2
    // Byte offset from MakeClosure opcode to the trailing upvalue-count operand.
    // MakeClosure layout: [opcode:1][function_index:2][upvalue_count:1][upvalue_descriptors...]
    //                      ↑ offset from opcode to upvalue_count = 1 + 2 = 3
    static constexpr std::size_t k_make_closure_upvalue_offset =
        k_opcode_size + k_u16_operand_size; // 3
};

// Limits specific to the bytecode optimizer passes.
struct OptimizerLimits {
    // Maximum optimizer iterations before giving up. Cascading optimizations
    // (e.g. constant fold creates a new peephole opportunity) may require
    // several passes; 10 is sufficient for all known patterns.
    static constexpr int k_max_optimizer_iterations = 10;
    // Maximum string length for compile-time concatenation folding.
    // Prevents the constant pool from being bloated by very long folded strings.
    static constexpr std::size_t k_max_folded_string_length = 1024;
    // Maximum jump-chain depth for jump threading. Prevents infinite loops
    // in pathological bytecode with circular jump chains.
    static constexpr int k_max_jump_chain_depth = 10;
    // Maximum callee body size (in bytes) to consider for inlining.
    static constexpr std::size_t k_max_inline_body = 16;
};

// Limits used by the bytecode serializer/deserializer.
struct SerializerLimits {
    // Maximum size of a single function's code section in bytes.
    //
    // The code-section length is stored as a u32 in the .lumc format, so the
    // binary ceiling is 2^32 (~4 GB).  10 MB is chosen as a practical safety
    // cap: no realistic single-function bytecode body approaches this size,
    // and accepting pathologically large values would risk exhausting memory
    // when the deserializer pre-allocates the code buffer.  If the VM's
    // addressable code space or jump-offset range is ever widened beyond u32,
    // this limit should be revisited alongside the jump-instruction encoding.
    static constexpr std::uint32_t k_max_code_section_bytes = 10'000'000; // 10 MB

    // Maximum number of compiled functions in a serialized bytecode file.
    //
    // The function count field is stored as a u32, so the binary ceiling is
    // ~4 billion.  100 000 is chosen as a generous but bounded limit: even
    // very large Luma programs rarely define more than a few thousand
    // functions, and capping the count prevents a malformed .lumc file from
    // causing the deserializer to allocate an enormous functions vector before
    // any data is validated.
    static constexpr std::uint32_t k_max_function_count = 100'000;

    // Maximum total size of a serialized bytecode file.
    //
    // Enforced before the deserializer reads any data into memory, so that a
    // corrupt or adversarially crafted .lumc file cannot force an allocation
    // larger than this bound.  100 MB is a comfortable upper bound for any
    // plausible pre-compiled Luma module or script; a file exceeding this
    // almost certainly indicates corruption or an attack.
    static constexpr std::size_t k_max_bytecode_file_size = 100'000'000; // 100 MB

    // Size in bytes of a single source-map entry in the serialized format:
    // offset (u32) + file_id (u32) + line (u32) + column (u32) = 16 bytes.
    // The deserializer uses this to bound the source-map entry count read from
    // an untrusted .lumc file against the remaining input, preventing an
    // unbounded reservation (out-of-memory) from a corrupt or malicious file.
    static constexpr std::size_t k_source_map_entry_bytes = 16;

    // Minimum number of bytes a serialized string occupies: its u32 length
    // prefix. The actual string may be longer, but a count of string entries
    // can never validly exceed (remaining bytes / this), so the deserializer
    // uses it to bound name/param/local count reservations against the
    // remaining input — same out-of-memory protection as the entries above.
    static constexpr std::size_t k_string_length_prefix_bytes = 4;

    // Size in bytes of a single serialized local-mutable flag: one u8.
    static constexpr std::size_t k_local_mutable_entry_bytes = 1;

    // Size in bytes of a single serialized upvalue descriptor:
    // index (u16) + flags (u8) = 3 bytes.
    static constexpr std::size_t k_upvalue_entry_bytes = 3;

    // Minimum number of bytes a serialized function occupies, with every
    // embedded count zero and its name empty: name length prefix (u32) + arity,
    // required_arity and upvalue_count (3×u16) + flags (u8) + param-name,
    // local-name, local-mutable and upvalue counts (4×u16) + an empty chunk
    // (code length u32 + constants count u16 + source-map count u32 + names
    // count u16) = 31 bytes. A real function can only be larger, so a func_count
    // whose implied minimum size exceeds the remaining input can never be valid;
    // the deserializer uses this to bound the functions-vector reservation
    // against the remaining bytes — the same out-of-memory protection the
    // source-map and name-table guards apply.
    static constexpr std::size_t k_min_function_bytes = 31;
};

} // namespace luma

#endif // LUMA_COMPILER_COMPILER_LIMITS_HPP
