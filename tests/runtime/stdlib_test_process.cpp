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

// ─── Process.execute — captures stdout + stderr separately (N06) ────────────
// Unlike Process.run (which merges both streams into one `output` field),
// Process.execute returns a Process.CommandOutput record with separate
// standard_output / standard_error buffers plus a derived success flag.

static void test_process_execute_returns_command_output() {
#ifdef _WIN32
    const auto v = eval("Process.execute(\"cmd /c echo hello\")");
#else
    const auto v = eval("Process.execute(\"echo hello\")");
#endif

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;

    ASSERT_TRUE(inner.is_record());
    ASSERT_TRUE(inner.as_record()->type_name == "CommandOutput");
    ASSERT_TRUE(inner.as_record()->find_field("exit_code") != nullptr);
    ASSERT_TRUE(inner.as_record()->find_field("standard_output") != nullptr);
    ASSERT_TRUE(inner.as_record()->find_field("standard_error") != nullptr);
    ASSERT_TRUE(inner.as_record()->find_field("success") != nullptr);

    ASSERT_TRUE(inner.as_record()->find_field("exit_code")->is_integer());
    ASSERT_TRUE(inner.as_record()->find_field("exit_code")->as_integer() == 0);
    ASSERT_TRUE(inner.as_record()->find_field("standard_output")->is_string());
    ASSERT_TRUE(inner.as_record()->find_field("standard_output")->as_string().find("hello") !=
                std::string::npos);
    ASSERT_TRUE(inner.as_record()->find_field("success")->is_bool());
    ASSERT_TRUE(inner.as_record()->find_field("success")->as_bool());
}

static void test_process_execute_captures_stderr() {
    // A command that writes only to stderr must populate standard_error while
    // leaving standard_output empty — the whole point of the separate capture.
#ifdef _WIN32
    const auto v = eval("Process.execute(\"cmd /c \\\"echo err_text 1>&2\\\"\")");
#else
    const auto v = eval("Process.execute(\"sh -c \\\"echo err_text 1>&2\\\"\")");
#endif

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;

    ASSERT_TRUE(inner.as_record()->find_field("standard_error")->as_string().find("err_text") !=
                std::string::npos);
    ASSERT_TRUE(inner.as_record()->find_field("standard_output")->as_string().find("err_text") ==
                std::string::npos);
}

static void test_process_execute_nonzero_exit_sets_success_false() {
#ifdef _WIN32
    const auto v = eval("Process.execute(\"cmd /c exit 7\")");
#else
    const auto v = eval("Process.execute(\"sh -c \\\"exit 7\\\"\")");
#endif

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;

    ASSERT_TRUE(inner.as_record()->find_field("exit_code")->as_integer() == 7);
    ASSERT_FALSE(inner.as_record()->find_field("success")->as_bool());
}

static void test_process_execute_nonexistent_command_fails() {
    ASSERT_EVAL_FAILURE("Process.execute(\"_luma_nonexistent_command_xyz_123\")");
}

static void test_process_execute_empty_command_fails() {
    ASSERT_EVAL_FAILURE("Process.execute(\"\")");
}

static void test_process_execute_unclosed_quote_fails() {
    ASSERT_EVAL_FAILURE("Process.execute(\"echo \\\"unclosed\")");
}

// ─── Process.Command / Process.run_command (T01) ─────────────────────────────

static void test_process_command_builds_record() {
    const auto v = eval(R"(Process.command("echo", ["a", "b"]))");

    ASSERT_TRUE(v.is_record());
    ASSERT_EQ(v.as_record()->type_name, std::string{"Command"});
    ASSERT_EQ(v.as_record()->find_field("program")->as_string(), "echo");

    const auto* args = v.as_record()->find_field("arguments");
    ASSERT_TRUE(args->is_array());
    ASSERT_EQ(args->as_array()->elements->size(), std::size_t{2});
    ASSERT_EQ((*args->as_array()->elements)[0].as_string(), "a");
}

