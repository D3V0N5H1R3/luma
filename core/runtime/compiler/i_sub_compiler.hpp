// ─────────────────────────────────────────────────────────────────────────────
// ISubCompiler — Narrow interface for recursive sub-compilation
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Recursively compile nested expressions and statements, and
//   perform compile-time constant folding of binary literal expressions.
//
// Part of the ICompilationBackend interface-segregation (ISP) split.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_I_SUB_COMPILER_HPP
#define LUMA_COMPILER_I_SUB_COMPILER_HPP

#include <vector>

#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/source/source_location.hpp"

namespace luma {

struct LiteralExpression;

// Recursive sub-expression/statement compilation surface.
class ISubCompiler {
public:
    virtual ~ISubCompiler() = default;

    virtual void compile_expression(const Expression& expr) = 0;
    virtual void compile_statement(const Statement& stmt) = 0;
    virtual void compile_body_as_expression(const std::vector<StatementPtr>& body,
                                            SourceLocation loc) = 0;
    [[nodiscard]] virtual bool try_fold_binary_at_compile_time(const LiteralExpression& lhs,
                                                               const LiteralExpression& rhs,
                                                               TokenType op,
                                                               SourceLocation loc) = 0;

protected:
    ISubCompiler() = default;
    ISubCompiler(const ISubCompiler&) = default;
    ISubCompiler& operator=(const ISubCompiler&) = default;
};

} // namespace luma

#endif // LUMA_COMPILER_I_SUB_COMPILER_HPP
