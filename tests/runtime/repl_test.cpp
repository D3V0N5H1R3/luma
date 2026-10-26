// REPL unit tests — compute_brace_depth_delta, completion matching, REPL
// completion list, history management, and error recovery.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/diagnostics/renderer.hpp"
#include "analysis/errors/error.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/source/source_manager.hpp"
#include "analysis/types/type_checker.hpp"
#include "common/path_utils.hpp"
#include "runtime/compiler/compiler_config.hpp"
#include "runtime/compiler/compiler_limits.hpp"
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

    // Container/handle type names (queue, channel, …) were demoted from reserved
    // keywords to ordinary identifiers (R02), so they are no longer offered as
    // keyword completions.
    ASSERT_FALSE(contains(completions, "queue"));
    ASSERT_FALSE(contains(completions, "channel"));

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
// RuntimeError from compile_and_run.  A persistent TypeChecker, VM and program
// list mirror a real REPL session, so definitions from earlier successful lines
// stay visible.
static Value repl_eval(TypeChecker& checker, VM& vm, std::vector<Program>& programs,
                       const std::string& source) {
    auto program = repl_detail::lex_and_parse(source);

    if (!program) {
        throw std::runtime_error{"lex/parse error"};
    }

    // These inputs include nothing, so a per-call include registry suffices.
    repl_detail::ReplIncludeRegistry includes;
    auto value = repl_detail::run_and_keep(std::move(*program), checker, vm, programs, includes);

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
    std::vector<Program> programs;

    ASSERT_THROWS(repl_eval(checker, vm, programs, "integer x = )"));

    // Environment is still functional after the error.
    const auto result = repl_eval(checker, vm, programs, "1 + 2");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 3);
}

static void test_runtime_error_is_recoverable() {
    // A runtime error (assertion failure thrown during VM execution, which
    // propagates out of compile_and_run) must not break the session.
    const auto env = luma::test::make_std_env();

    VM vm{env};
    TypeChecker checker;
    std::vector<Program> programs;

    ASSERT_THROWS(repl_eval(checker, vm, programs, "assert(false)"));

    // The pipeline still evaluates correctly after the runtime error.
    const auto result = repl_eval(checker, vm, programs, "6 + 9");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 15);
}

static void test_pipeline_recovers_across_multiple_errors() {
    // A multi-line REPL session with interleaved successes and errors: the
    // pipeline must keep evaluating correctly throughout, and definitions from
    // the successful lines stay visible.
    const auto env = luma::test::make_std_env();

    VM vm{env};
    TypeChecker checker;
    std::vector<Program> programs;

    // Line 1: a successful definition.
    repl_eval(checker, vm, programs, "integer a = 100");

    // Line 2: parse error (caught and discarded).
    try {
        repl_eval(checker, vm, programs, "!!!");
    } catch (const std::exception&) {} // NOLINT(bugprone-empty-catch)

    // Line 3: another successful definition.
    repl_eval(checker, vm, programs, "integer b = 200");

    // Line 4: runtime error (caught and discarded).
    try {
        repl_eval(checker, vm, programs, "assert(false)");
    } catch (const std::exception&) {} // NOLINT(bugprone-empty-catch)

    // Line 5: both earlier definitions are still visible.
    const auto result = repl_eval(checker, vm, programs, "a + b");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 300);
}

static void test_variable_persists_across_lines() {
    const auto env = luma::test::make_std_env();

    VM vm{env};
    TypeChecker checker;
    std::vector<Program> programs;

    repl_eval(checker, vm, programs, "integer x = 5");

    const auto result = repl_eval(checker, vm, programs, "x + 1");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 6);
}

static void test_function_and_record_persist_across_lines() {
    const auto env = luma::test::make_std_env();

    VM vm{env};
    TypeChecker checker;
    std::vector<Program> programs;

    repl_eval(checker, vm, programs, "function integer square(integer n) { return n * n }");
    // `y` is filled from the declaration's default on a later line.
    repl_eval(checker, vm, programs, "record Point { integer x, integer y = 4 }");
    repl_eval(checker, vm, programs, "Point p = Point { x = 3 }");

    const auto result = repl_eval(checker, vm, programs, "square(p.x) + p.y");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 13);
}

