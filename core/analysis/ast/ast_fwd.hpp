#ifndef LUMA_AST_FWD_HPP
#define LUMA_AST_FWD_HPP

// Lightweight forward declarations for the core AST types.
// Include this header instead of the full AST headers when only
// pointers or references to AST nodes are needed.

#include <memory>

namespace luma {

// ─── AST node forward declarations ───

struct Expression;
struct Statement;
struct Declaration;
struct Program;

// ─── Owning pointer aliases ───

using ExpressionPtr = std::unique_ptr<Expression>;
using StatementPtr = std::unique_ptr<Statement>;
using DeclarationPtr = std::unique_ptr<Declaration>;

} // namespace luma

#endif // LUMA_AST_FWD_HPP
