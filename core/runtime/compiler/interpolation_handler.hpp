// ─────────────────────────────────────────────────────────────────────────────
// InterpolationHandler
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Compile string interpolation expressions (e.g.,
//   "hello ${name}!") into bytecode, including adjacent-literal merging and
//   compile-time folding of embedded string literal expressions.
//
// Design: Holds a non-owning reference to an ICompilationBackend interface, which
//   provides controlled access to Compiler's emit and compilation methods.
//
// ICompilationBackend methods used:
//   - emit_constant()       — emit each literal string part as a constant
//   - emit_u8()             — emit the Interpolate opcode with part count
//   - compile_expression()  — compile each embedded expression part
//   - error_limit_exceeded()— report over-size interpolation errors
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_INTERPOLATION_HANDLER_HPP
#define LUMA_COMPILER_INTERPOLATION_HANDLER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "analysis/ast/expression.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/compiler/compiler_helper.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

// Compiles string interpolation expressions into bytecode.
// Merges adjacent string literal parts at compile time to minimise the
// number of operands passed to the Interpolate opcode at runtime.
//
// Lifetime is bounded by the Compiler that owns it by value.
class InterpolationHandler : public CompilerHelper {
public:
    using CompilerHelper::CompilerHelper;

    // Compile a StringInterpolationExpression into bytecode.
    //
    // The algorithm works in two passes:
    //   1. Build a merged part list, combining adjacent string literals and
    //      folding embedded string-literal expressions into the surrounding
    //      text at compile time.
    //   2. Emit a Constant for every literal part and compile every
    //      expression part, then emit a single Interpolate opcode whose
    //      operand is the total part count.
    //
    // Special cases:
    //   - If all parts fold into a single string constant the Interpolate
    //     opcode is omitted entirely.
    //   - If the merged part count exceeds the 8-bit operand limit a
    //     compile-time error is reported.
    void compile(const StringInterpolationExpression& expr);

private:
    // A single element in the merged interpolation sequence.
    struct MergedPart {
        bool is_literal;
        std::string literal_value;
        std::size_t expression_index;
    };

    // Build the merged part list from the raw AST parts/expressions.
    [[nodiscard]] static std::vector<MergedPart>
    merge_parts(const StringInterpolationExpression& expr);
};

} // namespace luma

#endif // LUMA_COMPILER_INTERPOLATION_HANDLER_HPP