static void test_redefinition_shadows_earlier_line() {
    // Redefining a name on a later line is allowed and may change its type.
    const auto env = luma::test::make_std_env();

    VM vm{env};
    TypeChecker checker;
    std::vector<Program> programs;

    repl_eval(checker, vm, programs, "integer y = 1");
    repl_eval(checker, vm, programs, "string y = \"one\"");

    const auto result = repl_eval(checker, vm, programs, "String.length(y)");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 3);
}

static void test_rejected_line_defines_nothing() {
    // A line that fails type checking never ran, so its names stay undefined.
    const auto env = luma::test::make_std_env();

    VM vm{env};
    TypeChecker checker;
    std::vector<Program> programs;

    ASSERT_THROWS(repl_eval(checker, vm, programs, "integer z = \"oops\""));
    ASSERT_THROWS(repl_eval(checker, vm, programs, "z + 1"));
}

static void test_runtime_error_keeps_definitions_that_ran() {
    // The declaration ran before the assertion failed, so `a` exists in the VM
    // and must stay visible to the type checker as well.
    const auto env = luma::test::make_std_env();

    VM vm{env};
    TypeChecker checker;
    std::vector<Program> programs;

    ASSERT_THROWS(repl_eval(checker, vm, programs, "integer a = 1; assert(false)"));

    const auto result = repl_eval(checker, vm, programs, "a + 1");

    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 2);
}

