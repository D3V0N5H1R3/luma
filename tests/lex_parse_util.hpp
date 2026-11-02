// Lightweight lex → parse utility shared by the parser, type-checker, and
// linter test suites.
//
// Depends only on the analysis front-end (lexer + parser + diagnostics), so
// including it does NOT pull in the compiler / VM / stdlib backend. This keeps
// pure-analysis translation units (parser_test_*, type_checker_test_*,
// linter_test) from compiling the runtime backend they never link. The
// full-pipeline evaluators in shared_eval.hpp reuse lex_and_parse() from here
// so the lex→parse boilerplate lives in exactly one place.

#ifndef LUMA_LEX_PARSE_UTIL_HPP
#define LUMA_LEX_PARSE_UTIL_HPP

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/parser/parser.hpp"

namespace luma::test {

// A parsed program together with the lexer's and the parser's errors (lexer
// warnings are dropped).
struct ParseOutcome {
    Program program;
    std::vector<Diagnostic> lexer_errors;
    std::vector<Diagnostic> errors;
};

// Lex and parse source text, keeping the lexer's and the parser's errors.
[[nodiscard]] inline ParseOutcome lex_and_parse_with_errors(const std::string& source) {
    DiagnosticCollector lexer_diagnostics;
    Lexer lexer{source, lexer_diagnostics};
    auto tokens = lexer.tokenize();

    Parser parser{std::move(tokens)};
    auto program = parser.parse();

    std::vector<Diagnostic> lexer_errors;
    std::ranges::copy_if(lexer_diagnostics.diagnostics(), std::back_inserter(lexer_errors),
                         [](const Diagnostic& d) { return d.severity == Severity::Error; });

    return ParseOutcome{std::move(program), std::move(lexer_errors), parser.get_errors()};
}

// Lex and parse source text into a Program AST, discarding diagnostics.
[[nodiscard]] inline Program lex_and_parse(const std::string& source) {
    return lex_and_parse_with_errors(source).program;
}

// Lex and parse source text, returning only the parser's syntax errors.
[[nodiscard]] inline std::vector<Diagnostic> parse_errors(const std::string& source) {
    return lex_and_parse_with_errors(source).errors;
}

// Join diagnostic messages into one indented block for an exception message.
[[nodiscard]] inline std::string describe_diagnostics(const std::vector<Diagnostic>& diagnostics) {
    std::string text;

    for (const auto& diagnostic : diagnostics) {
        text += "\n  " + diagnostic.message;
    }

    return text;
}

// Lex and parse source text that must be valid, throwing (with every lexer and
// parser error) otherwise. The parser recovers from errors, so silently using
// its partial program would test something other than the source.
[[nodiscard]] inline Program lex_and_parse_valid(const std::string& source) {
    auto parsed = lex_and_parse_with_errors(source);

    if (!parsed.lexer_errors.empty() || !parsed.errors.empty()) {
        throw std::runtime_error{"Parse failed:" + describe_diagnostics(parsed.lexer_errors) +
                                 describe_diagnostics(parsed.errors)};
    }

    return std::move(parsed.program);
}

} // namespace luma::test

#endif // LUMA_LEX_PARSE_UTIL_HPP
