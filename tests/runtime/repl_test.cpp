// REPL unit tests — compute_brace_depth_delta, completion matching, REPL
// completion list, history management, and error recovery.

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/types/type_checker.hpp"
#include "runtime/compiler/compiler_config.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/repl/line_editor.hpp"
#include "runtime/repl/repl.hpp"
#include "runtime/repl/repl_detail.hpp"
#include "runtime/stdlib/common/stdlib_registry.hpp"
#include "runtime/vm/vm.hpp"
#include "shared_eval.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Helpers ───

// True if `haystack` contains `needle`.
[[nodiscard]] static bool contains(const std::vector<std::string>& haystack,
                                   const std::string& needle) {
    return std::ranges::find(haystack, needle) != haystack.end();
}

// ─── Tests ───

static void test_empty_line() {
    ASSERT_EQ(compute_brace_depth_delta(""), 0);
}

static void test_no_braces() {
    ASSERT_EQ(compute_brace_depth_delta("let x = 42"), 0);
}

static void test_open_brace() {
    ASSERT_EQ(compute_brace_depth_delta("if x > 0 {"), 1);
}

static void test_close_brace() {
    ASSERT_EQ(compute_brace_depth_delta("}"), -1);
}

static void test_balanced_braces() {
    ASSERT_EQ(compute_brace_depth_delta("{ }"), 0);
}

static void test_multiple_open() {
    ASSERT_EQ(compute_brace_depth_delta("{ {"), 2);
}

static void test_braces_in_string_ignored() {
    ASSERT_EQ(compute_brace_depth_delta("let s = \"{\""), 0);
}

static void test_braces_in_comment_ignored() {
    ASSERT_EQ(compute_brace_depth_delta("# this is a { comment"), 0);
}

static void test_string_interpolation() {
    // "value is ${expr}" — the ${ opens interpolation, } closes it.
    // Neither should affect brace depth.
    ASSERT_EQ(compute_brace_depth_delta("let s = \"value ${x}\""), 0);
}

static void test_interpolation_with_trailing_brace() {
    // "value ${x}" followed by an actual open brace.
    ASSERT_EQ(compute_brace_depth_delta("let s = \"${x}\" {"), 1);
}

static void test_escaped_quote_in_string() {
    // String with escaped quote: "hello \"world\""
    // The braces inside the string should be ignored.
    ASSERT_EQ(compute_brace_depth_delta("let s = \"hello \\\"world\\\"\""), 0);
}

static void test_brace_after_comment() {
    ASSERT_EQ(compute_brace_depth_delta("let x = 1 # { comment"), 0);
}

static void test_nested_interpolation() {
    // Nested braces inside interpolation: "${if x { y } else { z }}"
    ASSERT_EQ(compute_brace_depth_delta("\"${if x { y } else { z }}\""), 0);
}

static void test_whitespace_only() {
    ASSERT_EQ(compute_brace_depth_delta("   "), 0);
}

static void test_open_close_sequence() {
    ASSERT_EQ(compute_brace_depth_delta("{ } {"), 1);
}

// ─── Additional edge case tests ───

static void test_only_close_braces() {
    ASSERT_EQ(compute_brace_depth_delta("}}"), -2);
}

static void test_deeply_nested_open() {
    ASSERT_EQ(compute_brace_depth_delta("{{{"), 3);
}

static void test_brace_in_char_context() {
    // Braces immediately after code.
    ASSERT_EQ(compute_brace_depth_delta("f(){"), 1);
}

static void test_multiple_strings_with_braces() {
    // Multiple strings — braces in strings should all be ignored.
    ASSERT_EQ(compute_brace_depth_delta("\"a{b\" + \"c}d\""), 0);
}

static void test_interpolation_in_middle_of_string() {
    ASSERT_EQ(compute_brace_depth_delta("\"before ${x + 1} after\""), 0);
}

static void test_string_with_hash() {
    // Hash inside string should not start a comment.
    ASSERT_EQ(compute_brace_depth_delta("\"hello # world\" {"), 1);
}

static void test_comment_at_start_of_line() {
    ASSERT_EQ(compute_brace_depth_delta("# { } {"), 0);
}

static void test_mixed_code_and_comment() {
    ASSERT_EQ(compute_brace_depth_delta("{ # }"), 1);
}

