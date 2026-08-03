// Standard library tests: Console.

#include <iostream>
#include <sstream>
#include <string>

#include "stdlib_test_helpers.hpp"

static void test_console_prompt_eof_fails() {
    std::istringstream fake_stdin{""};
    const StdinRedirect guard{fake_stdin.rdbuf()};

    ASSERT_EVAL_FAILURE("Console.prompt(\"\")");
}

static void test_console_module_registers_functions() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Console.prompt"));
    ASSERT_TRUE(env->has("Console.read_from_stdin"));
    ASSERT_TRUE(env->has("Console.write_to_stderr"));
    ASSERT_TRUE(env->has("Console.write_to_stdout"));
}

static void test_console_prompt_returns_line_and_echoes_prompt() {
    // Provide a line on stdin and verify the prompt text is written to stdout
    // (flushed, no newline) and the returned line has its trailing newline
    // stripped.
    std::istringstream fake_stdin{"Alice\nignored second line\n"};
    const StdinRedirect stdin_guard{fake_stdin.rdbuf()};

    std::string returned;
    {
        const CapturedStream captured{std::cout};

        const auto v = eval("Console.prompt(\"Name? \")");

        ASSERT_RESULT_SUCCESS(v);
        returned = v.as_result()->owned_inner->as_string();

        ASSERT_EQ(captured.str(), std::string("Name? "));
    }

    ASSERT_EQ(returned, std::string("Alice"));
}

static void test_console_prompt_rejects_non_string() {
    // A non-string argument is a runtime type error (not a failure result).
    std::istringstream fake_stdin{"\n"};
    const StdinRedirect guard{fake_stdin.rdbuf()};

    ASSERT_THROWS(eval("Console.prompt(42)"));
}

static void test_console_read_from_stdin() {
    // Redirect std::cin to a stringstream so the function can be tested
    // without interactive input.
    std::istringstream fake_stdin{"hello from stdin"};
    const StdinRedirect guard{fake_stdin.rdbuf()};

    ASSERT_EVAL_STR("Console.read_from_stdin()", "hello from stdin");
}

static void test_console_read_from_stdin_empty() {
    // Empty stdin is not an error — it yields a successful, empty string.
    std::istringstream fake_stdin{""};
    const StdinRedirect guard{fake_stdin.rdbuf()};

    const auto v = eval("Console.read_from_stdin()");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_string());
    ASSERT_EQ(v.as_result()->owned_inner->as_string(), "");
}

