#ifndef LUMA_COMPILER_VERIFIER_HPP
#define LUMA_COMPILER_VERIFIER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "runtime/compiler/chunk.hpp"

namespace luma {

// ─── Verification error ───

struct VerifyError {
    std::size_t offset{0};
    std::string message;
};

// ─── Stack effect ───
// How many values one instruction pops and then pushes.  For instructions
// whose effect depends on the path taken, this describes the path that
// continues into the instruction's successors: ForIterStep / ForIterStepKV
// describe a step that produced an element (the exhausted step pushes only
// `false`).

struct StackEffect {
    int pops{0};
    int pushes{0};
};

// Stack effect of the instruction at `offset`, whose operands must lie within
// `code`.
[[nodiscard]] StackEffect instruction_stack_effect(const std::vector<std::uint8_t>& code,
                                                   std::size_t offset);

// ─── Bytecode verifier ───
// A linear pass over the bytecode checks, at each instruction:
//   • opcode validity and instruction truncation
//   • constant pool index bounds
//   • name table index bounds
//   • upvalue index bounds
//   • local slot index bounds
//
// Jump target validation is deferred until after that pass, because forward
// jumps may target offsets not yet visited.  A target must be an instruction
// boundary strictly inside the code (a target equal to the code size has no
// instruction to run).
//
// Stack depth (underflow / overflow) is then checked by walking the control
// flow graph from the entry point: each reachable instruction is simulated
// with the deepest stack that reaches it along any path, following jump,
// loop, exception-handler, and loop-exhaustion edges.  The walk also reports
// any instruction whose fall-through path runs off the end of the code.

class BytecodeVerifier {
public:
    // Legacy type alias preserved for backward compatibility.
    using VerifyError = luma::VerifyError;

    // Verify a compiled function's chunk.  Returns an empty vector on success.
    [[nodiscard]] std::vector<VerifyError> verify(const CompiledFunction& func);
};

} // namespace luma

#endif // LUMA_COMPILER_VERIFIER_HPP
