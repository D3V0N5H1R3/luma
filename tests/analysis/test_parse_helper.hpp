// Shared helpers for parser unit tests.
//
// Provides parse() and parse_errors() so that any test file that needs to
// lex → parse source text can reuse the same boilerplate. Both delegate to
// lex_parse_util.hpp, which owns the lex → parse logic.

#ifndef LUMA_TEST_PARSE_HELPER_HPP
#define LUMA_TEST_PARSE_HELPER_HPP

#include <string>

#include "lex_parse_util.hpp"
#include "test_framework.hpp"

namespace luma::test {

// Lex and parse source text, discarding diagnostics.
[[nodiscard]] inline Program parse(const std::string& source) {
    return lex_and_parse(source);
}

} // namespace luma::test

#endif // LUMA_TEST_PARSE_HELPER_HPP