static void test_process_command_rejects_non_string_argument() {
    ASSERT_THROWS(eval(R"(Process.command("echo", [1, 2]))"));
}

static void test_process_run_command_returns_command_output() {
#ifdef _WIN32
    const auto v = eval(R"(Process.run_command(Process.command("cmd", ["/c", "echo", "hello"])))");
#else
    const auto v = eval(R"(Process.run_command(Process.command("echo", ["hello"])))");
#endif

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;
    ASSERT_TRUE(inner.is_record());
    ASSERT_EQ(inner.as_record()->type_name, std::string{"CommandOutput"});
    ASSERT_EQ(inner.as_record()->find_field("exit_code")->as_integer(),
              static_cast<std::int64_t>(0));
    ASSERT_TRUE(inner.as_record()->find_field("success")->as_bool());
    ASSERT_TRUE(inner.as_record()->find_field("standard_output")->as_string().find("hello") !=
                std::string::npos);
}

#ifndef _WIN32
static void test_process_run_command_metacharacters_are_inert() {
    // No shell is involved, so ';', '|' and '$(...)' reach echo as a literal
    // argument instead of being interpreted.
    const auto v =
        eval(R"LUMA(Process.run_command(Process.command("echo", ["a; b | c $(whoami)"])))LUMA");

    ASSERT_RESULT_SUCCESS(v);

    const auto& out =
        v.as_result()->owned_inner->as_record()->find_field("standard_output")->as_string();
    ASSERT_TRUE(out.find("a; b | c $(whoami)") != std::string::npos);
}
#endif

static void test_process_run_command_nonexistent_program_fails() {
    ASSERT_EVAL_FAILURE(R"(Process.run_command(Process.command("_luma_nonexistent_xyz_123", [])))");
}

static void test_process_run_command_empty_program_fails() {
    ASSERT_EVAL_FAILURE(R"(Process.run_command(Process.command("", ["x"])))");
}

static void test_process_run_command_rejects_non_record() {
    ASSERT_THROWS(eval("Process.run_command(42)"));
}

// ─── Process.exit_status — classifies the exit_code sign convention ────────
// 0 = Success, a positive code = Failed(code), a negative code = LaunchFailed
// (the process never ran).  Derives every CommandOutput from a real
// Process.execute call and uses `with { exit_code = ... }` to explore the
// other branches — a record literal can't set `success` directly because it
// lexes as the `success` pattern-match keyword, not an Identifier, in field
// position (a pre-existing parser quirk unrelated to this classifier).

static void test_process_exit_status_zero_is_success() {
#ifdef _WIN32
    const auto v = eval(R"(Process.exit_status(Result.unwrap(Process.execute("cmd /c echo hi"))))");
#else
    const auto v = eval(R"(Process.exit_status(Result.unwrap(Process.execute("echo hi"))))");
#endif

    ASSERT_TRUE(v.is_choice());
    ASSERT_EQ(v.as_choice()->type_name, std::string{"ExitStatus"});
    ASSERT_EQ(v.as_choice()->variant, std::string{"Success"});
    ASSERT_TRUE(v.as_choice()->fields.empty());
}

static void test_process_exit_status_positive_is_failed_with_code() {
#ifdef _WIN32
    const auto v = eval(R"(Process.exit_status(
        Result.unwrap(Process.execute("cmd /c echo hi")) with { exit_code = 7 }))");
#else
    const auto v = eval(R"(Process.exit_status(
        Result.unwrap(Process.execute("echo hi")) with { exit_code = 7 }))");
#endif

    ASSERT_TRUE(v.is_choice());
    ASSERT_EQ(v.as_choice()->type_name, std::string{"ExitStatus"});
    ASSERT_EQ(v.as_choice()->variant, std::string{"Failed"});
    ASSERT_EQ(v.as_choice()->fields.size(), std::size_t{1});
    ASSERT_EQ(v.as_choice()->fields[0].as_integer(), static_cast<std::int64_t>(7));
}

static void test_process_exit_status_negative_is_launch_failed() {
#ifdef _WIN32
    const auto v = eval(R"(Process.exit_status(
        Result.unwrap(Process.execute("cmd /c echo hi")) with { exit_code = -1 }))");
