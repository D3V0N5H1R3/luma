// Standard library tests: Log.

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "stdlib_test_helpers.hpp"

using luma::Value;
using luma::test::CapturedStream;
using luma::test::eval;
using luma::test::make_sandboxed_std_env;
using luma::test::make_std_env;
using luma::test::read_file_text;
using luma::test::TempFile;

namespace {

// Restore the process-global Log configuration (level, format, output sink,
// context) to its defaults on construction and on destruction, so a test that
// throws part-way through can never leak a redirect or level into later tests.
// Log.reset() also closes any file sink, flushing it so the file is readable.
class ScopedLogReset {
public:
    ScopedLogReset() {
        reset();
    }

    ScopedLogReset(const ScopedLogReset&) = delete;
    ScopedLogReset& operator=(const ScopedLogReset&) = delete;
    ScopedLogReset(ScopedLogReset&&) = delete;
    ScopedLogReset& operator=(ScopedLogReset&&) = delete;

    ~ScopedLogReset() noexcept {
        try {
            reset();
        } catch (const std::exception&) { // NOLINT(bugprone-empty-catch)
            // An exception must not escape a destructor.
        }
    }

private:
    static void reset() {
        // Log.reset() returns nothing worth checking.
        (void)eval("Log.reset()");
    }
};

// Strip carriage returns so captured output compares equal to '\n'-terminated
// expectations on every platform (the log file stream writes in text mode, so
// on Windows newlines are stored as CRLF).
[[nodiscard]] std::string without_carriage_returns(std::string text) {
    const auto removed = std::ranges::remove(text, '\r');
    text.erase(removed.begin(), removed.end());
    return text;
}

// Route Log output to a private file through `set_output_expr`, evaluate the
// Luma program, and return the file's contents. The Log configuration is reset
// before and after, so nothing leaks into subsequent tests.
[[nodiscard]] std::string capture_log_output_via(const std::string& set_output_expr,
                                                 const std::string& program) {
    // Log.set_output opens the file in append mode, so a TempFile gives a clean
    // (truncated) slate up front and guarantees the file is removed on scope
    // exit — even if the eval below throws.
    const TempFile log_file{std::filesystem::path{"_test_log_capture.log"}, ""};

    {
        const ScopedLogReset log_reset;
        // The program's last statement is a logging call, which yields nothing.
        (void)eval(set_output_expr + "\n" + program);
    }

    return without_carriage_returns(read_file_text(log_file.path()));
}

// Route Log output to a private file with the string-path form of
// Log.set_output, evaluate the program, and return the file's contents. Tests
// that pin a format keep the captured text deterministic for exact comparison.
[[nodiscard]] std::string capture_log_output(const std::string& program) {
    return capture_log_output_via("Log.set_output(\"_test_log_capture.log\")", program);
}

// Evaluate the Luma program with the default Log configuration (output to
// stderr) and return everything it wrote to std::cerr.
[[nodiscard]] std::string capture_stderr_log(const std::string& program) {
    const ScopedLogReset log_reset;
    const CapturedStream captured{std::cerr};

    // The program's last statement is a logging call, which yields nothing.
    (void)eval(program);

    return captured.str();
}

// Reset the global log level, apply the given set expression, and read the
// level back.  Returns the resulting Log.Level choice value.
[[nodiscard]] Value level_roundtrip(const std::string& set_expr) {
    const ScopedLogReset log_reset;
    return eval(set_expr + "\nLog.get_level()");
}

} // namespace

// ─── Registration ───

static void test_log_module() {
    const auto env = make_std_env();

    ASSERT_TRUE(env->has("Log.debug"));
    ASSERT_TRUE(env->has("Log.information"));
    ASSERT_TRUE(env->has("Log.warning"));
    ASSERT_TRUE(env->has("Log.error"));
    ASSERT_TRUE(env->has("Log.set_level"));
    ASSERT_TRUE(env->has("Log.get_level"));
    ASSERT_TRUE(env->has("Log.set_format"));
    ASSERT_TRUE(env->has("Log.set_context"));
    ASSERT_TRUE(env->has("Log.clear_context"));
    ASSERT_TRUE(env->has("Log.set_output"));
    ASSERT_TRUE(env->has("Log.reset"));
}

