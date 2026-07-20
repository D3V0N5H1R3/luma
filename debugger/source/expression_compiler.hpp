#ifndef LUMA_DAP_EXPRESSION_COMPILER_HPP
#define LUMA_DAP_EXPRESSION_COMPILER_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Shared Expression Compilation Utility
// ─────────────────────────────────────────────────────────────────────────────
// Provides common Lexer → Parser → Compiler pipeline functions used by the
// expression evaluator, compiled breakpoints, and debug session.
//
// TESTING SEAM: these free functions are the injection points for mocking the
// compilation pipeline in unit tests.  To substitute a test double, replace
// this translation unit (expression_compiler.cpp) with a stub that returns
// pre-built CompiledFunction / PipelineCompileResult values.  Callers
// (ExpressionEvaluator, CompiledBreakpointCache, DebugExecutionEngine) depend
// only on the function signatures declared here, not on the implementation.
// ─────────────────────────────────────────────────────────────────────────────

#include <optional>
#include <string>
#include <vector>

#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/compiler.hpp"

namespace luma {
class SourceManager;
} // namespace luma

namespace luma::dap {

// Result of a full pipeline compilation (TypeChecker + Compiler).
struct PipelineCompileResult {
    std::vector<CompiledFunction> functions;
    CompiledFunction top_level;
};

// Compile an expression using the direct Compiler in REPL mode (no type
// checker). Wraps the expression as: function boolean __bp_eval__() { return
// <expr> }. The declared return type is cosmetic — this path bypasses the type
// checker. The breakpoint-condition cache uses the result only to validate that
// the expression compiles, while the expression evaluator runs it via
// VM::execute_function to read the expression's value back.
// Returns the first compiled function, or nullopt on failure.
[[nodiscard]] std::optional<CompiledFunction>
compile_expression_direct(const std::string& expression, std::string& error_out);

// Compile a program from a source file through the full pipeline, including
// include resolution. Returns nullopt on failure.
[[nodiscard]] std::optional<PipelineCompileResult>
compile_program_pipeline(SourceManager& source_manager, const std::string& path,
                         std::string& error_out,
                         std::vector<std::string>* detailed_errors = nullptr);

} // namespace luma::dap

#endif // LUMA_DAP_EXPRESSION_COMPILER_HPP