#else
    const auto v = eval(R"(Process.exit_status(
        Result.unwrap(Process.execute("echo hi")) with { exit_code = -1 }))");
#endif

    ASSERT_TRUE(v.is_choice());
    ASSERT_EQ(v.as_choice()->type_name, std::string{"ExitStatus"});
    ASSERT_EQ(v.as_choice()->variant, std::string{"LaunchFailed"});
    ASSERT_TRUE(v.as_choice()->fields.empty());
}

static void test_process_exit_status_rejects_non_record() {
    ASSERT_THROWS(eval("Process.exit_status(42)"));
}

// ─── Process.run_command_typed (typed launch errors via Process.Error) ────────

// Reads the Process.Error variant name from a run_command_typed failure result.
[[nodiscard]] static std::string process_error_variant_of(const luma::Value& v) {
    const auto& inner = v.as_result()->owned_inner;
    if (!inner->is_choice()) {
        return "<not-a-choice>";
    }
    return inner->as_choice()->type_name + "." + inner->as_choice()->variant;
}

static void test_process_run_command_typed_success_returns_output() {
#ifdef _WIN32
    const auto v =
        eval(R"(Process.run_command_typed(Process.command("cmd", ["/c", "echo", "hi"])))");
#else
    const auto v = eval(R"(Process.run_command_typed(Process.command("echo", ["hi"])))");
#endif

    ASSERT_RESULT_SUCCESS(v);

    const auto& inner = *v.as_result()->owned_inner;
    ASSERT_TRUE(inner.is_record());
    ASSERT_EQ(inner.as_record()->type_name, std::string{"CommandOutput"});
    ASSERT_EQ(inner.as_record()->find_field("exit_code")->as_integer(),
              static_cast<std::int64_t>(0));
}

static void test_process_run_command_typed_nonexistent_is_not_found() {
    const auto v =
        eval(R"(Process.run_command_typed(Process.command("_luma_nonexistent_xyz_123", [])))");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(process_error_variant_of(v), "Error.NotFound");
}

static void test_process_run_command_typed_empty_program_is_invalid() {
    const auto v = eval(R"(Process.run_command_typed(Process.command("", ["x"])))");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(process_error_variant_of(v), "Error.InvalidCommand");
}

static void test_process_run_command_typed_rejects_non_record() {
    ASSERT_THROWS(eval("Process.run_command_typed(42)"));
}

static void test_process_run_command_typed_registered() {
    const auto env = luma::test::make_std_env();
    ASSERT_TRUE(env->has("Process.run_command_typed"));
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
    RUN(test_process_execute_returns_command_output);
    RUN(test_process_execute_captures_stderr);
    RUN(test_process_execute_nonzero_exit_sets_success_false);
    RUN(test_process_execute_nonexistent_command_fails);
    RUN(test_process_execute_empty_command_fails);
    RUN(test_process_execute_unclosed_quote_fails);
    RUN(test_process_command_builds_record);
    RUN(test_process_command_rejects_non_string_argument);
    RUN(test_process_run_command_returns_command_output);
#ifndef _WIN32
    RUN(test_process_run_command_metacharacters_are_inert);
#endif
    RUN(test_process_run_command_nonexistent_program_fails);
    RUN(test_process_run_command_empty_program_fails);
    RUN(test_process_run_command_rejects_non_record);
    RUN(test_process_exit_status_zero_is_success);
    RUN(test_process_exit_status_positive_is_failed_with_code);
    RUN(test_process_exit_status_negative_is_launch_failed);
    RUN(test_process_exit_status_rejects_non_record);
    RUN(test_process_run_command_typed_success_returns_output);
    RUN(test_process_run_command_typed_nonexistent_is_not_found);
    RUN(test_process_run_command_typed_empty_program_is_invalid);
    RUN(test_process_run_command_typed_rejects_non_record);
    RUN(test_process_run_command_typed_registered);

    return SUMMARY();
}
