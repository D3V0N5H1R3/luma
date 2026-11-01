#ifndef LUMA_FUZZ_FUZZ_FRONTEND_HPP
#define LUMA_FUZZ_FUZZ_FRONTEND_HPP

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/lexer/token.hpp"
#include "analysis/parser/parser.hpp"
#include "analysis/pipeline/lint_pass.hpp"
#include "analysis/pipeline/pipeline.hpp"
#include "analysis/pipeline/type_checker_pass.hpp"
#include "analysis/types/type_checker.hpp"
#include "fuzz_harness.hpp"

// Front-end pipeline-stage helpers shared by every fuzz target.
//
// These wrappers depend only on luma_analysis (lexer → parser → type checker →
// linter), so the analysis-only targets can include this header without
// pulling in any runtime/VM headers.  Compile/optimise/execute helpers live in
// fuzz_pipeline.hpp.

namespace luma::fuzz {

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

// Stage 1 + 2 with the production gate applied: like compile_program(), refuse
// to go further when the parser reported syntax errors.
[[nodiscard]] inline std::optional<Program> parse_clean(const std::string& input) {
    auto parsed = parse(input);
    if (!parsed.ok) {
        return std::nullopt;
    }
    return std::move(parsed.program);
}

// Stage 3 — type-check.  Returns the type diagnostics and exercises the
// warning accessor as a side effect.
[[nodiscard]] inline std::vector<Diagnostic> type_check(Program& program) {
    TypeChecker checker;
    auto diagnostics = checker.check(program, /*require_main=*/false);
    do_not_optimize(checker.get_warnings().size());
    return diagnostics;
}

// Stages 3 + 4 exactly as production runs them: the linter relies on the
// annotations the type checker leaves on the AST, and LintPass still runs after
// a type-check failure, so lint always follows a type check.
[[nodiscard]] inline PipelineResult type_check_and_lint(Program& program) {
    auto pipeline =
        Pipeline::builder().add<TypeCheckerPass>(/*require_main=*/false).add<LintPass>().build();
    return pipeline.run(program);
}

} // namespace luma::fuzz

#endif // LUMA_FUZZ_FUZZ_FRONTEND_HPP
