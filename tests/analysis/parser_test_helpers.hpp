// Shared helpers for the parser_test_* unit tests.

#ifndef LUMA_PARSER_TEST_HELPERS_HPP
#define LUMA_PARSER_TEST_HELPERS_HPP

#include "ast_test_util.hpp"
#include "test_framework.hpp"

namespace luma::test {

// The first top-level declaration, which must be a function. Checks the kind
// so a recovered AST with a different first declaration fails the test
// cleanly instead of invoking undefined behaviour through a wrong cast.
[[nodiscard]] inline const FunctionDeclaration& first_function(const Program& program) {
    if (program.declarations.empty()) {
        throw_assertion_error("first_function: program has no top-level declarations");
    }

    const auto& declaration = *program.declarations.front();

    if (declaration.kind != DeclarationKind::Function) {
        throw_assertion_error("first_function: first declaration is not a function");
    }

    return static_cast<const FunctionDeclaration&>(declaration);
}

} // namespace luma::test

#endif // LUMA_PARSER_TEST_HELPERS_HPP
