#ifndef LUMA_COMPILER_CHUNK_HPP
#define LUMA_COMPILER_CHUNK_HPP

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/byte_utils.hpp"
#include "runtime/compiler/constant_pool.hpp"
#include "runtime/compiler/name_table.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/source_map.hpp"

namespace luma {

// TODO(refactor/C5): BytecodeBuilder — decompose Chunk into focused types.
//
// Progress: the name table and source map are now standalone types that bundle
// their data with their behaviour:
//   names       Interned name table         → NameTable (name_table.hpp); owns
//               the dedup index that used to be a private Chunk member.
//   source_map  Instruction→location info   → SourceMap (source_map.hpp); owns
//               the binary-search lookup.
//
// Remaining concerns kept on Chunk by design:
//   code        Raw instruction bytes       → intentionally a plain
//               std::vector<std::uint8_t>.  The optimizer rewrites this stream
//               in place (erase/insert/resize/move) and the VM reads it via raw
//               pointer arithmetic, so a newtype wrapper would have to re-export
//               the entire mutable vector API for no decoupling benefit.  The
//               separable "builder" concern (emit/patch/jump) already lives in
//               the methods below, which must coordinate code + source_map
//               together and therefore belong on Chunk.
//   constants   Runtime constants table     → ConstantPool (already a separate
//               type, embedded by value here).
//
// CompiledFunction and FunctionDebugInfo have been extracted to
// compiled_function.hpp (see that header for function-level metadata).

// A chunk of bytecode — the output of the compiler and the input
// to the VM.  Contains the bytecode stream, a constant pool, and
// source location information for error reporting.
struct Chunk {
    /// Placeholder value for jump offsets before they are patched.
    static constexpr std::uint32_t k_jump_placeholder{0xFFFFFFFF};

    std::vector<std::uint8_t> code;
    ConstantPool constants;

    // Source map: maps instruction byte-offset → source location.
    // Only one entry per opcode (not per operand byte), saving ~60% memory.
    SourceMap source_map;

    NameTable names; // Interned name table (for globals, fields).

    // Emit a single-byte opcode.
    void emit(Op op, SourceLocation loc) {
        source_map.append(code.size(), loc);
        code.push_back(static_cast<std::uint8_t>(op));
    }

    // Emit an opcode with a u8 operand.
    void emit_u8(Op op, std::uint8_t operand, SourceLocation loc) {
        source_map.append(code.size(), loc);
        code.push_back(static_cast<std::uint8_t>(op));
        code.push_back(operand);
    }

    // Emit an opcode with a u16 operand (big-endian).
    void emit_u16(Op op, std::uint16_t operand, SourceLocation loc) {
        source_map.append(code.size(), loc);
        code.push_back(static_cast<std::uint8_t>(op));
        write_u16_be(code, operand);
    }

    // Emit an opcode with a u32 operand (big-endian). Used for jump instructions.
    void emit_u32(Op op, std::uint32_t operand, SourceLocation loc) {
        source_map.append(code.size(), loc);
        code.push_back(static_cast<std::uint8_t>(op));
        push_u32_be(operand);
    }

    // Push a raw u16 value into the bytecode stream (no opcode, no location).
    void push_u16(std::uint16_t value) {
        write_u16_be(code, value);
    }

    // Push a raw u32 value in big-endian byte order (no opcode, no location).
    void push_u32_be(std::uint32_t value) {
        write_u32_be(code, value);
    }

    // Add a constant to the pool and return its index.
    // Deduplicates integer, string, and number constants.
    [[nodiscard]] std::uint16_t add_constant(Value value) {
        return constants.add(std::move(value));
    }

    // Add a name to the name table and return its index.
    [[nodiscard]] std::uint16_t add_name(std::string_view name) {
        return names.add(name);
    }

    // Emit a jump instruction and return the offset of the jump
    // target (to be patched later). Jumps use 32-bit offsets to support
    // function bodies larger than 64 KB.
    [[nodiscard]] std::size_t emit_jump(Op op, SourceLocation loc) {
        source_map.append(code.size(), loc);
        code.push_back(static_cast<std::uint8_t>(op));
        push_u32_be(k_jump_placeholder);

        return code.size() - InstructionLayout::k_u32_operand_size;
    }

    // Patch a previously emitted jump instruction.
    void patch_jump(std::size_t offset) {
        const auto jump =
            calculate_jump_offset(offset + InstructionLayout::k_u32_operand_size, code.size());
        patch_u32_be(&code[offset], static_cast<std::uint32_t>(jump));
    }

    // Emit a loop instruction (backward jump). Uses a 32-bit offset.
    void emit_loop(std::size_t loop_start, SourceLocation loc) {
        source_map.append(code.size(), loc);
        code.push_back(static_cast<std::uint8_t>(Op::Loop));

        const auto offset = static_cast<std::uint32_t>(code.size() - loop_start +
                                                       InstructionLayout::k_u32_operand_size);
        push_u32_be(offset);
    }

    // Current code offset.
    [[nodiscard]] std::size_t current_offset() const noexcept {
        return code.size();
    }

    // Calculate the relative jump offset between two absolute positions.
    [[nodiscard]] static constexpr std::size_t calculate_jump_offset(std::size_t from,
                                                                     std::size_t to) {
        assert(to >= from && "calculate_jump_offset: backward jump not supported; use emit_loop");
        return to - from;
    }

    // Resolve a forward jump target from an instruction offset and relative delta.
    [[nodiscard]] static constexpr std::size_t
    resolve_forward_jump_target(std::size_t instruction_offset, std::size_t relative) {
        return instruction_offset + InstructionLayout::k_jump_instruction_size + relative;
    }

    // Resolve a backward (loop) jump target from an instruction offset and relative delta.
    [[nodiscard]] static constexpr std::size_t
    resolve_loop_jump_target(std::size_t instruction_offset, std::size_t relative) {
        return instruction_offset + InstructionLayout::k_jump_instruction_size - relative;
    }

    // Get the source location for a given instruction byte offset.
    // Uses binary search on the sparse source_map.
    [[nodiscard]] SourceLocation location_at(std::size_t offset) const {
        return source_map.location_at(offset);
    }

    // Disassemble the chunk for debugging.
    [[nodiscard]] std::string disassemble(const std::string& name = "chunk") const;

    // Disassemble a single instruction at the given offset.
    // Returns the number of variable-size trailing bytes beyond the fixed prefix.
    [[nodiscard]] std::size_t disassemble_instruction(std::size_t offset,
                                                      std::ostringstream& out) const;
};

} // namespace luma

// Include CompiledFunction after Chunk definition so that chunk.hpp
// remains the single include for consumers that need both types.
// This preserves backward compatibility while separating concerns.
#include "runtime/compiler/compiled_function.hpp"

#endif // LUMA_COMPILER_CHUNK_HPP
