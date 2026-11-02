// Shared AST-navigation helpers for parser tests.
//
// Small, dependency-light accessors that pull common nodes out of a parsed
// Program so multiple test suites don't each re-roll the same casts. Every
// downcast first checks the node's kind and fails the test with a clear message
// on a mismatch, rather than invoking undefined behaviour through a wrong
// static_cast.

#ifndef LUMA_AST_TEST_UTIL_HPP
#define LUMA_AST_TEST_UTIL_HPP

#include <string_view>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "test_framework.hpp"

namespace luma::test {

// Downcast an expression to a binary expression node.
[[nodiscard]] inline const BinaryExpression& as_binary(const Expression& expr) {
    if (expr.kind != ExpressionKind::Binary) {
        throw_assertion_error("as_binary: expression is not a binary expression");
    }

    return static_cast<const BinaryExpression&>(expr);
}

// Downcast an expression to a unary expression node.
[[nodiscard]] inline const UnaryExpression& as_unary(const Expression& expr) {
    if (expr.kind != ExpressionKind::Unary) {
        throw_assertion_error("as_unary: expression is not a unary expression");
    }

    return static_cast<const UnaryExpression&>(expr);
}

// The initializer expression of the first top-level variable declaration — a
// convenient way to obtain an arbitrary parsed expression from a program.
[[nodiscard]] inline const Expression& first_initializer(const Program& program) {
    if (program.statements.empty()) {
        throw_assertion_error("first_initializer: program has no top-level statements");
    }

    const auto& statement = *program.statements.front();

    if (statement.kind != StatementKind::VariableDeclaration) {
        throw_assertion_error("first_initializer: first statement is not a variable declaration");
    }

    const auto& declaration = static_cast<const VariableDeclStatement&>(statement);

    if (!declaration.initializer) {
        throw_assertion_error("first_initializer: variable declaration has no initializer");
    }

    return *declaration.initializer;
}

} // namespace luma::test

#endif // LUMA_AST_TEST_UTIL_HPP
