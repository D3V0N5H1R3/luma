#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/lexer/token.hpp"
#include "analysis/linter/linter.hpp"
#include "analysis/parser/parser.hpp"
#include "analysis/resolver/resolver.hpp"
#include "analysis/types/type_checker.hpp"
#include "fuzz_harness.hpp"

// Front-end pipeline-stage helpers shared by every fuzz target.
//
// These wrappers depend only on luma_analysis (lexer → parser → resolver →
// type checker → linter), so the analysis-only targets can include this
// header without pulling in any runtime/VM headers.  Compile/optimise/execute
// helpers live in fuzz_pipeline.hpp.

namespace luma::fuzz {

// True if any diagnostic is an error.  Mirrors PipelineResult::has_errors()
// but operates on a raw diagnostic list, which is what the analysis stages
// return.
[[nodiscard]] inline bool has_error(const std::vector<Diagnostic>& diagnostics) {
    return std::ranges::any_of(diagnostics,
                               [](const Diagnostic& d) { return d.severity == Severity::Error; });
}

// Touch every token so the work that produced them cannot be elided.
inline void consume_tokens(const std::vector<Token>& tokens) {
    for (const auto& tok : tokens) {
        do_not_optimize(tok.lexeme);
        do_not_optimize(tok.type);
    }
}

// Touch every diagnostic so the work that produced them cannot be elided.
inline void consume_diagnostics(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& d : diagnostics) {
        do_not_optimize(d.message);
        do_not_optimize(d.primary_location().line);
    }
}

// Stage 1 — tokenize.
[[nodiscard]] inline std::vector<Token> lex(const std::string& input) {
    DiagnosticCollector diagnostics;
    Lexer lexer{input, diagnostics};
    return lexer.tokenize();
}

// Outcome of parsing: the (possibly partially recovered) AST plus whether the
// parser reported any syntax errors.  `ok == true` means the AST is safe to
// feed to stages that assume well-formed input (e.g. the compiler).
struct ParseResult {
    Program program;
    bool ok;
};

// Stage 1 + 2 — tokenize and parse.
[[nodiscard]] inline ParseResult parse(const std::string& input) {
    Parser parser{lex(input)};
    auto program = parser.parse();
    return ParseResult{std::move(program), parser.get_errors().empty()};
}

// Stage 3 — resolve names in place.  Returns the resolver diagnostics.
[[nodiscard]] inline std::vector<Diagnostic> resolve(Program& program) {
    NameResolver resolver;
    return resolver.resolve(program);
}

// Stage 4 — type-check.  Returns the type diagnostics and exercises the
// warning accessor as a side effect.
[[nodiscard]] inline std::vector<Diagnostic> type_check(Program& program) {
    TypeChecker checker;
    auto diagnostics = checker.check(program, /*require_main=*/false);
    do_not_optimize(checker.get_warnings().size());
    return diagnostics;
}

// Lint pass — returns the linter warnings.
[[nodiscard]] inline std::vector<Diagnostic> lint(Program& program) {
    Linter linter;
    return linter.lint(program);
}

} // namespace luma::fuzz
