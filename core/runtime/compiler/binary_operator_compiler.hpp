// ─────────────────────────────────────────────────────────────────────────────
// BinaryOperatorCompiler
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Compile binary and compound-assignment operators, extracted
//   from the Compiler class.
//
// Design: Holds a non-owning reference to an ICompilationBackend interface, which
//   provides controlled access to Compiler's emit and compilation methods.
//
// ICompilationBackend methods used:
//   - emit()               — opcode emission for operators and pops
//   - emit_jump()          — short-circuit and null-coalesce jump emission
//   - patch_jump()         — back-patching short-circuit exit jumps
//   - compile_expression() — recursive sub-expression compilation
//   - try_fold_binary_at_compile_time()— constant folding delegation
//   - error()              — unknown operator diagnostics
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_BINARY_OPERATOR_COMPILER_HPP
#define LUMA_COMPILER_BINARY_OPERATOR_COMPILER_HPP

#include <array>
#include <optional>

#include "analysis/ast/expression.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/compiler/compiler_helper.hpp"
#include "runtime/compiler/opcode.hpp"

namespace luma {

// ─── Token-to-opcode lookup functions ───────────────────────────────────────
// Centralised mappings used by BinaryOperatorCompiler and any other
// component that needs to convert a token to its corresponding opcode.
// Declared here so callers do not need to know the internal table layout.

// Returns the binary operator opcode for a given token, or nullopt if the
// token is not a binary operator.
[[nodiscard]] constexpr std::optional<Op> binary_op_for_token(TokenType t) noexcept;

// Returns the arithmetic/bitwise opcode that a compound-assignment token
// (+=, -=, …) applies, or nullopt if the token is not a compound-assignment
// operator.
[[nodiscard]] constexpr std::optional<Op> compound_op_for_token(TokenType t) noexcept;

// Compiles binary operators (arithmetic, comparison, logical, bitwise,
// string concatenation) and compound-assignment operators (+=, -=, etc.).
// Lifetime is bounded by the Compiler that owns it by value.
class BinaryOperatorCompiler : public CompilerHelper {
public:
    using CompilerHelper::CompilerHelper;

    void compile_binary(const BinaryExpression& expr);
    void emit_compound_op(TokenType op, SourceLocation loc);

private:
    void compile_short_circuit(const BinaryExpression& expr, Op jump_op, bool is_and);
};

} // namespace luma

#endif // LUMA_COMPILER_BINARY_OPERATOR_COMPILER_HPP
