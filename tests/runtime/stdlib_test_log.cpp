// Standard library tests: Log.

#include <algorithm>
#include <filesystem>
#include <string>

#include "stdlib_test_helpers.hpp"

namespace {

// Route Log output to a private file, evaluate the Luma program, and return the
// file's contents.  The program is wrapped with a reset + redirect prologue and
// a reset epilogue: Log.reset() closes the file stream (flushing the bytes and
// making the file readable) and restores the process-global LogState to its
// defaults, so no configuration leaks into subsequent tests.  A timestamp-free
// format keeps the captured text deterministic for exact comparison.
[[nodiscard]] std::string capture_log_output(const std::string& program) {
    // Log.set_output opens the file in append mode, so a TempFile gives a clean
    // (truncated) slate up front and guarantees the file is removed on scope
    // exit — even if the eval below throws before cleanup would otherwise run.
    const TempFile log_file{std::filesystem::path{"_test_log_capture.log"}, ""};

    eval("Log.reset()\n"
         "Log.set_output(\"_test_log_capture.log\")\n" +
         program + "\nLog.reset()\n");

    // The log file stream writes in text mode, so on Windows newlines are
    // stored as CRLF.  Strip carriage returns so captured output compares
    // equal to '\n'-terminated expectations on every platform.
    std::string content = read_file_text(log_file.path());
    content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());

    return content;
}

// Reset the global log level, apply the given set expression, and read the
// level back.  Returns the resulting Log.Level choice value.
[[nodiscard]] Value level_roundtrip(const std::string& set_expr) {
    return eval("Log.reset()\n" + set_expr + "\nLog.get_level()");
}

} // namespace

// ─── Registration ───

static void test_log_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Log.debug"));
    ASSERT_TRUE(env->has("Log.info"));
    ASSERT_TRUE(env->has("Log.warn"));
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
    const auto env = luma::test::make_std_env(/*sandbox=*/true);

    ASSERT_FALSE(env->has("Log.set_output"));
    ASSERT_TRUE(env->has("Log.info"));
    ASSERT_TRUE(env->has("Log.set_level"));
}

// ─── Emission smoke tests (no throw) ───

static void test_log_debug() {
    eval("Log.debug(\"test debug\")");
}

static void test_log_info() {
    // Log.info just prints — no throw means success.
    eval("Log.info(\"test message\")");
}

static void test_log_warn() {
    eval("Log.warn(\"test warning\")");
}

static void test_log_error() {
    eval("Log.error(\"test error\")");
}

// ─── Level get/set ───

static void test_log_get_level() {
    const auto v = eval("Log.get_level()");

    ASSERT_TRUE(v.is_string() || v.is_choice());
}

static void test_log_get_level_returns_level_choice() {
    const auto v = eval("Log.reset()\nLog.get_level()");

    ASSERT_TRUE(v.is_choice());
    ASSERT_EQ(v.as_choice()->type_name, "Level");
    ASSERT_EQ(v.as_choice()->variant, "Info"); // reset() restores Info
}

static void test_log_set_level() {
    // Test with string argument (backward compatible).
    auto result = eval(R"(
        Log.set_level("warn")
        Log.get_level()
    )");

    ASSERT_TRUE(result.is_choice());
    ASSERT_EQ(result.as_choice()->type_name, "Level");
    ASSERT_EQ(result.as_choice()->variant, "Warn");

    // Test with choice argument.
    result = eval(R"(
        Log.set_level(Log.Level.Error)
        Log.get_level()
    )");

    ASSERT_TRUE(result.is_choice());
    ASSERT_EQ(result.as_choice()->variant, "Error");
}

static void test_log_set_level_all_choice_variants() {
    for (const auto* variant : {"Debug", "Info", "Warn", "Error", "Off"}) {
        const auto v = level_roundtrip(std::string{"Log.set_level(Log.Level."} + variant + ")");

        ASSERT_TRUE(v.is_choice());
        ASSERT_EQ(v.as_choice()->variant, std::string{variant});
    }
}

