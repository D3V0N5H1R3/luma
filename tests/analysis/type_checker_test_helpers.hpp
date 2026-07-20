// Shared helpers for type checker unit tests.
// See stdlib_test_helpers.hpp header comment for why these helpers are not unified.

#ifndef LUMA_TYPE_CHECKER_TEST_HELPERS_HPP
#define LUMA_TYPE_CHECKER_TEST_HELPERS_HPP

#include <algorithm>
#include <string>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/types/type_checker.hpp"
#include "test_parse_helper.hpp"

// ─── Helpers ───

static std::vector<Diagnostic> check(const std::string& source, bool require_main = false) {
    const auto program = parse(source);

    TypeChecker checker;

    return checker.check(program, require_main);
}

[[maybe_unused]] static bool passes(const std::string& source) {
    return check(source).empty();
}

[[maybe_unused]] static bool fails(const std::string& source) {
    return !check(source).empty();
}

static std::vector<Diagnostic> check_warnings(const std::string& source,
                                              bool require_main = false) {
    const auto program = parse(source);

    TypeChecker checker;

    [[maybe_unused]] const auto errors = checker.check(program, require_main);

    return checker.get_warnings();
}

[[maybe_unused]] static bool has_warnings(const std::string& source) {
    return !check_warnings(source).empty();
}

// ─── Diagnostic-code identity helpers ───
// These assert *which* rule fired, not merely that some diagnostic occurred, so
// negative tests can't silently pass for the wrong reason (e.g. a parse error
// standing in for the intended type error).

// True if any diagnostic in the list carries the given code.
[[maybe_unused]] static bool has_code(const std::vector<Diagnostic>& diagnostics,
                                      DiagnosticCode code) {
    return std::ranges::any_of(diagnostics, [code](const Diagnostic& d) { return d.code == code; });
}

// True if type-checking `source` fails with a diagnostic carrying `code`.
// Stronger than fails(): pins the diagnostic identity.
[[maybe_unused]] static bool fails_with(const std::string& source, DiagnosticCode code) {
    return has_code(check(source), code);
}

#endif // LUMA_TYPE_CHECKER_TEST_HELPERS_HPP
