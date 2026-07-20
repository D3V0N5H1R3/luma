// Lightweight lex → parse utility shared by the parser, type-checker,
// resolver, and linter test suites.
//
// Depends only on the analysis front-end (lexer + parser + diagnostics), so
// including it does NOT pull in the compiler / VM / stdlib backend. This keeps
// pure-analysis translation units (parser_test, type_checker_test_*,
// resolver_test, linter_test) from compiling the runtime backend they never
// link. The full-pipeline evaluators in shared_eval.hpp reuse lex_and_parse()
// from here so the lex→parse boilerplate lives in exactly one place.

#ifndef LUMA_LEX_PARSE_UTIL_HPP
#define LUMA_LEX_PARSE_UTIL_HPP

#include <string>
#include <utility>

#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/parser/parser.hpp"

namespace luma::test {

// Lex and parse source text into a Program AST, discarding diagnostics.
[[nodiscard]] inline Program lex_and_parse(const std::string& source) {
    DiagnosticCollector discarded;
    Lexer lexer{source, discarded};
    auto tokens = lexer.tokenize();

    Parser parser{std::move(tokens)};
    return parser.parse();
}

} // namespace luma::test

#endif // LUMA_LEX_PARSE_UTIL_HPP