static void test_log_set_level_string_variants() {
    // The runtime also accepts lowercase string level names.
    const std::pair<const char*, const char*> cases[] = {
        {"debug", "Debug"}, {"info", "Info"}, {"warn", "Warn"}, {"error", "Error"}, {"off", "Off"},
    };

    for (const auto& [arg, expected] : cases) {
        const auto v = level_roundtrip(std::string{"Log.set_level(\""} + arg + "\")");

        ASSERT_TRUE(v.is_choice());
        ASSERT_EQ(v.as_choice()->variant, std::string{expected});
    }
}

static void test_log_set_level_unknown_string_defaults_to_info() {
    // An unrecognised level name falls back to Info rather than throwing.
    const auto v = level_roundtrip(R"(Log.set_level("verbose"))");

    ASSERT_TRUE(v.is_choice());
    ASSERT_EQ(v.as_choice()->variant, "Info");
}

// ─── Formatting ───

static void test_log_format_substitutes_level_and_message() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${level}::\${message}")
Log.set_level(Log.Level.Debug)
Log.info("hello world"))LUMA");

    ASSERT_EQ(out, "INFO::hello world\n");
}

static void test_log_format_substitutes_timestamp() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${timestamp}")
Log.set_level(Log.Level.Debug)
Log.info("ignored"))LUMA");

    // The ${timestamp} placeholder is replaced by an ISO-8601 UTC stamp.
    ASSERT_TRUE(out.find("${timestamp}") == std::string::npos);
    ASSERT_TRUE(out.find('T') != std::string::npos);
    ASSERT_TRUE(out.find('Z') != std::string::npos);
    ASSERT_FALSE(out.empty());
    ASSERT_EQ(out.back(), '\n');
}

static void test_log_format_preserves_unknown_placeholder() {
    // ${foo} is not a recognised field, so it is emitted verbatim.
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${foo}")
Log.set_level(Log.Level.Debug)
Log.info("ignored"))LUMA");

    ASSERT_EQ(out, "${foo}\n");
}

static void test_log_format_literal_prefix() {
    // A format with no placeholders is emitted as a literal prefix; the message
    // text is not appended unless ${message} appears.
    const auto out = capture_log_output(R"LUMA(Log.set_format("[APP] ")
Log.set_level(Log.Level.Debug)
Log.warn("dropped"))LUMA");

    ASSERT_EQ(out, "[APP] \n");
}

static void test_log_default_format_after_reset() {
    // With the default format restored by the capture prologue, an info line
    // carries the [LEVEL] tag, the message, and an ISO-8601 timestamp.
    const auto out = capture_log_output(R"LUMA(Log.info("ready"))LUMA");

    ASSERT_TRUE(out.find("[INFO] ready") != std::string::npos);
    ASSERT_TRUE(out.find('Z') != std::string::npos);
}

// ─── Level filtering ───

static void test_log_level_filtering_suppresses_below_threshold() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Error)
Log.debug("d")
Log.info("i")
Log.warn("w")
Log.error("kept"))LUMA");

    ASSERT_EQ(out, "kept\n");
}

static void test_log_level_off_suppresses_everything() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Off)
Log.debug("d")
Log.info("i")
Log.warn("w")
Log.error("e"))LUMA");

    ASSERT_EQ(out, "");
}

static void test_log_level_debug_emits_every_level() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Debug)
Log.debug("a")
Log.info("b")
Log.warn("c")
Log.error("d"))LUMA");

    ASSERT_EQ(out, "a\nb\nc\nd\n");
}

// ─── Context ───

static void test_log_set_context() {
    eval(R"(Log.set_context("key", "value"))");
}

static void test_log_context_appended_to_output() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Debug)
Log.set_context("k", "v")
Log.info("m"))LUMA");

    ASSERT_EQ(out, "m | k=v\n");
}

static void test_log_context_multiple_pairs_preserve_order() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Debug)
Log.set_context("k1", "v1")
Log.set_context("k2", "v2")
Log.info("m"))LUMA");

    ASSERT_EQ(out, "m | k1=v1 k2=v2\n");
}

static void test_log_set_context_updates_existing_key() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Debug)
Log.set_context("k", "v1")
Log.set_context("k", "v2")
Log.info("m"))LUMA");

    ASSERT_EQ(out, "m | k=v2\n");
}