static void test_empty_string_literal() {
    ASSERT_EQ(compute_brace_depth_delta("\"\" {"), 1);
}

static void test_tab_characters() {
    ASSERT_EQ(compute_brace_depth_delta("\t\t{"), 1);
}

static void test_brace_after_close_brace() {
    ASSERT_EQ(compute_brace_depth_delta("} {"), 0);
}

// ─── compute_brace_depth_delta — pathological / lexer-driven input ───
// These pin down the multiline-detection behaviour on malformed lines. The
// function is noexcept and must always return a sane delta so the REPL never
// gets stuck in (or wrongly enters) multiline mode.

static void test_unterminated_string_no_brace() {
    // An unterminated string has no brace tokens — delta must be 0.
    ASSERT_EQ(compute_brace_depth_delta("let s = \"abc"), 0);
}

static void test_brace_inside_unterminated_string() {
    // The '{' is part of the unterminated string literal, not a real block —
    // it must not push the REPL into multiline mode.
    ASSERT_EQ(compute_brace_depth_delta("let s = \"abc {"), 0);
}

static void test_multiple_interpolations() {
    ASSERT_EQ(compute_brace_depth_delta("\"${a} and ${b}\""), 0);
}

static void test_consecutive_interpolations() {
    ASSERT_EQ(compute_brace_depth_delta("\"${a}${b}\""), 0);
}

static void test_interpolation_then_real_brace() {
    ASSERT_EQ(compute_brace_depth_delta("\"${a}\" + f() {"), 1);
}

static void test_dict_literal_open() {
    ASSERT_EQ(compute_brace_depth_delta("mutable dictionary<integer> d = {"), 1);
}

static void test_dict_literal_balanced() {
    ASSERT_EQ(compute_brace_depth_delta("dictionary<integer> d = { \"a\": 1 }"), 0);
}

static void test_net_negative_three() {
    ASSERT_EQ(compute_brace_depth_delta("} } }"), -3);
}

static void test_unbalanced_extra_close() {
    ASSERT_EQ(compute_brace_depth_delta("if x { } }"), -1);
}

static void test_open_brace_then_comment_with_braces() {
    ASSERT_EQ(compute_brace_depth_delta("if x { # } }"), 1);
}

static void test_many_open_braces() {
    ASSERT_EQ(compute_brace_depth_delta(std::string(50, '{')), 50);
}

static void test_many_balanced_braces() {
    ASSERT_EQ(compute_brace_depth_delta(std::string(50, '{') + std::string(50, '}')), 0);
}

// ─── match_completions — positive cases ───

