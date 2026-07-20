// ─────────────────────────────────────────────────────────────────────────────
// Emitter — bytecode emission abstraction
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Encapsulate all low-level bytecode emission operations
// (opcodes, operands, constants, jumps) behind a single class that operates
// on a Chunk.  This decouples the compiler's AST traversal from the details
// of bytecode encoding, making emission independently testable and allowing
// alternate backends in the future.
//
// The Emitter does NOT own the Chunk — it holds a non-owning reference.
// The compiler (or test harness) is responsible for the Chunk's lifetime.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_EMITTER_HPP
#define LUMA_COMPILER_EMITTER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "analysis/source/source_location.hpp"
#include "runtime/compiler/bytecode_emitter.hpp"
#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

// Bytecode emitter — concrete implementation of BytecodeEmitter that
// wraps a Chunk reference.  Provides the full emission API used by the
// compiler and its helper classes.
//
// Usage:
//   Chunk chunk;
//   Emitter emitter{chunk};
//   emitter.emit_opcode(Op::None, loc);
//   auto idx = emitter.emit_constant(Value{42}, loc);
//   auto jump = emitter.emit_jump(Op::JumpIfFalse, loc);
//   // ... emit more code ...
//   emitter.patch_jump(jump);
class Emitter final : public BytecodeEmitter {
public:
    explicit Emitter(Chunk& chunk) : chunk_(chunk) {}

    // ─── Single-byte opcode ───
    void emit_opcode(Op op, SourceLocation loc) override {
        chunk_.emit(op, loc);
    }

    // ─── Opcode with u8 operand ───
    void emit_u8(Op op, std::uint8_t operand, SourceLocation loc) override {
        chunk_.emit_u8(op, operand, loc);
    }

    // ─── Opcode with u16 operand (big-endian) ───
    void emit_u16(Op op, std::uint16_t operand, SourceLocation loc) override {
        chunk_.emit_u16(op, operand, loc);
    }

    // ─── Opcode with u32 operand (big-endian) ───
    void emit_u32(Op op, std::uint32_t operand, SourceLocation loc) override {
        chunk_.emit_u32(op, operand, loc);
    }

    // ─── Constant emission ───
    // Add a constant to the pool and emit a Constant instruction
    // referencing it.  Returns the pool index.
    std::uint16_t emit_constant(Value value, SourceLocation loc) override {
        auto index = chunk_.add_constant(std::move(value));
        emit_u16(Op::Constant, index, loc);
        return index;
    }

    // ─── Jump emission and patching ───
    // Emit a jump instruction with a placeholder 32-bit offset.
    // Returns the bytecode offset of the placeholder (to be patched later).
    [[nodiscard]] std::size_t emit_jump(Op op, SourceLocation loc) override {
        return chunk_.emit_jump(op, loc);
    }

    // Patch a previously emitted jump so it lands at the current offset.
    void patch_jump(std::size_t offset) override {
        chunk_.patch_jump(offset);
    }

    // ─── Loop (backward jump) ───
    void emit_loop(std::size_t loop_start, SourceLocation loc) override {
        chunk_.emit_loop(loop_start, loc);
    }

    // ─── Raw byte / u16 emission (no opcode, no source map entry) ───
    void emit_raw_byte(std::uint8_t byte) override {
        chunk_.code.push_back(byte);
    }

    void emit_raw_u16(std::uint16_t value) override {
        chunk_.push_u16(value);
    }

    // ─── Name table ───
    // Add a name to the chunk's name table and return its index.
    [[nodiscard]] std::uint16_t add_name(std::string_view name) override {
        return chunk_.add_name(name);
    }

    // ─── Constant pool (no instruction emitted) ───
    // Add a constant to the pool without emitting an instruction.
    [[nodiscard]] std::uint16_t add_constant(Value value) override {
        return chunk_.add_constant(std::move(value));
    }

    // ─── Offset queries ───
    [[nodiscard]] std::size_t current_offset() const override {
        return chunk_.current_offset();
    }

    // ─── Chunk access ───
    // Escape hatch for operations that need direct chunk access
    // (e.g. disassembly, verification, serialisation).
    // Not part of the BytecodeEmitter interface — specific to
    // the Chunk-backed implementation.
    [[nodiscard]] Chunk& chunk() noexcept {
        return chunk_;
    }

    [[nodiscard]] const Chunk& chunk() const noexcept {
        return chunk_;
    }

private:
    Chunk& chunk_;
};

} // namespace luma

#endif // LUMA_COMPILER_EMITTER_HPP
