// ─────────────────────────────────────────────────────────────────────────────
// BytecodeEmitter — Abstract interface for bytecode emission
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Define the pure emission API used by the compiler and its
// helper classes, decoupled from any concrete bytecode representation.
//
// The concrete implementation (Emitter, in emitter.hpp) delegates to a Chunk,
// but tests and alternative backends can provide their own implementations
// without depending on Chunk, ConstantPool, or the source-map machinery.
//
// Design notes:
//   - No chunk() accessor: consumers that need direct Chunk access should
//     use the concrete Emitter type.  The interface deliberately hides the
//     storage representation.
//   - Value is taken by value (moved into the constant pool by the impl).
//   - SourceLocation is taken by value (trivially copyable).
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_BYTECODE_EMITTER_HPP
#define LUMA_COMPILER_BYTECODE_EMITTER_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "analysis/source/source_location.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

class BytecodeEmitter {
public:
    virtual ~BytecodeEmitter() = default;

    BytecodeEmitter() = default;
    BytecodeEmitter(const BytecodeEmitter&) = default;
    BytecodeEmitter& operator=(const BytecodeEmitter&) = default;
    BytecodeEmitter(BytecodeEmitter&&) = default;
    BytecodeEmitter& operator=(BytecodeEmitter&&) = default;

    // ─── Opcode emission ───

    // Emit a single-byte opcode.
    virtual void emit_opcode(Op op, SourceLocation loc) = 0;

    // Emit an opcode with a u8 operand.
    virtual void emit_u8(Op op, std::uint8_t operand, SourceLocation loc) = 0;

    // Emit an opcode with a u16 operand (big-endian).
    virtual void emit_u16(Op op, std::uint16_t operand, SourceLocation loc) = 0;

    // Emit an opcode with a u32 operand (big-endian).
    virtual void emit_u32(Op op, std::uint32_t operand, SourceLocation loc) = 0;

    // ─── Constant emission ───

    // Add a constant to the pool and emit a Constant instruction.
    // Returns the pool index (needed only when patching constants post-emit).
    virtual std::uint16_t emit_constant(Value value, SourceLocation loc) = 0;

    // Add a constant to the pool without emitting an instruction.
    [[nodiscard]] virtual std::uint16_t add_constant(Value value) = 0;

    // ─── Jump emission and patching ───

    // Emit a jump instruction with a placeholder 32-bit offset.
    // Returns the bytecode offset of the placeholder.
    [[nodiscard]] virtual std::size_t emit_jump(Op op, SourceLocation loc) = 0;

    // Patch a previously emitted jump so it lands at the current offset.
    virtual void patch_jump(std::size_t offset) = 0;

    // Emit a loop (backward jump) to loop_start.
    virtual void emit_loop(std::size_t loop_start, SourceLocation loc) = 0;

    // ─── Raw emission (no opcode, no source map entry) ───

    // Push a single raw byte into the bytecode stream.
    virtual void emit_raw_byte(std::uint8_t byte) = 0;

    // Push a raw u16 value (big-endian) into the bytecode stream.
    virtual void emit_raw_u16(std::uint16_t value) = 0;

    // ─── Name table ───

    // Add a name to the name table and return its index.
    [[nodiscard]] virtual std::uint16_t add_name(std::string_view name) = 0;

    // ─── Offset queries ───

    // Current bytecode offset (byte count emitted so far).
    [[nodiscard]] virtual std::size_t current_offset() const = 0;
};

} // namespace luma

#endif // LUMA_COMPILER_BYTECODE_EMITTER_HPP