static void test_log_set_output_disabled_in_sandbox() {
    // set_output opens files, so it is withheld from sandboxed environments;
    // the pure-configuration functions remain available.
    const auto env = make_sandboxed_std_env();

    ASSERT_FALSE(env->has("Log.set_output"));
    ASSERT_TRUE(env->has("Log.information"));
    ASSERT_TRUE(env->has("Log.set_level"));
}

// ─── Emission with the default configuration (stderr, default format) ───

static void test_log_debug() {
    // Debug lies below the default Information threshold, so lower the level.
    const auto out = capture_stderr_log("Log.set_level(Log.Level.Debug)\n"
                                        "Log.debug(\"test debug\")");

    ASSERT_NE(out.find("[DEBUG] test debug\n"), std::string::npos);
}

static void test_log_info() {
    const auto out = capture_stderr_log("Log.information(\"test message\")");

    ASSERT_NE(out.find("[INFO] test message\n"), std::string::npos);
}

static void test_log_warn() {
    const auto out = capture_stderr_log("Log.warning(\"test warning\")");

    ASSERT_NE(out.find("[WARN] test warning\n"), std::string::npos);
}

static void test_log_error() {
    const auto out = capture_stderr_log("Log.error(\"test error\")");

    ASSERT_NE(out.find("[ERROR] test error\n"), std::string::npos);
}

// ─── Level get/set ───

static void test_log_get_level_returns_level_choice() {
    const ScopedLogReset log_reset;
    const auto v = eval("Log.get_level()");

    ASSERT_TRUE(v.is_choice());
    ASSERT_EQ(v.as_choice()->type_name, "Level");
    ASSERT_EQ(v.as_choice()->variant, "Information"); // reset() restores Information
}

static void test_log_set_level() {
    const ScopedLogReset log_reset;

    // Test with string argument (backward compatible).
    const auto from_string = eval(R"(
        Log.set_level("warn")
        Log.get_level()
    )");

    ASSERT_TRUE(from_string.is_choice());
    ASSERT_EQ(from_string.as_choice()->type_name, "Level");
    ASSERT_EQ(from_string.as_choice()->variant, "Warning");

    // Test with choice argument.
    const auto from_choice = eval(R"(
        Log.set_level(Log.Level.Error)
        Log.get_level()
    )");

    ASSERT_TRUE(from_choice.is_choice());
    ASSERT_EQ(from_choice.as_choice()->variant, "Error");
}

static void test_log_set_level_all_choice_variants() {
    for (const auto* variant : {"Debug", "Information", "Warning", "Error", "Off"}) {
        const auto v = level_roundtrip(std::string{"Log.set_level(Log.Level."} + variant + ")");

        ASSERT_TRUE(v.is_choice());
        ASSERT_EQ(v.as_choice()->variant, std::string{variant});
    }
}

static void test_log_set_level_string_variants() {
    // The runtime also accepts lowercase string level names.
    constexpr std::array<std::pair<std::string_view, std::string_view>, 5> cases{{
        {"debug", "Debug"},
        {"info", "Information"},
        {"warn", "Warning"},
        {"error", "Error"},
        {"off", "Off"},
    }};

    for (const auto& [arg, expected] : cases) {
        const auto v = level_roundtrip("Log.set_level(\"" + std::string{arg} + "\")");

        ASSERT_TRUE(v.is_choice());
        ASSERT_EQ(v.as_choice()->variant, std::string{expected});
    }
}

static void test_log_set_level_unknown_string_defaults_to_info() {
    // An unrecognised level name falls back to Information rather than throwing.
    const auto v = level_roundtrip(R"(Log.set_level("verbose"))");

    ASSERT_TRUE(v.is_choice());
    ASSERT_EQ(v.as_choice()->variant, "Information");
}

// ─── Formatting ───

static void test_log_format_substitutes_level_and_message() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${level}::\${message}")
Log.set_level(Log.Level.Debug)
Log.information("hello world"))LUMA");

    ASSERT_EQ(out, "INFO::hello world\n");
}

