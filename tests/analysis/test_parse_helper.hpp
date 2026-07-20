// Shared helpers for parser unit tests.
//
// Provides parse() and parse_errors() so that any test file that needs to
// lex → parse source text can reuse the same boilerplate.

#ifndef LUMA_TEST_PARSE_HELPER_HPP
#define LUMA_TEST_PARSE_HELPER_HPP

#include <string>
#include <vector>

#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/parser/parser.hpp"
#include "lex_parse_util.hpp"
#include "test_framework.hpp"

// Test-only convenience: this header is included exclusively by test
// translation units (never by any library or production header), so pulling
// the luma namespace into scope here keeps test bodies concise without leaking
// into shipped code.
using namespace luma;

// Lex and parse source text, discarding diagnostics.
// Delegates to the canonical lex_and_parse() in lex_parse_util.hpp.
[[maybe_unused]] static Program parse(const std::string& source) {
    return luma::test::lex_and_parse(source);
}

// Lex, parse, and return the collected syntax errors.
[[maybe_unused]] static std::vector<Diagnostic> parse_errors(const std::string& source) {
    DiagnosticCollector discarded;
    Lexer lexer{source, discarded};

    auto tokens = lexer.tokenize();

    Parser parser{std::move(tokens)};

    [[maybe_unused]] const auto program = parser.parse();

    return {parser.get_errors().begin(), parser.get_errors().end()};
}

#endif // LUMA_TEST_PARSE_HELPER_HPP
