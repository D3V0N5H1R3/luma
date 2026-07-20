// DAP expression-compiler tests — the shared compilation seam used by the
// expression evaluator, compiled breakpoints, and the debug session.
//
// compile_expression_direct() wraps an expression in a REPL-mode function and
// reports success/failure without a VM.  compile_program_pipeline() runs the
// full TypeChecker + Compiler pipeline on a source file (written via TempFile),
// covering both the parse-error and type-error failure paths.

#include <optional>
#include <string>
#include <vector>

#include "analysis/source/source_manager.hpp"
#include "expression_compiler.hpp"
#include "test_framework.hpp"

using namespace luma::dap;
using luma::SourceManager;

namespace {

// ─── compile_expression_direct: success ───────────────────────────

void test_compile_arithmetic_expression() {
    std::string error;
    const auto compiled = compile_expression_direct("1 + 2", error);

    ASSERT_TRUE(compiled.has_value());
    ASSERT_TRUE(error.empty());
    // The expression is wrapped in a synthetic __bp_eval__ function.
    ASSERT_EQ(compiled->name, std::string("__bp_eval__"));
}

void test_compile_boolean_literal() {
    std::string error;
    const auto compiled = compile_expression_direct("true", error);
    ASSERT_TRUE(compiled.has_value());
    ASSERT_TRUE(error.empty());
}

void test_compile_parenthesised_expression() {
    std::string error;
    const auto compiled = compile_expression_direct("(1 + 2) * 3", error);
    ASSERT_TRUE(compiled.has_value());
    ASSERT_TRUE(error.empty());
}

void test_compile_string_expression() {
    std::string error;
    const auto compiled = compile_expression_direct(R"("hello")", error);
    ASSERT_TRUE(compiled.has_value());
    ASSERT_TRUE(error.empty());
}

void test_compile_free_identifier_resolves_as_global() {
    // REPL-mode compilation resolves unknown identifiers as globals (looked up
    // at run time), so a bare identifier compiles successfully.
    std::string error;
    const auto compiled = compile_expression_direct("some_variable", error);
    ASSERT_TRUE(compiled.has_value());
    ASSERT_TRUE(error.empty());
}

// ─── compile_expression_direct: failure ───────────────────────────

void test_compile_incomplete_expression_fails() {
    std::string error;
    const auto compiled = compile_expression_direct("1 +", error);
    ASSERT_FALSE(compiled.has_value());
    ASSERT_FALSE(error.empty());
}

void test_compile_unbalanced_parens_fails() {
    std::string error;
    const auto compiled = compile_expression_direct("(1 + 2", error);
    ASSERT_FALSE(compiled.has_value());
    ASSERT_FALSE(error.empty());
}

void test_compile_leading_operator_fails() {
    std::string error;
    const auto compiled = compile_expression_direct("* 5", error);
    ASSERT_FALSE(compiled.has_value());
    ASSERT_FALSE(error.empty());
}

// ─── compile_program_pipeline: success ────────────────────────────

void test_pipeline_compiles_valid_program() {
    const TempFile program{"@main\nfunction void main() {\n    print(\"hi\")\n}\n"};

    SourceManager source_manager;
    std::string error;
    const auto result = compile_program_pipeline(source_manager, program.path_string(), error);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(error.empty());
    // The program defines at least the main function.
    ASSERT_FALSE(result->functions.empty());
}

// ─── compile_program_pipeline: failure ────────────────────────────

void test_pipeline_reports_syntax_error() {
    // Malformed parameter list — the parser rejects it before type checking.
    const TempFile program{"@main\nfunction void main( {\n    print(\"hi\")\n}\n"};

    SourceManager source_manager;
    std::string error;
    const auto result = compile_program_pipeline(source_manager, program.path_string(), error);

    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(error.empty());
}

void test_pipeline_reports_type_error() {
    // Parses cleanly but assigns a string to an integer — a type error surfaced
    // by the TypeChecker pass rather than the parser.
    const TempFile program{"@main\nfunction void main() {\n    integer x = \"nope\"\n}\n"};

    SourceManager source_manager;
    std::string error;
    const auto result = compile_program_pipeline(source_manager, program.path_string(), error);

    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(error.empty());
}

void test_pipeline_populates_detailed_errors() {
    const TempFile program{"@main\nfunction void main( {\n    print(\"hi\")\n}\n"};

    SourceManager source_manager;
    std::string error;
    std::vector<std::string> detailed;
    const auto result =
        compile_program_pipeline(source_manager, program.path_string(), error, &detailed);

    ASSERT_FALSE(result.has_value());
    // With a detailed sink provided, per-diagnostic messages are collected.
    ASSERT_FALSE(detailed.empty());
}

void test_pipeline_reports_missing_file() {
    SourceManager source_manager;
    std::string error;
    const auto result = compile_program_pipeline(source_manager, "no_such_program_zzz.luma", error);

    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(error.empty());
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP Expression Compiler Tests");

    // compile_expression_direct — success.
    RUN(test_compile_arithmetic_expression);
    RUN(test_compile_boolean_literal);
    RUN(test_compile_parenthesised_expression);
    RUN(test_compile_string_expression);
    RUN(test_compile_free_identifier_resolves_as_global);

    // compile_expression_direct — failure.
    RUN(test_compile_incomplete_expression_fails);
    RUN(test_compile_unbalanced_parens_fails);
    RUN(test_compile_leading_operator_fails);

    // compile_program_pipeline — success.
    RUN(test_pipeline_compiles_valid_program);

    // compile_program_pipeline — failure.
    RUN(test_pipeline_reports_syntax_error);
    RUN(test_pipeline_reports_type_error);
    RUN(test_pipeline_populates_detailed_errors);
    RUN(test_pipeline_reports_missing_file);

    return SUMMARY();
}
