// ─────────────────────────────────────────────────────────────────────────────
// IBytecodeEmitter — Narrow interface for raw bytecode/jump emission
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Emit opcodes with inline operands, jumps, loops, and raw
//   bytes into the bytecode stream, and patch previously emitted jumps.
//
// Part of the ICompilationBackend interface-segregation (ISP) split: the fat
// backend interface is composed from focused role interfaces so that helper
// classes can depend on only the slice they use, and so each concern is
// independently documented and mockable.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_I_BYTECODE_EMITTER_HPP
#define LUMA_COMPILER_I_BYTECODE_EMITTER_HPP

#include <cstddef>
#include <cstdint>

#include "analysis/source/source_location.hpp"
#include "runtime/compiler/opcode.hpp"

namespace luma {

// Raw bytecode and jump emission surface.
class IBytecodeEmitter {
public:
    virtual ~IBytecodeEmitter() = default;

    virtual void emit_u8(Op op, std::uint8_t operand, SourceLocation loc) = 0;
    virtual void emit_u16(Op op, std::uint16_t operand, SourceLocation loc) = 0;

    [[nodiscard]] virtual std::size_t emit_jump(Op op, SourceLocation loc) = 0;
    virtual void emit_loop(std::size_t loop_start, SourceLocation loc) = 0;
    virtual void emit_raw_byte(std::uint8_t byte) = 0;
    virtual void emit_raw_u16(std::uint16_t value) = 0;
    virtual void patch_jump(std::size_t offset) = 0;

protected:
    IBytecodeEmitter() = default;
    IBytecodeEmitter(const IBytecodeEmitter&) = default;
    IBytecodeEmitter& operator=(const IBytecodeEmitter&) = default;
};

} // namespace luma

#endif // LUMA_COMPILER_I_BYTECODE_EMITTER_HPP
