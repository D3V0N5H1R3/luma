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

    return SUMMARY();
}
