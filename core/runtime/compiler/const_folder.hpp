// ─────────────────────────────────────────────────────────────────────────────
// ConstantFolder
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Evaluate binary/unary operations on literal operands at
//   compile time and emit the result as a single constant, extracted from
//   the Compiler god class.
//
// Design: Holds a non-owning reference to an ICompilationBackend interface.
//   The private try_fold_with<> template helper is defined in const_folder.cpp
//   to avoid pulling the full Compiler definition into this header.
//
// ICompilationBackend methods used (narrowest subset of all helpers):
//   - emit()          — to emit Op::True / Op::False for boolean folds
//   - emit_constant() — to emit the folded numeric/string constant
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_CONST_FOLDER_HPP
#define LUMA_COMPILER_CONST_FOLDER_HPP

#include <cstdint>
#include <string>

#include "analysis/lexer/token_type.hpp"
#include "analysis/source/source_location.hpp"

namespace luma {

class IConstantEmitter;
struct LiteralExpression;

// Compile-time constant folding for the Luma compiler.
//
// Each try_fold_* method returns true if folding succeeded (the result was
// emitted into the current chunk as a constant), or false if the operands
// cannot be folded and the caller should fall back to runtime evaluation.
class ConstantFolder {
public:
    explicit ConstantFolder(IConstantEmitter& api) noexcept : api_(api) {}

    // Primary entry point: attempt to fold a binary expression whose both
    // sides are known literal values.
    [[nodiscard]] bool try_fold_binary_at_compile_time(const LiteralExpression& lhs,
                                                       const LiteralExpression& rhs, TokenType op,
                                                       SourceLocation loc);

    [[nodiscard]] bool try_fold_integer_arithmetic(std::int64_t l, std::int64_t r, TokenType op,
                                                   SourceLocation loc);

    [[nodiscard]] bool try_fold_number_arithmetic(double l, double r, TokenType op,
                                                  SourceLocation loc);

    [[nodiscard]] bool try_fold_string_concatenation(const std::string& l, const std::string& r,
                                                     TokenType op, SourceLocation loc);

    [[nodiscard]] bool try_fold_comparison(double l, double r, TokenType op, SourceLocation loc);

private:
    // Evaluate a constant expression by calling `compute`.  If it returns a
    // Value, emit it into the current chunk and return true.
    // Defined in const_folder.cpp to break the circular header dependency.
    template <typename ComputeFn> bool try_fold_with(SourceLocation loc, ComputeFn compute);

    IConstantEmitter& api_;
};

} // namespace luma

#endif // LUMA_COMPILER_CONST_FOLDER_HPP
