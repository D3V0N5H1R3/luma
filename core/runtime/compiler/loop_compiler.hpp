// ─────────────────────────────────────────────────────────────────────────────
// LoopCompiler
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Compile for and while loop constructs, extracted from the
//   Compiler class.
//
// Design: Holds a non-owning reference to an ICompilationBackend interface, which
//   provides controlled access to Compiler's emit, scope, and compilation
//   methods.
//
// ICompilationBackend methods used:
//   - emit()               — single-byte opcodes (Pop, ForIterStep, etc.)
//   - emit_u16()           — GetLocal / SetLocal with slot indices
//   - emit_constant()      — destructuring index constants
//   - emit_jump()          — exit-jump emission
//   - emit_loop()          — backward-jump (loop) emission
//   - patch_jump()         — back-patching the exit jump
//   - current_offset()     — record the loop-back target before the test
//   - begin_loop()         — enter loop context (break/continue tracking)
//   - end_loop()           — exit loop context
//   - declare_local()      — declare iterator and loop variables
//   - compile_expression() — condition and iterable compilation
//   - compile_statement()  — loop body statements
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_LOOP_COMPILER_HPP
#define LUMA_COMPILER_LOOP_COMPILER_HPP

#include "analysis/ast/statement.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/compiler/compiler_helper.hpp"

namespace luma {

struct ForIterationState;

// Compiles for and while loop constructs.
// Lifetime is bounded by the Compiler that owns it by value.
class LoopCompiler : public CompilerHelper {
public:
    using CompilerHelper::CompilerHelper;

    void compile_for(const ForStatement& stmt);
    void compile_while(const WhileStatement& stmt);

private:
    void compile_for_iteration(const ForStatement& stmt, const ForIterationState& state);

    /// Declares a loop variable with a None placeholder on the stack.
    /// Returns the allocated slot index.
    [[nodiscard]] std::uint16_t declare_loop_variable(std::string_view name,
                                                      const SourceLocation& loc);
};

} // namespace luma

#endif // LUMA_COMPILER_LOOP_COMPILER_HPP
