// Unit tests for the line/token-based Luma source formatter engine
// (lsp_text_formatter), exercised directly without the LSP protocol layer.

#include <string>

#include "lsp_text_formatter.hpp"
#include "test_framework.hpp"

using luma::lsp::format_luma_source;
using luma::lsp::format_range_text;

namespace {

// ─── Whitespace normalization ──────────────────────────────────────

void test_strips_trailing_whitespace() {
    ASSERT_EQ(format_luma_source("foo   \n", 4), std::string("foo\n"));
}

void test_ensures_single_trailing_newline() {
    // Missing trailing newline is added.
    ASSERT_EQ(format_luma_source("foo", 4), std::string("foo\n"));
    // Multiple trailing newlines collapse to one.
    ASSERT_EQ(format_luma_source("foo\n\n\n", 4), std::string("foo\n"));
}

void test_caps_consecutive_blank_lines() {
    // At most two consecutive blank lines are preserved between content.
    ASSERT_EQ(format_luma_source("a\n\n\n\n\nb\n", 4), std::string("a\n\n\nb\n"));
}

// ─── Operator spacing ──────────────────────────────────────────────

void test_normalizes_assignment_spacing() {
    ASSERT_EQ(format_luma_source("x=1\n", 4), std::string("x = 1\n"));
}

void test_normalizes_two_char_operators() {
    ASSERT_EQ(format_luma_source("if a==b {\n}\n", 4), std::string("if a == b {\n}\n"));
}

void test_normalizes_pipe_operator() {
    ASSERT_EQ(format_luma_source("y=x|>f()\n", 4), std::string("y = x |> f()\n"));
}

void test_leaves_string_contents_untouched() {
    // The '=' inside the string literal must not be re-spaced.
    ASSERT_EQ(format_luma_source("x=\"a=b\"\n", 4), std::string("x = \"a=b\"\n"));
}

// Characterization: operators inside a ${...} interpolation are NOT re-spaced
// (the formatter treats interpolation content as string context). Locks current
// behaviour before any future unification of the lexical-context scanners.
void test_leaves_interpolation_operators_untouched() {
    ASSERT_EQ(format_luma_source("x=\"${a+b}\"\n", 4), std::string("x = \"${a+b}\"\n"));
}

// Characterization: operators inside a trailing line comment are NOT re-spaced.
void test_leaves_comment_contents_untouched() {
    ASSERT_EQ(format_luma_source("x=1 # a=b\n", 4), std::string("x = 1 # a=b\n"));
}

// ─── Indentation ───────────────────────────────────────────────────

void test_indents_braced_block() {
    ASSERT_EQ(format_luma_source("function f() {\nreturn 1\n}\n", 4),
              std::string("function f() {\n    return 1\n}\n"));
}

void test_indent_respects_tab_size() {
    ASSERT_EQ(format_luma_source("function f() {\nreturn 1\n}\n", 2),
              std::string("function f() {\n  return 1\n}\n"));
}

// ─── Declaration separation ────────────────────────────────────────

void test_inserts_blank_line_before_declaration() {
    ASSERT_EQ(format_luma_source("x = 1\nfunction f() {\n}\n", 4),
              std::string("x = 1\n\nfunction f() {\n}\n"));
}

// ─── Idempotency ───────────────────────────────────────────────────

void test_formatting_is_idempotent() {
    const std::string input = "x=1\nfunction f() {\nreturn x|>g()\n}\n";
    const auto once = format_luma_source(input, 4);
    const auto twice = format_luma_source(once, 4);
    ASSERT_EQ(once, twice);
}

// ─── Range formatting ──────────────────────────────────────────────

void test_range_formatting_preserves_base_indent() {
    // A range nested one level deep keeps its 4-space base indent while the
    // operator spacing inside it is still normalized.
    ASSERT_EQ(format_range_text("    x=1\n", 4), std::string("    x = 1\n"));
}

void test_range_formatting_no_base_indent() {
    ASSERT_EQ(format_range_text("x=1\n", 4), std::string("x = 1\n"));
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
    RUN(test_strips_trailing_whitespace);
    RUN(test_ensures_single_trailing_newline);
    RUN(test_caps_consecutive_blank_lines);
    RUN(test_normalizes_assignment_spacing);
    RUN(test_normalizes_two_char_operators);
    RUN(test_normalizes_pipe_operator);
    RUN(test_leaves_string_contents_untouched);
    RUN(test_leaves_interpolation_operators_untouched);
    RUN(test_leaves_comment_contents_untouched);
    RUN(test_indents_braced_block);
    RUN(test_indent_respects_tab_size);
    RUN(test_inserts_blank_line_before_declaration);
    RUN(test_formatting_is_idempotent);
    RUN(test_range_formatting_preserves_base_indent);
    RUN(test_range_formatting_no_base_indent);

    return SUMMARY();
}