static void test_log_format_substitutes_timestamp() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${timestamp}")
Log.set_level(Log.Level.Debug)
Log.information("ignored"))LUMA");

    // The ${timestamp} placeholder is replaced by an ISO-8601 UTC stamp.
    ASSERT_EQ(out.find("${timestamp}"), std::string::npos);
    ASSERT_NE(out.find('T'), std::string::npos);
    ASSERT_NE(out.find('Z'), std::string::npos);
    ASSERT_FALSE(out.empty());
    ASSERT_EQ(out.back(), '\n');
}

static void test_log_format_preserves_unknown_placeholder() {
    // ${foo} is not a recognised field, so it is emitted verbatim.
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${foo}")
Log.set_level(Log.Level.Debug)
Log.information("ignored"))LUMA");

    ASSERT_EQ(out, "${foo}\n");
}

static void test_log_format_literal_prefix() {
    // A format with no placeholders is emitted as a literal prefix; the message
    // text is not appended unless ${message} appears.
    const auto out = capture_log_output(R"LUMA(Log.set_format("[APP] ")
Log.set_level(Log.Level.Debug)
Log.warning("dropped"))LUMA");

    ASSERT_EQ(out, "[APP] \n");
}

static void test_log_default_format_after_reset() {
    // With the default format restored by the capture prologue, an info line
    // carries the [LEVEL] tag, the message, and an ISO-8601 timestamp.
    const auto out = capture_log_output(R"LUMA(Log.information("ready"))LUMA");

    ASSERT_NE(out.find("[INFO] ready"), std::string::npos);
    ASSERT_NE(out.find('Z'), std::string::npos);
}

// ─── Typed Log.Output target (Stderr / Stdout / File) ───

static void test_log_set_output_typed_file_writes() {
    // The typed Log.Output.File(path) variant routes output to the file exactly
    // like the string-path form the other capture tests use.
    const auto out = capture_log_output_via(
        R"LUMA(Log.set_output(Log.Output.File("_test_log_capture.log")))LUMA",
        R"LUMA(Log.set_format("\${level}::\${message}")
Log.information("typed"))LUMA");

    ASSERT_EQ(out, "INFO::typed\n");
}

static void test_log_set_output_typed_streams_route_output() {
    // The Stdout / Stderr variants are accepted, report success, and route the
    // following lines to the matching standard stream.
    const ScopedLogReset log_reset;
    const CapturedStream captured_out{std::cout};
    const CapturedStream captured_err{std::cerr};

    ASSERT_RESULT_SUCCESS(eval("Log.set_format(\"\\${message}\")\n"
                               "Log.set_output(Log.Output.Stdout)"));
    (void)eval("Log.information(\"to stdout\")");

    ASSERT_RESULT_SUCCESS(eval("Log.set_output(Log.Output.Stderr)"));
    (void)eval("Log.information(\"to stderr\")");

    ASSERT_EQ(captured_out.str(), "to stdout\n");
    ASSERT_EQ(captured_err.str(), "to stderr\n");
}

// ─── Level filtering ───

static void test_log_level_filtering_suppresses_below_threshold() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Error)
Log.debug("d")
Log.information("i")
Log.warning("w")
Log.error("kept"))LUMA");

    ASSERT_EQ(out, "kept\n");
}

static void test_log_level_off_suppresses_everything() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Off)
Log.debug("d")
Log.information("i")
Log.warning("w")
Log.error("e"))LUMA");

    ASSERT_EQ(out, "");
}

static void test_log_level_debug_emits_every_level() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Debug)
Log.debug("a")
Log.information("b")
Log.warning("c")
Log.error("d"))LUMA");

    ASSERT_EQ(out, "a\nb\nc\nd\n");
}

// ─── Context ───

static void test_log_set_context() {
    // With the default format, the context pairs follow the message.
    const auto out = capture_stderr_log("Log.set_context(\"key\", \"value\")\n"
                                        "Log.information(\"with context\")");

    ASSERT_NE(out.find("[INFO] with context | key=value\n"), std::string::npos);
}

static void test_log_context_appended_to_output() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Debug)
Log.set_context("k", "v")
Log.information("m"))LUMA");

    ASSERT_EQ(out, "m | k=v\n");
}

