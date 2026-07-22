// Standard library tests: Process.

#include <string>
#include <vector>

#include "runtime/stdlib/system/process_module.hpp"
#include "stdlib_test_helpers.hpp"

static void test_process_get_args_empty() {
    set_program_args({});

    const auto v = eval("Process.get_arguments()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), std::size_t{0});
}

static void test_process_get_args_with_values() {
    set_program_args({"hello", "world", "42"});

    const auto v = eval("Process.get_arguments()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), std::size_t{3});
    ASSERT_EQ((*v.as_array()->elements)[0].as_string(), "hello");
    ASSERT_EQ((*v.as_array()->elements)[1].as_string(), "world");
    ASSERT_EQ((*v.as_array()->elements)[2].as_string(), "42");

    // Reset to avoid affecting other tests.
    set_program_args({});
}

static void test_process_run_returns_record() {
    // Process.run should return result<ProcessResult> with exit_code and output fields.
#ifdef _WIN32
    // `echo` is a cmd.exe builtin on Windows, so it must be invoked via `cmd /c`.
    const auto v = eval("Process.run(\"cmd /c echo hello\")");
#else
    // On POSIX `echo` is a standalone binary resolved through PATH by execvp.
    const auto v = eval("Process.run(\"echo hello\")");
#endif

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;

    ASSERT_TRUE(inner.is_record());
    ASSERT_TRUE(inner.as_record()->type_name == "ProcessResult");
    ASSERT_TRUE(inner.as_record()->find_field("exit_code") != nullptr);
    ASSERT_TRUE(inner.as_record()->find_field("output") != nullptr);
    ASSERT_TRUE(inner.as_record()->find_field("exit_code")->is_integer());
    ASSERT_TRUE(inner.as_record()->find_field("exit_code")->as_integer() == 0);
    ASSERT_TRUE(inner.as_record()->find_field("output")->is_string());
    ASSERT_TRUE(inner.as_record()->find_field("output")->as_string().find("hello") !=
                std::string::npos);
}

static void test_process_run_unclosed_quote() {
    // An unclosed-quote command must return fail, not throw.
    ASSERT_EVAL_FAILURE("Process.run(\"echo \\\"unclosed\")");
}

// ─── tokenize_command — the untrusted command-string parser behind ──────────
// Process.run.  These exercise the parser directly (it is declared in
// process_module.hpp and feeds CreateProcessA / execvp without a shell), so the
// quoting and escaping grammar is pinned down independently of any OS spawn.

static void test_tokenize_command_simple() {
    const auto argv = tokenize_command("echo hello world");

    ASSERT_EQ(argv.size(), std::size_t{3});
    ASSERT_EQ(argv[0], "echo");
    ASSERT_EQ(argv[1], "hello");
    ASSERT_EQ(argv[2], "world");
}

static void test_tokenize_command_collapses_repeated_whitespace() {
    // Runs of spaces and tabs between arguments collapse to separators.
    const auto argv = tokenize_command("  a\t\t b   c ");

    ASSERT_EQ(argv.size(), std::size_t{3});
    ASSERT_EQ(argv[0], "a");
    ASSERT_EQ(argv[1], "b");
    ASSERT_EQ(argv[2], "c");
}

static void test_tokenize_command_double_quotes_preserve_spaces() {
    const auto argv = tokenize_command("cmd /c \"hello world\"");

    ASSERT_EQ(argv.size(), std::size_t{3});
    ASSERT_EQ(argv[0], "cmd");
    ASSERT_EQ(argv[1], "/c");
    ASSERT_EQ(argv[2], "hello world");
}

static void test_tokenize_command_single_quotes_preserve_spaces() {
    const auto argv = tokenize_command("app 'a b c'");

    ASSERT_EQ(argv.size(), std::size_t{2});
    ASSERT_EQ(argv[0], "app");
    ASSERT_EQ(argv[1], "a b c");
}

static void test_tokenize_command_backslash_escapes_space() {
    // A backslash escapes the following space, joining the token.
    const auto argv = tokenize_command("a\\ b");

    ASSERT_EQ(argv.size(), std::size_t{1});
    ASSERT_EQ(argv[0], "a b");
}

static void test_tokenize_command_backslash_escapes_quote() {
    const auto argv = tokenize_command("a\\\"b");

    ASSERT_EQ(argv.size(), std::size_t{1});
    ASSERT_EQ(argv[0], "a\"b");
}

static void test_tokenize_command_adjacent_quoted_concatenation() {
    // Quoted and unquoted spans abut to form a single argument.
    const auto argv = tokenize_command("foo\"bar baz\"qux");

    ASSERT_EQ(argv.size(), std::size_t{1});
    ASSERT_EQ(argv[0], "foobar bazqux");
}

static void test_tokenize_command_single_quotes_keep_backslash_literal() {
    // Inside single quotes a backslash is a literal byte, not an escape.
    const auto argv = tokenize_command("'a\\b'");

    ASSERT_EQ(argv.size(), std::size_t{1});
    ASSERT_EQ(argv[0], "a\\b");
}

static void test_tokenize_command_empty_input() {
    ASSERT_EQ(tokenize_command("").size(), std::size_t{0});
}

static void test_tokenize_command_whitespace_only_input() {
    ASSERT_EQ(tokenize_command("   \t  ").size(), std::size_t{0});
}

static void test_tokenize_command_unclosed_double_quote_throws() {
    ASSERT_THROWS_WITH_MESSAGE(tokenize_command("echo \"unterminated"), "unclosed quote");
}

static void test_tokenize_command_unclosed_single_quote_throws() {
    ASSERT_THROWS_WITH_MESSAGE(tokenize_command("echo 'unterminated"), "unclosed quote");
}

// ─── Process environment / introspection (portable) ─────────────────────────

static void test_process_get_environment_variable_path_success() {
    // PATH is defined on every supported platform.
    const auto v = eval("Process.get_environment_variable(\"PATH\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_string());
    ASSERT_FALSE(v.as_result()->owned_inner->as_string().empty());
}

static void test_process_has_environment_variable_true() {
    const auto v = eval("Process.has_environment_variable(\"PATH\")");

    ASSERT_TRUE(v.is_bool());
    ASSERT_TRUE(v.as_bool());
}

static void test_process_set_and_get_environment_variable_roundtrip() {
    // set_environment_variable mutates the real process environment, so a
    // subsequent get in the same program must observe the written value.
    ASSERT_EVAL_STR("Process.set_environment_variable(\"_LUMA_IO_TEST_VAR\", \"round-trip\")\n"
                    "Process.get_environment_variable(\"_LUMA_IO_TEST_VAR\")",
                    "round-trip");
}

static void test_process_current_directory_success() {
    const auto v = eval("Process.current_directory()");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_string());
    ASSERT_FALSE(v.as_result()->owned_inner->as_string().empty());
}

static void test_process_get_process_id_positive() {
    const auto v = eval("Process.get_process_id()");

    ASSERT_TRUE(v.is_integer());
    ASSERT_GT(v.as_integer(), 0);
}

static void test_process_get_all_environment_variables_contains_set_var() {
    const auto v = eval("Process.set_environment_variable(\"_LUMA_IO_ALLENV\", \"present\")\n"
                        "Process.get_all_environment_variables()");

    ASSERT_TRUE(v.is_dictionary());
    ASSERT_GT(v.as_dictionary()->entries.size(), std::size_t{0});

    const auto* entry = v.as_dictionary()->find("_LUMA_IO_ALLENV");

    ASSERT_TRUE(entry != nullptr);
    ASSERT_EQ(entry->as_string(), "present");
}

// ─── Process negative / argument-validation (portable) ──────────────────────

static void test_process_run_empty_command_fails() {
    // The empty-command guard fires before any spawn, on every platform.
    ASSERT_EVAL_FAILURE("Process.run(\"\")");
}

static void test_process_get_environment_variable_missing_fails() {
    ASSERT_EVAL_FAILURE("Process.get_environment_variable(\"_LUMA_DEFINITELY_NOT_SET_XYZ_999_\")");
}

static void test_process_get_environment_variable_rejects_non_string() {
    ASSERT_THROWS(eval("Process.get_environment_variable(42)"));
}

static void test_process_has_environment_variable_rejects_non_string() {
    ASSERT_THROWS(eval("Process.has_environment_variable(42)"));
}

static void test_process_set_environment_variable_empty_name_fails() {
    ASSERT_EVAL_FAILURE("Process.set_environment_variable(\"\", \"value\")");
}

static void test_process_set_environment_variable_name_with_equals_fails() {
    ASSERT_EVAL_FAILURE("Process.set_environment_variable(\"a=b\", \"value\")");
}

static void test_process_set_environment_variable_rejects_non_string() {
    ASSERT_THROWS(eval("Process.set_environment_variable(1, 2)"));
}

static void test_process_exit_out_of_range_throws() {
    // A 64-bit code that cannot fit in a 32-bit exit status is rejected before
    // any ExitSignal is raised, so this never terminates the test process.
    ASSERT_THROWS(eval("Process.exit(9999999999)"));
}

// ─── Process.run end-to-end execution (platform-specific commands) ──────────
// Process.run hands tokenized argv straight to the OS, so these use a shell
// that exists on the host: cmd.exe on Windows, /bin/sh elsewhere.

static void test_process_run_nonzero_exit_code() {
#ifdef _WIN32
    const auto v = eval("Process.run(\"cmd /c exit 7\")");
#else
    const auto v = eval("Process.run(\"sh -c \\\"exit 7\\\"\")");
#endif

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->as_record()->find_field("exit_code")->as_integer() ==
                7);
}

static void test_process_run_nonexistent_command_fails() {
    // A command that does not exist cannot be launched. Windows CreateProcessA
    // reports this directly; on POSIX the child's execvp failure is relayed to
    // the parent through a close-on-exec self-pipe. Either way Process.run
    // reports a failure result on every platform.
    ASSERT_EVAL_FAILURE("Process.run(\"_luma_nonexistent_command_xyz_123\")");
}

int main() {
    RUN(test_process_get_args_empty);
    RUN(test_process_get_args_with_values);
    RUN(test_process_run_returns_record);
    RUN(test_process_run_unclosed_quote);
    RUN(test_tokenize_command_simple);
    RUN(test_tokenize_command_collapses_repeated_whitespace);
    RUN(test_tokenize_command_double_quotes_preserve_spaces);
    RUN(test_tokenize_command_single_quotes_preserve_spaces);
    RUN(test_tokenize_command_backslash_escapes_space);
    RUN(test_tokenize_command_backslash_escapes_quote);
    RUN(test_tokenize_command_adjacent_quoted_concatenation);
    RUN(test_tokenize_command_single_quotes_keep_backslash_literal);
    RUN(test_tokenize_command_empty_input);
    RUN(test_tokenize_command_whitespace_only_input);
    RUN(test_tokenize_command_unclosed_double_quote_throws);
    RUN(test_tokenize_command_unclosed_single_quote_throws);
    RUN(test_process_get_environment_variable_path_success);
    RUN(test_process_has_environment_variable_true);
    RUN(test_process_set_and_get_environment_variable_roundtrip);
    RUN(test_process_current_directory_success);
    RUN(test_process_get_process_id_positive);
    RUN(test_process_get_all_environment_variables_contains_set_var);
    RUN(test_process_run_empty_command_fails);
    RUN(test_process_get_environment_variable_missing_fails);
    RUN(test_process_get_environment_variable_rejects_non_string);
    RUN(test_process_has_environment_variable_rejects_non_string);
    RUN(test_process_set_environment_variable_empty_name_fails);
    RUN(test_process_set_environment_variable_name_with_equals_fails);
    RUN(test_process_set_environment_variable_rejects_non_string);
    RUN(test_process_exit_out_of_range_throws);
    RUN(test_process_run_nonzero_exit_code);
    RUN(test_process_run_nonexistent_command_fails);

    return SUMMARY();
}