static void test_console_write_to_stderr() {
    // Capture stderr to verify the exact bytes written and the success result,
    // and to keep the test's own output off the real stderr.
    const CapturedStream captured{std::cerr};

    const auto v = eval("Console.write_to_stderr(\"err-output\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_bool());
    ASSERT_TRUE(v.as_result()->owned_inner->as_bool());
    ASSERT_EQ(captured.str(), std::string("err-output"));
}

static void test_console_write_to_stderr_rejects_non_string() {
    const CapturedStream captured{std::cerr};

    ASSERT_THROWS(eval("Console.write_to_stderr(true)"));
}

static void test_console_write_to_stdout() {
    // Capture stdout to verify the exact bytes written and the success result.
    const CapturedStream captured{std::cout};

    const auto v = eval("Console.write_to_stdout(\"hello\")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_bool());
    ASSERT_TRUE(v.as_result()->owned_inner->as_bool());
    ASSERT_EQ(captured.str(), std::string("hello"));
}

static void test_console_write_to_stdout_empty_string() {
    // Writing an empty string is a no-op write that still succeeds.
    const CapturedStream captured{std::cout};

    ASSERT_EVAL_BOOL("Console.write_to_stdout(\"\")", true);
    ASSERT_EQ(captured.str(), std::string(""));
}

static void test_console_write_to_stdout_rejects_non_string() {
    const CapturedStream captured{std::cout};

    ASSERT_THROWS(eval("Console.write_to_stdout(42)"));
}

// ─── read_line / read_lines / typed prompts / confirm / defaults ───

static void test_console_read_line_strips_newline() {
    std::istringstream fake_stdin{"first line\nsecond line\n"};
    const StdinRedirect guard{fake_stdin.rdbuf()};

    ASSERT_EVAL_STR("Console.read_line()", "first line");
}

static void test_console_read_line_eof_fails() {
    std::istringstream fake_stdin{""};
    const StdinRedirect guard{fake_stdin.rdbuf()};

    ASSERT_EVAL_FAILURE("Console.read_line()");
}

static void test_console_read_lines_splits_input() {
    std::istringstream fake_stdin{"a\nb\nc\n"};
    const StdinRedirect guard{fake_stdin.rdbuf()};

    const auto v = eval("Console.read_lines()");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_TRUE(v.as_result()->owned_inner->is_array());

    const auto& elems = *v.as_result()->owned_inner->as_array()->elements;
    ASSERT_EQ(elems.size(), 3U);
    ASSERT_EQ(elems[0].as_string(), "a");
    ASSERT_EQ(elems[2].as_string(), "c");
}

static void test_console_prompt_integer_parses() {
    std::istringstream fake_stdin{"  42  \n"};
    const StdinRedirect guard{fake_stdin.rdbuf()};

    const CapturedStream captured{std::cout};
    ASSERT_EVAL_INT("Console.prompt_integer(\"n? \")", 42);
}

static void test_console_prompt_integer_rejects_non_number() {
    std::istringstream fake_stdin{"not a number\n"};
    const StdinRedirect guard{fake_stdin.rdbuf()};

    const CapturedStream captured{std::cout};
    ASSERT_EVAL_FAILURE("Console.prompt_integer(\"n? \")");
}

static void test_console_prompt_number_parses() {
    std::istringstream fake_stdin{"3.14\n"};
    const StdinRedirect guard{fake_stdin.rdbuf()};

    const CapturedStream captured{std::cout};
    const auto v = eval("Console.prompt_number(\"x? \")");

    ASSERT_RESULT_SUCCESS(v);
    ASSERT_NEAR(v.as_result()->owned_inner->as_number(), 3.14, 0.0001);
}

static void test_console_confirm_yes_no() {
    {
        std::istringstream fake_stdin{"YES\n"};
        const StdinRedirect guard{fake_stdin.rdbuf()};
        const CapturedStream captured{std::cout};
        ASSERT_EVAL_BOOL("Console.confirm(\"ok? \")", true);
    }
    {
        std::istringstream fake_stdin{"n\n"};
        const StdinRedirect guard{fake_stdin.rdbuf()};
        const CapturedStream captured{std::cout};
        ASSERT_EVAL_BOOL("Console.confirm(\"ok? \")", false);
    }
    {
        std::istringstream fake_stdin{"maybe\n"};
        const StdinRedirect guard{fake_stdin.rdbuf()};
        const CapturedStream captured{std::cout};
        ASSERT_EVAL_FAILURE("Console.confirm(\"ok? \")");
    }
}

static void test_console_confirm_error_message_uses_trimmed_original_case() {
    // The failure message must report the trimmed *original-case* input
    // (not the lowercased comparison value), with surrounding whitespace
    // stripped. This pins the exact behaviour preserved when the local
    // trim_ascii()/stream_is_tty() duplicates were replaced with the shared
    // trim() utility and platform_terminal helpers.
    std::istringstream fake_stdin{"  Maybe  \n"};
    const StdinRedirect guard{fake_stdin.rdbuf()};
    const CapturedStream captured{std::cout};

    const auto v = eval("Console.confirm(\"ok? \")");

    ASSERT_RESULT_FAILURE(v);
    ASSERT_EQ(v.as_result()->owned_inner->as_string(), "Console.confirm: 'Maybe' is not yes or no");
}

static void test_console_prompt_with_default() {
    {
        // Empty line accepts the default.
        std::istringstream fake_stdin{"\n"};
        const StdinRedirect guard{fake_stdin.rdbuf()};
        const CapturedStream captured{std::cout};
        ASSERT_EVAL_STR("Console.prompt_with_default(\"name? \", \"Anon\")", "Anon");
    }
    {
        std::istringstream fake_stdin{"Alice\n"};
        const StdinRedirect guard{fake_stdin.rdbuf()};
        const CapturedStream captured{std::cout};
        ASSERT_EVAL_STR("Console.prompt_with_default(\"name? \", \"Anon\")", "Alice");
    }
}

static void test_console_new_functions_registered() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Console.read_line"));
    ASSERT_TRUE(env->has("Console.read_lines"));
    ASSERT_TRUE(env->has("Console.flush"));
    ASSERT_TRUE(env->has("Console.is_tty"));
    ASSERT_TRUE(env->has("Console.is_interactive"));
    ASSERT_TRUE(env->has("Console.prompt_integer"));
    ASSERT_TRUE(env->has("Console.prompt_number"));
    ASSERT_TRUE(env->has("Console.confirm"));
    ASSERT_TRUE(env->has("Console.prompt_with_default"));
}

int main() {
    RUN(test_console_prompt_eof_fails);
    RUN(test_console_module_registers_functions);
    RUN(test_console_prompt_returns_line_and_echoes_prompt);
    RUN(test_console_prompt_rejects_non_string);
    RUN(test_console_read_from_stdin);
    RUN(test_console_read_from_stdin_empty);
    RUN(test_console_write_to_stderr);
    RUN(test_console_write_to_stderr_rejects_non_string);
    RUN(test_console_write_to_stdout);
    RUN(test_console_write_to_stdout_empty_string);
    RUN(test_console_write_to_stdout_rejects_non_string);
    RUN(test_console_read_line_strips_newline);
    RUN(test_console_read_line_eof_fails);
    RUN(test_console_read_lines_splits_input);
    RUN(test_console_prompt_integer_parses);
    RUN(test_console_prompt_integer_rejects_non_number);
    RUN(test_console_prompt_number_parses);
    RUN(test_console_confirm_yes_no);
    RUN(test_console_confirm_error_message_uses_trimmed_original_case);
    RUN(test_console_prompt_with_default);
    RUN(test_console_new_functions_registered);

    return SUMMARY();
}