static void test_log_context_multiple_pairs_preserve_order() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Debug)
Log.set_context("k1", "v1")
Log.set_context("k2", "v2")
Log.information("m"))LUMA");

    ASSERT_EQ(out, "m | k1=v1 k2=v2\n");
}

static void test_log_set_context_updates_existing_key() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Debug)
Log.set_context("k", "v1")
Log.set_context("k", "v2")
Log.information("m"))LUMA");

    ASSERT_EQ(out, "m | k=v2\n");
}

static void test_log_clear_context_removes_pairs() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Debug)
Log.set_context("k", "v")
Log.clear_context()
Log.information("m"))LUMA");

    ASSERT_EQ(out, "m\n");
}

// ─── Output routing & reset ───

static void test_log_set_format() {
    // A placeholder-free format replaces the whole line layout: the emitted
    // line is the literal format text, with no timestamp, level, or message.
    const auto out = capture_stderr_log("Log.set_format(\"text\")\n"
                                        "Log.information(\"hidden\")");

    ASSERT_EQ(out, "text\n");
}

static void test_log_set_output_standard_streams_succeed() {
    const ScopedLogReset log_reset;

    ASSERT_RESULT_SUCCESS(eval(R"(Log.set_output("stderr"))"));
    ASSERT_RESULT_SUCCESS(eval(R"(Log.set_output("stdout"))"));
}

static void test_log_reset_restores_info_level() {
    const ScopedLogReset log_reset;
    const auto v = eval(R"(
        Log.set_level(Log.Level.Error)
        Log.reset()
        Log.get_level()
    )");

    ASSERT_TRUE(v.is_choice());
    ASSERT_EQ(v.as_choice()->variant, "Information");
}

static void test_log_reset_clears_context_and_format() {
    // Log.reset() must clear the context and format set earlier in the same
    // program; logging afterwards uses the default format and shows no trailing
    // context fields.
    const ScopedLogReset log_reset;
    const CapturedStream captured{std::cerr};

    (void)eval(R"(
        Log.set_context("stale", "should_not_appear")
        Log.set_format("\${message}")
        Log.reset()
        Log.information("fresh")
    )");

    const auto out = captured.str();

    ASSERT_EQ(out.find("stale"), std::string::npos);
    ASSERT_EQ(out.find("should_not_appear"), std::string::npos);
    ASSERT_NE(out.find("[INFO] fresh\n"), std::string::npos);
}

static void test_log_set_output_failed_open_keeps_current_file() {
    // Regression: set_output closed the current file BEFORE opening the new one,
    // so a failed open left the sink set to File with a closed stream and every
    // later line was silently dropped.  The parent directory does not exist, so
    // the open fails while the path itself passes the sandbox validation.
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Debug)
assert(Result.is_failure(Log.set_output("_luma_no_such_dir_xyz/redirect.log")))
Log.information("still logged"))LUMA");

    ASSERT_EQ(out, "still logged\n");
}

// ─── Negative tests: argument types ───

static void test_log_debug_rejects_non_string() {
    ASSERT_EVAL_THROWS("Log.debug(42)", "Log.debug: expected string, got 'integer'");
}

static void test_log_info_rejects_non_string() {
    ASSERT_EVAL_THROWS("Log.information(true)", "Log.information: expected string, got 'boolean'");
}

static void test_log_warn_rejects_non_string() {
    ASSERT_EVAL_THROWS("Log.warning(3.14)", "Log.warning: expected string, got 'number'");
}

static void test_log_error_rejects_non_string() {
    ASSERT_EVAL_THROWS("Log.error([1, 2])", "Log.error: expected string, got 'array'");
}

static void test_log_set_format_rejects_non_string() {
    ASSERT_EVAL_THROWS("Log.set_format(99)", "Log.set_format: expected string, got 'integer'");
}

static void test_log_set_context_rejects_non_string_key() {
    ASSERT_EVAL_THROWS(R"(Log.set_context(1, "v"))", "expected string key and value");
}

static void test_log_set_context_rejects_non_string_value() {
    ASSERT_EVAL_THROWS(R"(Log.set_context("k", 2))", "expected string key and value");
}

static void test_log_set_level_rejects_invalid_type() {
    ASSERT_EVAL_THROWS("Log.set_level(42)", "expected a LogLevel or string");
}

