// ─────────────────────────────────────────────────────────────────────────────
// IConstantEmitter — Narrow interface for constant emission
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Define the minimal interface needed by ConstantFolder —
//   only emit(Op, loc) and emit_constant(Value, loc).
//
// Design: ICompilationBackend inherits from this interface, so any component
//   that accepts the full backend can also be passed to code expecting only
//   IConstantEmitter.  This enables ConstantFolder to depend on the narrowest
//   possible interface, improving testability and documenting the exact
//   coupling surface.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_I_CONSTANT_EMITTER_HPP
#define LUMA_COMPILER_I_CONSTANT_EMITTER_HPP

#include <cstdint>

#include "analysis/source/source_location.hpp"
#include "runtime/compiler/opcode.hpp"

namespace luma {

class Value;

// Minimal interface for emitting opcodes and constants into the bytecode
// stream.  Used by ConstantFolder to avoid depending on the full
// ICompilationBackend interface.
class IConstantEmitter {
public:
    virtual ~IConstantEmitter() = default;

    virtual void emit(Op op, SourceLocation loc) = 0;
    virtual std::uint16_t emit_constant(Value value, SourceLocation loc) = 0;

protected:
    IConstantEmitter() = default;
    IConstantEmitter(const IConstantEmitter&) = default;
    IConstantEmitter& operator=(const IConstantEmitter&) = default;
};

} // namespace luma

#endif // LUMA_COMPILER_I_CONSTANT_EMITTER_HPP