static void test_log_clear_context_removes_pairs() {
    const auto out = capture_log_output(R"LUMA(Log.set_format("\${message}")
Log.set_level(Log.Level.Debug)
Log.set_context("k", "v")
Log.clear_context()
Log.info("m"))LUMA");

    ASSERT_EQ(out, "m\n");
}

// ─── Output routing & reset ───

static void test_log_set_format() {
    eval("Log.set_format(\"text\")");
}

static void test_log_set_output_standard_streams_succeed() {
    ASSERT_RESULT_SUCCESS(eval(R"(Log.set_output("stderr"))"));
    ASSERT_RESULT_SUCCESS(eval(R"(Log.set_output("stdout"))"));

    eval("Log.reset()");
}

static void test_log_reset_restores_info_level() {
    const auto v = eval(R"(
        Log.set_level(Log.Level.Error)
        Log.reset()
        Log.get_level()
    )");

    ASSERT_TRUE(v.is_choice());
    ASSERT_EQ(v.as_choice()->variant, "Info");
}

static void test_log_reset_clears_context_and_format() {
    // reset() in the prologue must have cleared the context and format set by a
    // prior program; logging here with the default format shows no trailing
    // context fields.
    eval(R"(
        Log.set_context("stale", "should_not_appear")
        Log.set_format("\${message}")
        Log.reset()
    )");

    const auto out = capture_log_output(R"LUMA(Log.info("fresh"))LUMA");

    ASSERT_TRUE(out.find("stale") == std::string::npos);
    ASSERT_TRUE(out.find("should_not_appear") == std::string::npos);
    ASSERT_TRUE(out.find("[INFO] fresh") != std::string::npos);
}

// ─── Negative tests: argument types ───

static void test_log_debug_rejects_non_string() {
    ASSERT_THROWS(eval("Log.debug(42)"));
}

static void test_log_info_rejects_non_string() {
    ASSERT_THROWS(eval("Log.info(true)"));
}

static void test_log_warn_rejects_non_string() {
    ASSERT_THROWS(eval("Log.warn(3.14)"));
}

static void test_log_error_rejects_non_string() {
    ASSERT_THROWS(eval("Log.error([1, 2])"));
}

static void test_log_set_format_rejects_non_string() {
    ASSERT_THROWS(eval("Log.set_format(99)"));
}

static void test_log_set_context_rejects_non_string_key() {
    ASSERT_THROWS_WITH_MESSAGE(eval(R"(Log.set_context(1, "v"))"), "expected string key and value");
}

static void test_log_set_context_rejects_non_string_value() {
    ASSERT_THROWS_WITH_MESSAGE(eval(R"(Log.set_context("k", 2))"), "expected string key and value");
}

static void test_log_set_level_rejects_invalid_type() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Log.set_level(42)"), "expected a LogLevel or string");
}

static void test_log_set_output_rejects_non_string() {
    ASSERT_THROWS(eval("Log.set_output(123)"));
}

// ─── Negative tests: result-returning failures ───

static void test_log_set_output_invalid_path_returns_failure() {
    const auto v = eval(R"(Log.set_output("/no/such/dir/sub/missing.log"))");

    ASSERT_RESULT_FAILURE(v);

    eval("Log.reset()"); // restore default output sink
}

// ─── Negative tests: arity ───

static void test_log_debug_rejects_wrong_arity() {
    ASSERT_THROWS(eval("Log.debug()"));
    ASSERT_THROWS(eval(R"(Log.debug("a", "b"))"));
}

static void test_log_get_level_rejects_arguments() {
    ASSERT_THROWS(eval(R"(Log.get_level("unexpected"))"));
}

static void test_log_clear_context_rejects_arguments() {
    ASSERT_THROWS(eval(R"(Log.clear_context("unexpected"))"));
}

static void test_log_set_context_rejects_wrong_arity() {
    ASSERT_THROWS(eval(R"(Log.set_context("only_key"))"));
}

int main() {
    // Registration.
    RUN(test_log_module);
    RUN(test_log_set_output_disabled_in_sandbox);

    // Emission smoke tests.
    RUN(test_log_debug);
    RUN(test_log_info);
    RUN(test_log_warn);
    RUN(test_log_error);

    // Level get/set.
    RUN(test_log_get_level);
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
