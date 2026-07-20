// Shared AST-navigation helpers for parser tests.
//
// Small, dependency-light accessors that pull common nodes out of a parsed
// Program so multiple test suites don't each re-roll the same casts. Kept in
// namespace luma::test alongside lex_and_parse(); consumers add a using-
// declaration (e.g. `using luma::test::first_initializer;`) to call them
// unqualified.

#ifndef LUMA_AST_TEST_UTIL_HPP
#define LUMA_AST_TEST_UTIL_HPP

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"

namespace luma::test {

// The initializer expression of the first top-level variable declaration — a
// convenient way to obtain an arbitrary parsed expression from a program.
[[nodiscard]] inline const Expression& first_initializer(const Program& program) {
    const auto& var = static_cast<const VariableDeclStatement&>(*program.statements.at(0));
    return *var.initializer;
}

// Downcast an expression to a binary expression node.
[[nodiscard]] inline const BinaryExpression& as_binary(const Expression& expr) {
    return static_cast<const BinaryExpression&>(expr);
}

// Downcast an expression to a unary expression node.
[[nodiscard]] inline const UnaryExpression& as_unary(const Expression& expr) {
    return static_cast<const UnaryExpression&>(expr);
}

} // namespace luma::test

#endif // LUMA_AST_TEST_UTIL_HPP