static void test_completion_prefix_match() {
    const std::vector<std::string> completions = {"Array",        "Array.map", "String",
                                                  "String.upper", "Math",      "Math.abs"};
    const auto matches = repl_detail::match_completions(completions, "Str");

    ASSERT_EQ(matches.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(matches[0], "String");
    ASSERT_EQ(matches[1], "String.upper");
}

static void test_completion_qualified_prefix() {
    const std::vector<std::string> completions = {"Array.filter", "Array.map", "Array", "String"};
    const auto matches = repl_detail::match_completions(completions, "Array.");

    // Sorted ascending: "Array.filter" precedes "Array.map".
    ASSERT_EQ(matches.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(matches[0], "Array.filter");
    ASSERT_EQ(matches[1], "Array.map");
}

static void test_completion_fuzzy_fallback() {
    // No completion starts with "upper", but "String.upper" contains it.
    const std::vector<std::string> completions = {"Array", "String", "String.upper"};
    const auto matches = repl_detail::match_completions(completions, "upper");

    ASSERT_EQ(matches.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(matches[0], "String.upper");
}

static void test_completion_fuzzy_is_case_insensitive() {
    // "MATH" matches nothing by prefix (case-sensitive) but fuzzy-matches "Math*".
    const std::vector<std::string> completions = {"Array", "Math", "Math.abs", "String"};
    const auto matches = repl_detail::match_completions(completions, "MATH");

    ASSERT_EQ(matches.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(matches[0], "Math");
    ASSERT_EQ(matches[1], "Math.abs");
}

static void test_completion_prefix_preferred_over_fuzzy() {
    // "abc" matches "abc" by prefix; "xabc" only matches as a substring. Because
    // a prefix match exists, the fuzzy pass must not run, so "xabc" is excluded.
    const std::vector<std::string> completions = {"abc", "xabc"};
    const auto matches = repl_detail::match_completions(completions, "abc");

    ASSERT_EQ(matches.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(matches[0], "abc");
}

static void test_completion_results_are_sorted() {
    const std::vector<std::string> completions = {"azure", "apple", "apricot", "zebra"};
    const auto matches = repl_detail::match_completions(completions, "a");

    ASSERT_EQ(matches.size(), static_cast<std::size_t>(3));
    ASSERT_TRUE(std::ranges::is_sorted(matches));
    ASSERT_EQ(matches[0], "apple");
}

// ─── match_completions — negative cases ───

static void test_completion_empty_prefix_returns_nothing() {
    const std::vector<std::string> completions = {"Array", "String"};

    ASSERT_TRUE(repl_detail::match_completions(completions, "").empty());
}

static void test_completion_no_match_returns_nothing() {
    const std::vector<std::string> completions = {"Array", "String", "Math"};

    ASSERT_TRUE(repl_detail::match_completions(completions, "zzz").empty());
}

static void test_completion_empty_candidate_list() {
    const std::vector<std::string> completions = {};

    ASSERT_TRUE(repl_detail::match_completions(completions, "Array").empty());
}

// ─── build_repl_completions ───

static void test_repl_completions_not_empty() {
    ASSERT_FALSE(repl_detail::build_repl_completions().empty());
}

static void test_repl_completions_include_keywords() {
    const auto completions = repl_detail::build_repl_completions();

    ASSERT_TRUE(contains(completions, "function"));
    ASSERT_TRUE(contains(completions, "match"));
    ASSERT_TRUE(contains(completions, "return"));
}

static void test_repl_completions_derive_from_keyword_table() {
    // Characterization: the keyword completions are derived from the canonical
    // k_keywords table, so every spelling in that table must be offered and the
    // list cannot drift from the language's keywords.
    const auto completions = repl_detail::build_repl_completions();

    for (const auto& keyword : k_keywords) {
        ASSERT_TRUE(contains(completions, std::string{keyword.spelling}));
    }

    // Keywords that the previous hand-maintained list omitted are now present.
    ASSERT_TRUE(contains(completions, "use"));
    ASSERT_TRUE(contains(completions, "in"));
    ASSERT_TRUE(contains(completions, "type"));
    ASSERT_TRUE(contains(completions, "queue"));
    ASSERT_TRUE(contains(completions, "channel"));

    // "import" is not a Luma keyword (absent from k_keywords), so it must not be
    // offered even though the old hand-maintained list included it.
    ASSERT_FALSE(contains(completions, "import"));
}

static void test_repl_completions_include_commands() {
    const auto completions = repl_detail::build_repl_completions();

    ASSERT_TRUE(contains(completions, ":quit"));
    ASSERT_TRUE(contains(completions, ":help"));
    ASSERT_TRUE(contains(completions, ":clear"));
    ASSERT_TRUE(contains(completions, ":file"));
}

static void test_repl_completions_include_module_names() {
    const auto completions = repl_detail::build_repl_completions();

    ASSERT_TRUE(contains(completions, "String"));
    ASSERT_TRUE(contains(completions, "Array"));
    ASSERT_TRUE(contains(completions, "Math"));
}

static void test_repl_completions_include_qualified_names() {
    const auto completions = repl_detail::build_repl_completions();

    // The catalog adds both the module name and the fully-qualified function.
    ASSERT_TRUE(contains(completions, "String.length"));
}

static void test_repl_completions_sorted_and_deduplicated() {
    const auto completions = repl_detail::build_repl_completions();

    ASSERT_TRUE(std::ranges::is_sorted(completions));
    // Sorted + unique ⇒ no two adjacent entries are equal.
    ASSERT_TRUE(std::ranges::adjacent_find(completions) == completions.end());
}

static void test_repl_completions_feed_matcher() {
    // End-to-end: the generated list drives completion matching as the REPL does.
    const auto completions = repl_detail::build_repl_completions();
    const auto matches = repl_detail::match_completions(completions, "String.");

    ASSERT_FALSE(matches.empty());
    ASSERT_TRUE(contains(matches, "String.length"));
}

// ─── LineEditor history management ───

static void test_history_adds_entry() {
    LineEditor editor;
    editor.add_history("integer x = 1");

    ASSERT_EQ(editor.history().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(editor.history().front(), "integer x = 1");
}

static void test_history_ignores_empty_lines() {
    LineEditor editor;
    editor.add_history("");

    ASSERT_TRUE(editor.history().empty());
}

static void test_history_dedups_consecutive_duplicates() {
    LineEditor editor;
    editor.add_history("a");
    editor.add_history("a");

    ASSERT_EQ(editor.history().size(), static_cast<std::size_t>(1));
}

static void test_history_keeps_nonconsecutive_duplicates() {
    LineEditor editor;
    editor.add_history("a");
    editor.add_history("b");
    editor.add_history("a");

    ASSERT_EQ(editor.history().size(), static_cast<std::size_t>(3));
    ASSERT_EQ(editor.history().back(), "a");
}

static void test_history_caps_size_and_drops_oldest() {
    LineEditor editor;

    // Add one more than the 1000-entry cap, all unique so none are deduplicated.
    for (int i = 0; i <= 1000; ++i) {
        editor.add_history("v" + std::to_string(i));
    }

    ASSERT_EQ(editor.history().size(), static_cast<std::size_t>(1000));
    // The oldest entry ("v0") was evicted; "v1" is now the front.
    ASSERT_EQ(editor.history().front(), "v1");
    ASSERT_EQ(editor.history().back(), "v1000");
}

// ─── Error recovery tests ───
// These drive the real REPL pipeline (repl_detail::lex_and_parse →
// repl_detail::compile_and_run — i.e. Lexer → Parser → TypeChecker → Compiler →
// VM) to verify that errors are properly catchable and that the environment
// remains intact for subsequent executions after an error.

// Helper: run a line through the real REPL evaluation pipeline.  The production
// functions report lex/parse/type/compile failures by returning std::nullopt
// (and printing diagnostics); this adapter converts that into an exception so
// the tests can assert on recovery.  Runtime errors propagate directly as
// RuntimeError from compile_and_run.  A persistent TypeChecker and VM mirror a
// real REPL session, so definitions from earlier lines stay visible.
static Value repl_eval(TypeChecker& checker, VM& vm, const std::string& source) {
    auto program = repl_detail::lex_and_parse(source);

    if (!program) {
        throw std::runtime_error{"lex/parse error"};
    }

    auto value = repl_detail::compile_and_run(*program, checker, vm);

    if (!value) {
        throw std::runtime_error{"type/compile error"};
    }

    return *value;
}

static void test_syntax_error_is_recoverable() {
    // A syntax error on one line should not prevent subsequent lines from executing.
    const auto env = luma::test::make_std_env();

    VM vm{env};
    TypeChecker checker;

    ASSERT_THROWS(repl_eval(checker, vm, "integer x = )"));

    // Environment is still functional after the error.
    const auto result = repl_eval(checker, vm, "1 + 2");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 3);
}

static void test_runtime_error_is_recoverable() {
    // A runtime error (assertion failure thrown during VM execution, which
    // propagates out of compile_and_run) must not break the session.  Each line
    // is self-contained because the REPL type checker resets between inputs, so
    // a value defined on one line is not visible to the next.
    const auto env = luma::test::make_std_env();

    VM vm{env};
    TypeChecker checker;

    ASSERT_THROWS(repl_eval(checker, vm, "assert(false)"));

    // The pipeline still evaluates correctly after the runtime error.
    const auto result = repl_eval(checker, vm, "6 + 9");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 15);
}

static void test_pipeline_recovers_across_multiple_errors() {
    // A multi-line REPL session with interleaved successes and errors: the
    // pipeline must keep evaluating correctly throughout.  Lines are
    // self-contained because the REPL type checker resets between inputs, so a
    // value defined on one line is not visible to the next.
    const auto env = luma::test::make_std_env();

    VM vm{env};
    TypeChecker checker;

    // Line 1: a successful definition.
    repl_eval(checker, vm, "integer a = 100");

    // Line 2: parse error (caught and discarded).
    try {
        repl_eval(checker, vm, "!!!");
    } catch (const std::exception&) {} // NOLINT(bugprone-empty-catch)

    // Line 3: another successful definition.
    repl_eval(checker, vm, "integer b = 200");

    // Line 4: runtime error (caught and discarded).
    try {
        repl_eval(checker, vm, "assert(false)");
    } catch (const std::exception&) {} // NOLINT(bugprone-empty-catch)

    // Line 5: the pipeline still evaluates a fresh expression correctly.
    const auto result = repl_eval(checker, vm, "100 + 200");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 300);
}

// ─── run_eval — stdin program evaluation (backs the editor Playground) ───
// run_eval() reads a whole program from std::cin and evaluates it through the
// REPL pipeline (require_main = false), so bare top-level statements run. These
// tests redirect std::cin to a string buffer and capture std::cout / std::cerr.

// Helper: feed `source` to run_eval() via a redirected stdin, capturing stdout.
static int run_eval_capture(const std::string& source, std::string& out) {
    std::istringstream input{source};
    StdinRedirect stdin_guard{input.rdbuf()};
    CapturedStream captured_out{std::cout};

    const int code = run_eval(/*sandbox=*/false);
    out = captured_out.str();

    return code;
}

static void test_run_eval_executes_top_level_without_main() {
    std::string out;
    const int code = run_eval_capture("print(\"hi\")\n", out);

    ASSERT_EQ(code, exit_code::success);
    ASSERT_TRUE(out.find("hi") != std::string::npos);
}

static void test_run_eval_runs_multiple_statements() {
    std::string out;
    const int code = run_eval_capture("mutable integer total = 0\n"
                                      "for x in [1, 2, 3] {\n"
                                      "    total = total + x\n"
                                      "}\n"
                                      "print(\"sum=${total}\")\n",
                                      out);

    ASSERT_EQ(code, exit_code::success);
    ASSERT_TRUE(out.find("sum=6") != std::string::npos);
}

static void test_run_eval_reports_syntax_error() {
    // The parser writes a diagnostic to stderr; capture it so it does not
    // pollute the test log, and assert on the syntax exit code.
    CapturedStream captured_err{std::cerr};

    std::string out;
    const int code = run_eval_capture("integer x = )", out);

    ASSERT_EQ(code, exit_code::syntax_error);
}

static void test_run_eval_reports_runtime_error() {
    CapturedStream captured_err{std::cerr};

    std::string out;
    const int code = run_eval_capture("assert(false)", out);

    ASSERT_EQ(code, exit_code::runtime_error);
}

// The built-in Solaris surface is injected for --eval snippets that
// reference it (mirroring file execution), so a program using the surface
// type-checks and runs without any include.
static void test_run_eval_injects_gui_surface() {
    std::string out;
    const int code = run_eval_capture("function View greeting() {\n"
                                      "    return Solaris.heading(\"Hi\") |> Solaris.center()\n"
                                      "}\n"
                                      "print(\"kind=${greeting().kind}\")\n",
                                      out);

    ASSERT_EQ(code, exit_code::success);
    ASSERT_TRUE(out.find("kind=heading") != std::string::npos);
}

// The injection trigger is a whole-word match, so a program that merely embeds
// the trigger inside a longer identifier (and defines its own `View` record)
// must NOT get the prelude injected — otherwise the prelude's global `View`
// would collide with the user's. Regression test for the substring-trigger bug.
static void test_run_eval_embedded_trigger_does_not_inject() {
    std::string out;
    const int code = run_eval_capture("record View { integer n = 0 }\n"
                                      "function integer mySolarisHelper() { return 7 }\n"
                                      "print(\"n=${mySolarisHelper()}\")\n",
                                      out);

    ASSERT_EQ(code, exit_code::success);
    ASSERT_TRUE(out.find("n=7") != std::string::npos);
}

// ─── validate_file_path — pathological / OS-unresolvable input ───
// The :file command must reject a malformed path cleanly (return false) rather
// than letting a std::filesystem_error escape and tear down the whole REPL
// session. Regression test for the crash where a Windows device path such as
// "\\.\nul\..." reached the throwing std::filesystem::exists overload and
// aborted the interpreter with "fatal error: exists: The parameter is
// incorrect." The non-throwing error_code overloads must report it as simply
// not found.

static void test_validate_file_path_rejects_unresolvable_path() {
    // This string ends in ".luma" and has no traversal or symlink component, so
    // it passes the security checks and reaches the existence probe. On Windows
    // the device path makes the throwing exists() overload raise; on POSIX it is
    // just a nonexistent filename. Either way validate_file_path must return
    // false WITHOUT throwing (a thrown exception would abort this test process).
    ASSERT_FALSE(repl_detail::validate_file_path("\\\\.\\nul\\missing.luma"));
}

static void test_validate_file_path_rejects_empty() {
    ASSERT_FALSE(repl_detail::validate_file_path(""));
}

static void test_validate_file_path_rejects_non_luma_extension() {
    ASSERT_FALSE(repl_detail::validate_file_path("notes.txt"));
}

int main() {
    RUN(test_empty_line);
    RUN(test_no_braces);
    RUN(test_open_brace);
    RUN(test_close_brace);
    RUN(test_balanced_braces);
    RUN(test_multiple_open);
    RUN(test_braces_in_string_ignored);
    RUN(test_braces_in_comment_ignored);
    RUN(test_string_interpolation);
    RUN(test_interpolation_with_trailing_brace);
    RUN(test_escaped_quote_in_string);
    RUN(test_brace_after_comment);
    RUN(test_nested_interpolation);
    RUN(test_whitespace_only);
    RUN(test_open_close_sequence);
    RUN(test_only_close_braces);
    RUN(test_deeply_nested_open);
    RUN(test_brace_in_char_context);
    RUN(test_multiple_strings_with_braces);
    RUN(test_interpolation_in_middle_of_string);
    RUN(test_string_with_hash);
    RUN(test_comment_at_start_of_line);
    RUN(test_mixed_code_and_comment);
    RUN(test_empty_string_literal);
    RUN(test_tab_characters);
    RUN(test_brace_after_close_brace);

    // compute_brace_depth_delta — pathological / lexer-driven input.
    RUN(test_unterminated_string_no_brace);
    RUN(test_brace_inside_unterminated_string);
    RUN(test_multiple_interpolations);
    RUN(test_consecutive_interpolations);
    RUN(test_interpolation_then_real_brace);
    RUN(test_dict_literal_open);
    RUN(test_dict_literal_balanced);
    RUN(test_net_negative_three);
    RUN(test_unbalanced_extra_close);
    RUN(test_open_brace_then_comment_with_braces);
    RUN(test_many_open_braces);
    RUN(test_many_balanced_braces);

    // Completion matching — positive cases.
    RUN(test_completion_prefix_match);
    RUN(test_completion_qualified_prefix);
    RUN(test_completion_fuzzy_fallback);
    RUN(test_completion_fuzzy_is_case_insensitive);
    RUN(test_completion_prefix_preferred_over_fuzzy);
    RUN(test_completion_results_are_sorted);

    // Completion matching — negative cases.
    RUN(test_completion_empty_prefix_returns_nothing);
    RUN(test_completion_no_match_returns_nothing);
    RUN(test_completion_empty_candidate_list);

    // REPL completion list.
    RUN(test_repl_completions_not_empty);
    RUN(test_repl_completions_include_keywords);
    RUN(test_repl_completions_derive_from_keyword_table);
    RUN(test_repl_completions_include_commands);
    RUN(test_repl_completions_include_module_names);
    RUN(test_repl_completions_include_qualified_names);
    RUN(test_repl_completions_sorted_and_deduplicated);
    RUN(test_repl_completions_feed_matcher);

    // LineEditor history management.
    RUN(test_history_adds_entry);
    RUN(test_history_ignores_empty_lines);
    RUN(test_history_dedups_consecutive_duplicates);
    RUN(test_history_keeps_nonconsecutive_duplicates);
    RUN(test_history_caps_size_and_drops_oldest);

    // Error recovery tests.
    RUN(test_syntax_error_is_recoverable);
    RUN(test_runtime_error_is_recoverable);
    RUN(test_pipeline_recovers_across_multiple_errors);

    RUN(test_run_eval_executes_top_level_without_main);
    RUN(test_run_eval_runs_multiple_statements);
    RUN(test_run_eval_reports_syntax_error);
    RUN(test_run_eval_reports_runtime_error);
    RUN(test_run_eval_injects_gui_surface);
    RUN(test_run_eval_embedded_trigger_does_not_inject);

    // :file path validation.
    RUN(test_validate_file_path_rejects_unresolvable_path);
    RUN(test_validate_file_path_rejects_empty);
    RUN(test_validate_file_path_rejects_non_luma_extension);
    return SUMMARY();
}
