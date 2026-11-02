// Shared helpers for type checker unit tests.
// See stdlib_test_helpers.hpp header comment for why these helpers are not unified.

#ifndef LUMA_TYPE_CHECKER_TEST_HELPERS_HPP
#define LUMA_TYPE_CHECKER_TEST_HELPERS_HPP

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/linter/linter.hpp"
#include "analysis/types/type_checker.hpp"
#include "test_parse_helper.hpp"

namespace luma::test {

// ─── Helpers ───
//
// Each helper throws if the source has a lexer or parser error, so a snippet
// with a typo can never pass (or fail) type checking for the wrong reason.

// Type-check `source` as a snippet (no @main required) and return its errors.
[[nodiscard]] inline std::vector<Diagnostic> check(const std::string& source) {
    const auto program = lex_and_parse_valid(source);

    TypeChecker checker;

    return checker.check(program, /*require_main=*/false);
}

[[nodiscard]] inline bool passes(const std::string& source) {
    return check(source).empty();
}

[[nodiscard]] inline bool fails(const std::string& source) {
    return !check(source).empty();
}

// Type-check `source` as a snippet and return the type checker's warnings.
[[nodiscard]] inline std::vector<Diagnostic> check_warnings(const std::string& source) {
    const auto program = lex_and_parse_valid(source);

    TypeChecker checker;

    [[maybe_unused]] const auto errors = checker.check(program, /*require_main=*/false);

    return checker.get_warnings();
}

// Warnings from the type checker followed by the linter, mirroring the
// pipeline's TypeCheckerPass → LintPass order. The linter owns the
// variable-usage rules (unused, never-mutated, shadowed, self-assignment).
[[nodiscard]] inline std::vector<Diagnostic> check_warnings_with_lint(const std::string& source) {
    const auto program = lex_and_parse_valid(source);

    TypeChecker checker;

    [[maybe_unused]] const auto errors = checker.check(program, /*require_main=*/true);

    auto warnings = checker.get_warnings();

    Linter linter;

    for (auto& lint_warning : linter.lint(program)) {
        warnings.push_back(std::move(lint_warning));
    }

    return warnings;
}

[[nodiscard]] inline bool has_warnings(const std::string& source) {
    return !check_warnings(source).empty();
}

// ─── Diagnostic-code identity helpers ───
// These assert *which* rule fired, not merely that some diagnostic occurred, so
// negative tests can't silently pass for the wrong reason (e.g. a parse error
// standing in for the intended type error).

// True if any diagnostic in the list carries the given code.
[[nodiscard]] inline bool has_code(const std::vector<Diagnostic>& diagnostics,
                                   DiagnosticCode code) {
    return std::ranges::any_of(diagnostics, [code](const Diagnostic& d) { return d.code == code; });
}

// True if type-checking `source` fails with a diagnostic carrying `code`.
// Stronger than fails(): pins the diagnostic identity.
[[nodiscard]] inline bool fails_with(const std::string& source, DiagnosticCode code) {
    return has_code(check(source), code);
}

// True if type-checking `source` fails with a diagnostic whose message
// contains `fragment` — pins the rule for diagnostics that carry no code.
[[nodiscard]] inline bool fails_with_message(const std::string& source,
                                             const std::string& fragment) {
    return std::ranges::any_of(check(source), [&fragment](const Diagnostic& d) {
        return d.message.find(fragment) != std::string::npos;
    });
}

} // namespace luma::test

#endif // LUMA_TYPE_CHECKER_TEST_HELPERS_HPP
