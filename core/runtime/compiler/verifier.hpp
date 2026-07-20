#ifndef LUMA_COMPILER_VERIFIER_HPP
#define LUMA_COMPILER_VERIFIER_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "runtime/compiler/chunk.hpp"

namespace luma {

// ─── Verification error ───

struct VerifyError {
    std::size_t offset{0};
    std::string message;
};

// ─── Single-pass bytecode verifier ───
// Iterates bytecode once, performing all verification checks at each
// instruction:
//   • opcode validity and instruction truncation
//   • constant pool index bounds
//   • name table index bounds
//   • upvalue index bounds
//   • local slot index bounds
//   • stack depth tracking (underflow / overflow)
//
// Jump target validation is deferred until after the iteration completes,
// because forward jumps may target offsets not yet visited.  The deferred
// phase iterates only the (small) set of collected jump records, not the
// full bytecode.

class BytecodeVerifier {
public:
    // Legacy type alias preserved for backward compatibility.
    using VerifyError = luma::VerifyError;

    // Verify a compiled function's chunk.  Returns an empty vector on success.
    [[nodiscard]] std::vector<VerifyError> verify(const CompiledFunction& func);
};

} // namespace luma

#endif // LUMA_COMPILER_VERIFIER_HPP