static void test_log_set_output_rejects_non_string() {
    ASSERT_EVAL_THROWS("Log.set_output(123)", "Log.set_output: expected a Log.Output or string");
}

// ─── Negative tests: result-returning failures ───

static void test_log_set_output_invalid_path_returns_failure() {
    const ScopedLogReset log_reset; // restore the default output sink afterwards

    ASSERT_RESULT_FAILURE(eval(R"(Log.set_output("/no/such/dir/sub/missing.log"))"));
}

// ─── Negative tests: arity ───

static void test_log_debug_rejects_wrong_arity() {
    ASSERT_EVAL_THROWS("Log.debug()", "Log.debug: expected exactly 1 argument(s), got 0");
    ASSERT_EVAL_THROWS(R"(Log.debug("a", "b"))",
                       "Log.debug: expected exactly 1 argument(s), got 2");
}

static void test_log_get_level_rejects_arguments() {
    ASSERT_EVAL_THROWS(R"(Log.get_level("unexpected"))",
                       "Log.get_level: expected exactly 0 argument(s), got 1");
}

static void test_log_clear_context_rejects_arguments() {
    ASSERT_EVAL_THROWS(R"(Log.clear_context("unexpected"))",
                       "Log.clear_context: expected exactly 0 argument(s), got 1");
}

static void test_log_set_context_rejects_wrong_arity() {
    ASSERT_EVAL_THROWS(R"(Log.set_context("only_key"))",
                       "Log.set_context: expected exactly 2 argument(s), got 1");
}

int main() {
    // Registration.
    RUN(test_log_module);
    RUN(test_log_set_output_disabled_in_sandbox);
    RUN(test_log_set_output_failed_open_keeps_current_file);

    // Emission with the default configuration.
    RUN(test_log_debug);
    RUN(test_log_info);
    RUN(test_log_warn);
    RUN(test_log_error);

    // Level get/set.
    RUN(test_log_get_level_returns_level_choice);
    RUN(test_log_set_level);
    RUN(test_log_set_level_all_choice_variants);
    RUN(test_log_set_level_string_variants);
    RUN(test_log_set_level_unknown_string_defaults_to_info);

    // Formatting.
    RUN(test_log_format_substitutes_level_and_message);
    RUN(test_log_format_substitutes_timestamp);
    RUN(test_log_format_preserves_unknown_placeholder);
    RUN(test_log_format_literal_prefix);
    RUN(test_log_default_format_after_reset);
    RUN(test_log_set_output_typed_file_writes);
    RUN(test_log_set_output_typed_streams_route_output);

    // Level filtering.
    RUN(test_log_level_filtering_suppresses_below_threshold);
    RUN(test_log_level_off_suppresses_everything);
    RUN(test_log_level_debug_emits_every_level);

    // Context.
    RUN(test_log_set_context);
    RUN(test_log_context_appended_to_output);
    RUN(test_log_context_multiple_pairs_preserve_order);
    RUN(test_log_set_context_updates_existing_key);
    RUN(test_log_clear_context_removes_pairs);

    // Output routing & reset.
    RUN(test_log_set_format);
    RUN(test_log_set_output_standard_streams_succeed);
    RUN(test_log_reset_restores_info_level);
    RUN(test_log_reset_clears_context_and_format);

    // Negative: argument types.
    RUN(test_log_debug_rejects_non_string);
    RUN(test_log_info_rejects_non_string);
    RUN(test_log_warn_rejects_non_string);
    RUN(test_log_error_rejects_non_string);
    RUN(test_log_set_format_rejects_non_string);
    RUN(test_log_set_context_rejects_non_string_key);
    RUN(test_log_set_context_rejects_non_string_value);
    RUN(test_log_set_level_rejects_invalid_type);
    RUN(test_log_set_output_rejects_non_string);

    // Negative: result failures.
    RUN(test_log_set_output_invalid_path_returns_failure);

    // Negative: arity.
    RUN(test_log_debug_rejects_wrong_arity);
    RUN(test_log_get_level_rejects_arguments);
    RUN(test_log_clear_context_rejects_arguments);
    RUN(test_log_set_context_rejects_wrong_arity);

    return SUMMARY();
}