static void test_rejected_line_does_not_consume_unique() {
    // A rejected line never ran, so a unique value it would have consumed is
    // still available afterwards.
    const auto env = luma::test::make_std_env();

    VM vm{env};
    TypeChecker checker;
    std::vector<Program> programs;

    repl_eval(checker, vm, programs, "function void consume(unique string _s) { }");
    repl_eval(checker, vm, programs, "unique string x = \"hello\"");
    ASSERT_THROWS(repl_eval(checker, vm, programs, "consume(x); integer bad = \"oops\""));

    repl_eval(checker, vm, programs, "consume(x)");
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

// ─── parse_repl_command (regression: ":file" with no argument and unknown
// commands such as ":foo" fell through to the Luma parser) ───

static void test_parse_repl_command_exact_matches() {
    const auto quit = repl_detail::parse_repl_command(":q");
    ASSERT_TRUE(quit.kind.has_value());
    ASSERT_TRUE(*quit.kind == repl_detail::ReplCommandKind::Quit);

    const auto help = repl_detail::parse_repl_command("  :help  ");
    ASSERT_TRUE(help.kind.has_value());
    ASSERT_TRUE(*help.kind == repl_detail::ReplCommandKind::Help);

    // Extra whitespace around the argument is trimmed.
    const auto file = repl_detail::parse_repl_command(":file   app.luma  ");
    ASSERT_TRUE(file.kind.has_value());
    ASSERT_TRUE(*file.kind == repl_detail::ReplCommandKind::File);
    ASSERT_EQ(file.argument, "app.luma");

    const auto alias = repl_detail::parse_repl_command(":f\tapp.luma");
    ASSERT_TRUE(alias.kind.has_value());
    ASSERT_EQ(alias.argument, "app.luma");
}

static void test_parse_repl_command_missing_argument_is_usage() {
    for (const auto* line : {":file", ":f", ":file   "}) {
        const auto parsed = repl_detail::parse_repl_command(line);
        ASSERT_FALSE(parsed.kind.has_value());
        ASSERT_TRUE(parsed.error.find("Usage: :file <path>") != std::string::npos);
    }
}

static void test_parse_repl_command_unknown_and_extra_argument() {
    const auto unknown = repl_detail::parse_repl_command(":foo");
    ASSERT_FALSE(unknown.kind.has_value());
    ASSERT_TRUE(unknown.error.find("Unknown command: :foo") != std::string::npos);

    // A command word must match exactly — ":filex" is not ":file".
    const auto prefix = repl_detail::parse_repl_command(":filex app.luma");
    ASSERT_FALSE(prefix.kind.has_value());
    ASSERT_TRUE(prefix.error.find("Unknown command") != std::string::npos);

    const auto extra = repl_detail::parse_repl_command(":quit now");
    ASSERT_FALSE(extra.kind.has_value());
    ASSERT_TRUE(extra.error.find("Usage: :quit") != std::string::npos);
}

// ─── include in REPL input and --eval (regression: silently ignored) ───

namespace {

// Switches the working directory to `dir` for the guard's lifetime.
class ScopedCwd {
public:
    explicit ScopedCwd(const std::filesystem::path& dir)
        : previous_{std::filesystem::current_path()} {
        std::filesystem::current_path(dir);
    }

    ~ScopedCwd() noexcept {
        std::error_code ec;
        std::filesystem::current_path(previous_, ec);
    }

    ScopedCwd(const ScopedCwd&) = delete;
    ScopedCwd& operator=(const ScopedCwd&) = delete;
    ScopedCwd(ScopedCwd&&) = delete;
    ScopedCwd& operator=(ScopedCwd&&) = delete;

private:
    std::filesystem::path previous_;
};

} // namespace

static void test_repl_input_include_resolves_against_cwd() {
    const TempDir dir;
    const TempFile helper{dir.path() / "repl_inc_helper.luma",
                          "function integer repl_inc_value() { return 42 }\n"};
    const ScopedCwd cwd{dir.path()};

    const auto env = luma::test::make_std_env();
    VM vm{env};
    TypeChecker checker;
    std::vector<Program> programs;
    SourceManager source_manager;
    repl_detail::ReplIncludeRegistry includes;

    auto program = repl_detail::lex_and_parse("include \"repl_inc_helper.luma\"\n");
    ASSERT_TRUE(program.has_value());
    ASSERT_TRUE(repl_detail::resolve_input_includes(*program, source_manager, includes));
    ASSERT_TRUE(repl_detail::run_and_keep(std::move(*program), checker, vm, programs, includes)
                    .has_value());

    // The included function is visible to later inputs.
    const auto result = repl_eval(checker, vm, programs, "repl_inc_value() + 1");
    ASSERT_TRUE(result.is_integer());
    ASSERT_EQ(result.as_integer(), 43);
}

static void test_repl_input_include_missing_file_fails() {
    const TempDir dir;
    const ScopedCwd cwd{dir.path()};
    const CapturedStream captured_err{std::cerr};

    SourceManager source_manager;
    repl_detail::ReplIncludeRegistry includes;
    auto program = repl_detail::lex_and_parse("include \"repl_inc_missing.luma\"\n");
    ASSERT_TRUE(program.has_value());
    ASSERT_FALSE(repl_detail::resolve_input_includes(*program, source_manager, includes));
    ASSERT_TRUE(captured_err.str().find("not found") != std::string::npos);
}

static void test_run_eval_resolves_include() {
    const TempDir dir;
    const TempFile helper{dir.path() / "eval_inc_helper.luma",
                          "function string eval_inc_greeting() { return \"hello-include\" }\n"};
    const ScopedCwd cwd{dir.path()};

    std::string out;
    const int code = run_eval_capture("include \"eval_inc_helper.luma\"\n"
                                      "print(eval_inc_greeting())\n",
                                      out);

    ASSERT_EQ(code, exit_code::success);
    ASSERT_TRUE(out.find("hello-include") != std::string::npos);
}

// ─── run_eval exit codes per failure category (regression: every rejected
// program exited 2, including compile errors that should exit 4) ───

static void test_run_eval_type_error_exits_2() {
    const CapturedStream captured_err{std::cerr};

    std::string out;
    ASSERT_EQ(run_eval_capture("integer x = \"oops\"\n", out), exit_code::type_error);
}

static void test_run_eval_compile_error_exits_4() {
    // An array literal longer than the bytecode operand allows type-checks but
    // is rejected by the compiler.
    std::string source = "array<integer> big = [";
    for (std::size_t i = 0; i <= CompilerLimits::k_max_array_elements; ++i) {
        source += i == 0 ? "0" : ",0";
    }
    source += "]\n";

    const CapturedStream captured_err{std::cerr};

    std::string out;
    ASSERT_EQ(run_eval_capture(source, out), exit_code::compile_error);
}

static void test_run_eval_missing_include_exits_4() {
    const TempDir dir;
    const ScopedCwd cwd{dir.path()};
    const CapturedStream captured_err{std::cerr};

    std::string out;
    ASSERT_EQ(run_eval_capture("include \"eval_inc_missing.luma\"\nprint(1)\n", out),
              exit_code::compile_error);
}

// ─── :file reloads from disk (regression: a second :file ran stale text) ───

static void test_load_repl_file_source_rereads_edited_file() {
    const TempDir dir;
    const auto path = dir.path() / "reload_me.luma";
    const TempFile file{path, "function integer reload_value() { return 1 }\n"};

    SourceManager source_manager;

    const auto& first = repl_detail::load_repl_file_source(source_manager, path.string());
    ASSERT_TRUE(first.text.find("return 1") != std::string::npos);

    {
        std::ofstream out{path, std::ios::binary | std::ios::trunc};
        out << "function integer reload_value() { return 2 }\n";
    }

    const auto& second = repl_detail::load_repl_file_source(source_manager, path.string());
    ASSERT_TRUE(second.text.find("return 2") != std::string::npos);
    ASSERT_TRUE(second.file_id != first.file_id);
    // The reloaded file keeps its directory, so its relative includes still
    // resolve next to it.
    ASSERT_TRUE(path_from_utf8(second.path).parent_path() ==
                path_from_utf8(first.path).parent_path());

    // Earlier text stays alive for diagnostics that point into it.
    ASSERT_TRUE(source_manager.get_file(first.file_id)->text.find("return 1") != std::string::npos);
}

// ─── Include rollback (regression: a rejected input's included files stayed
// registered, so the include-once rule skipped them for every later input and
// their definitions never reached the session) ───

namespace {

// A REPL session's persistent state, as run_repl holds it.
struct ReplSession {
    EnvPtr env = luma::test::make_std_env();
    VM vm{env};
    TypeChecker checker;
    std::vector<Program> programs;
    SourceManager source_manager;
    repl_detail::ReplIncludeRegistry includes;

    // Mirrors `:file <path>`: true when the file was loaded and kept.
    [[nodiscard]] bool load_file(const std::filesystem::path& path) {
        auto program = repl_detail::load_file_input(path_to_utf8(path), source_manager, includes);

        return program &&
               repl_detail::run_and_keep(std::move(*program), checker, vm, programs, includes)
                   .has_value();
    }

    // Mirrors a typed input: true when it was evaluated and kept.
    [[nodiscard]] bool input(const std::string& source) {
        auto program = repl_detail::lex_and_parse(source);

        return program && repl_detail::resolve_input_includes(*program, source_manager, includes) &&
               repl_detail::run_and_keep(std::move(*program), checker, vm, programs, includes)
                   .has_value();
    }

    [[nodiscard]] Value eval(const std::string& source) {
        return repl_eval(checker, vm, programs, source);
    }
};

void overwrite(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out << text;
}

} // namespace

static void test_repl_file_include_rejected_then_fixed_is_reread() {
    // The reported repro: `:file a.luma` fails to type-check in b.luma, b.luma
    // is fixed, and the second `:file a.luma` reported "undefined variable
    // 'helper'" because b.luma was skipped as already included.
    const TempDir dir;
    const TempFile a{dir.path() / "a.luma", "include \"b.luma\"\nprint(helper())\n"};
    const TempFile b{dir.path() / "b.luma", "function integer helper() { return \"oops\" }\n"};

    const CapturedStream captured_out{std::cout};
    const CapturedStream captured_err{std::cerr};
    ReplSession session;

    ASSERT_FALSE(session.load_file(a.path()));

    overwrite(b.path(), "function integer helper() { return 42 }\n");

    ASSERT_TRUE(session.load_file(a.path()));
    ASSERT_TRUE(captured_out.str().find("42") != std::string::npos);
    ASSERT_EQ(session.eval("helper()").as_integer(), 42);
}

static void test_repl_typed_include_rejected_then_fixed_is_reread() {
    const TempDir dir;
    const TempFile b{dir.path() / "rb_helper.luma",
                     "function integer rb_helper() { return \"oops\" }\n"};
    const ScopedCwd cwd{dir.path()};

    const CapturedStream captured_err{std::cerr};
    ReplSession session;

    ASSERT_FALSE(session.input("include \"rb_helper.luma\"\n"));

    overwrite(b.path(), "function integer rb_helper() { return 7 }\n");

    ASSERT_TRUE(session.input("include \"rb_helper.luma\"\n"));
    ASSERT_EQ(session.eval("rb_helper()").as_integer(), 7);
}

static void test_repl_file_reload_rereads_included_files() {
    // A `:file` reload re-reads the files it includes, like the file itself, so
    // an edit to an included file takes effect.
    const TempDir dir;
    const TempFile a{dir.path() / "a.luma", "include \"b.luma\"\n"};
    const TempFile b{dir.path() / "b.luma", "function integer helper() { return 1 }\n"};

    const CapturedStream captured_out{std::cout};
    ReplSession session;

    ASSERT_TRUE(session.load_file(a.path()));
    ASSERT_EQ(session.eval("helper()").as_integer(), 1);

    overwrite(b.path(), "function integer helper() { return 2 }\n");

    ASSERT_TRUE(session.load_file(a.path()));
    ASSERT_EQ(session.eval("helper()").as_integer(), 2);
}

static void test_repl_include_once_within_input_and_across_kept_inputs() {
    const TempDir dir;
    const TempFile helper{dir.path() / "once_helper.luma",
                          "function integer once_value() { return 3 }\n"};
    const ScopedCwd cwd{dir.path()};

    ReplSession session;

    // Within one input the second include of the same file is skipped.
    auto twice = repl_detail::lex_and_parse("include \"once_helper.luma\"\n"
                                            "include \"once_helper.luma\"\n");
    ASSERT_TRUE(twice.has_value());
    ASSERT_TRUE(
        repl_detail::resolve_input_includes(*twice, session.source_manager, session.includes));
    ASSERT_EQ(twice->declarations.size(), 1U);
    ASSERT_TRUE(repl_detail::run_and_keep(std::move(*twice), session.checker, session.vm,
                                          session.programs, session.includes)
                    .has_value());

    // Once that input is kept, a later include of the file is skipped too.
    auto again = repl_detail::lex_and_parse("include \"once_helper.luma\"\n");
    ASSERT_TRUE(again.has_value());
    ASSERT_TRUE(
        repl_detail::resolve_input_includes(*again, session.source_manager, session.includes));
    ASSERT_TRUE(again->declarations.empty());
}

static void test_repl_file_load_counts_file_and_includes_as_included() {
    // A kept `:file` marks the file and its includes as included, so typing
    // `include` for either afterwards adds nothing.
    const TempDir dir;
    const TempFile a{dir.path() / "a.luma", "include \"b.luma\"\n"};
    const TempFile b{dir.path() / "b.luma", "function integer helper() { return 1 }\n"};
    const ScopedCwd cwd{dir.path()};

    ReplSession session;

    ASSERT_TRUE(session.load_file(a.path()));

    auto program = repl_detail::lex_and_parse("include \"a.luma\"\ninclude \"b.luma\"\n");
    ASSERT_TRUE(program.has_value());
    ASSERT_TRUE(
        repl_detail::resolve_input_includes(*program, session.source_manager, session.includes));
    ASSERT_TRUE(program->declarations.empty());
}

// ─── Runtime errors in typed input render with source (regression: they
// rendered as "RuntimeError in <unknown>:1:N" plus "[unable to retrieve source
// for file_id 0]", because typed input was not registered as a source) ───

static void test_repl_runtime_error_renders_input_source() {
    const CapturedStream captured_out{std::cout};
    const CapturedStream captured_err{std::cerr};
    ReplSession session;
    const DiagnosticRenderer renderer{session.source_manager};

    repl_detail::evaluate_input("array<integer> repl_xs = [1, 2]\n", session.source_manager,
                                renderer, session.checker, session.vm, session.programs,
                                session.includes);
    repl_detail::evaluate_input("integer repl_bad = repl_xs[7]\n", session.source_manager, renderer,
                                session.checker, session.vm, session.programs, session.includes);

    const auto errors = captured_err.str();
    ASSERT_TRUE(errors.find("<repl:2>:1:") != std::string::npos);
    ASSERT_TRUE(errors.find("integer repl_bad = repl_xs[7]") != std::string::npos);
    ASSERT_TRUE(errors.find("<unknown>") == std::string::npos);
    ASSERT_TRUE(errors.find("unable to retrieve source") == std::string::npos);

    // The input still counts as kept: the session keeps working.
    ASSERT_EQ(session.eval("repl_xs[1]").as_integer(), 2);
}

// ─── Line-editor UTF-8 cursor helpers (regression: Backspace/arrows moved by
// bytes, splitting multi-byte characters) ───

static void test_line_editor_char_boundaries_are_utf8_aware() {
    // "aé€😀" — 1 + 2 + 3 + 4 bytes.
    const std::string buf = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";

    ASSERT_EQ(repl_detail::next_char_boundary(buf, 0), 1U);
    ASSERT_EQ(repl_detail::next_char_boundary(buf, 1), 3U);
    ASSERT_EQ(repl_detail::next_char_boundary(buf, 3), 6U);
    ASSERT_EQ(repl_detail::next_char_boundary(buf, 6), 10U);
    ASSERT_EQ(repl_detail::next_char_boundary(buf, 10), 10U);

    ASSERT_EQ(repl_detail::prev_char_boundary(buf, 10), 6U);
    ASSERT_EQ(repl_detail::prev_char_boundary(buf, 6), 3U);
    ASSERT_EQ(repl_detail::prev_char_boundary(buf, 3), 1U);
    ASSERT_EQ(repl_detail::prev_char_boundary(buf, 1), 0U);
    ASSERT_EQ(repl_detail::prev_char_boundary(buf, 0), 0U);

    ASSERT_EQ(repl_detail::display_width(buf), 4U);
    ASSERT_EQ(repl_detail::display_width(std::string_view{buf}.substr(3)), 2U);
}

static void test_line_editor_char_boundaries_with_invalid_bytes() {
    // A stray continuation byte and a truncated lead are one column each.
    const std::string buf = "x\x80y\xE2";

    ASSERT_EQ(repl_detail::prev_char_boundary(buf, 2), 1U);
    ASSERT_EQ(repl_detail::prev_char_boundary(buf, 4), 3U);
    ASSERT_EQ(repl_detail::next_char_boundary(buf, 3), 4U);
    ASSERT_EQ(repl_detail::display_width(buf), 4U);
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
    RUN(test_variable_persists_across_lines);
    RUN(test_function_and_record_persist_across_lines);
    RUN(test_redefinition_shadows_earlier_line);
    RUN(test_rejected_line_defines_nothing);
    RUN(test_runtime_error_keeps_definitions_that_ran);
    RUN(test_rejected_line_does_not_consume_unique);

    RUN(test_run_eval_executes_top_level_without_main);
    RUN(test_run_eval_runs_multiple_statements);
    RUN(test_run_eval_reports_syntax_error);
    RUN(test_run_eval_reports_runtime_error);

    // :file path validation.
    RUN(test_validate_file_path_rejects_unresolvable_path);
    RUN(test_validate_file_path_rejects_empty);
    RUN(test_validate_file_path_rejects_non_luma_extension);

    // REPL command parsing.
    RUN(test_parse_repl_command_exact_matches);
    RUN(test_parse_repl_command_missing_argument_is_usage);
    RUN(test_parse_repl_command_unknown_and_extra_argument);

    // include in REPL input / --eval.
    RUN(test_repl_input_include_resolves_against_cwd);
    RUN(test_repl_input_include_missing_file_fails);
    RUN(test_run_eval_resolves_include);

    // run_eval exit codes.
    RUN(test_run_eval_type_error_exits_2);
    RUN(test_run_eval_compile_error_exits_4);
    RUN(test_run_eval_missing_include_exits_4);

    // :file reload.
    RUN(test_load_repl_file_source_rereads_edited_file);
    RUN(test_repl_file_include_rejected_then_fixed_is_reread);
    RUN(test_repl_typed_include_rejected_then_fixed_is_reread);
    RUN(test_repl_file_reload_rereads_included_files);
    RUN(test_repl_include_once_within_input_and_across_kept_inputs);
    RUN(test_repl_file_load_counts_file_and_includes_as_included);
    RUN(test_repl_runtime_error_renders_input_source);

    // Line-editor UTF-8 cursor helpers.
    RUN(test_line_editor_char_boundaries_are_utf8_aware);
    RUN(test_line_editor_char_boundaries_with_invalid_bytes);
    return SUMMARY();
}
