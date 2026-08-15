// CLI unit tests — exit codes, levenshtein distance, flag suggestions.

#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/pipeline/pipeline.hpp"
#include "runtime/cli/cli.hpp"
#include "runtime/cli/cli_args.hpp"
#include "runtime/cli/cli_commands.hpp"
#include "runtime/compiler/compilation_cache.hpp"
#include "test_framework.hpp"

using namespace luma;

namespace {
// Builds a mutable argv array (with a dummy argv[0] program name) so parse_args
// can be exercised directly. parse_args takes char* argv[], so the backing
// strings must be non-const and must outlive the call.
class ArgvBuilder {
public:
    ArgvBuilder(std::initializer_list<std::string> args) : storage_{args} {
        pointers_.reserve(storage_.size() + 1);

        for (auto& arg : storage_) {
            pointers_.push_back(arg.data());
        }

        pointers_.push_back(nullptr);
    }

    ArgvBuilder(const ArgvBuilder&) = delete;
    ArgvBuilder& operator=(const ArgvBuilder&) = delete;
    ArgvBuilder(ArgvBuilder&&) = delete;
    ArgvBuilder& operator=(ArgvBuilder&&) = delete;

    [[nodiscard]] int argc() const {
        return static_cast<int>(storage_.size());
    }

    [[nodiscard]] char** argv() {
        return pointers_.data();
    }

private:
    std::vector<std::string> storage_;
    std::vector<char*> pointers_;
};

// RAII guard that switches the process working directory to a fresh temp dir and
// restores the original on destruction. The temp dir itself is owned by a
// TempDir member, which removes it after the working directory is restored. Used
// by the `pkg init` tests, which write luma.json into the current directory.
class ScopedTempCwd {
public:
    ScopedTempCwd() : previous_{std::filesystem::current_path()} {
        std::filesystem::current_path(dir_.path());
    }

    ~ScopedTempCwd() noexcept {
        std::error_code ec;
        std::filesystem::current_path(previous_, ec);
    }

    ScopedTempCwd(const ScopedTempCwd&) = delete;
    ScopedTempCwd& operator=(const ScopedTempCwd&) = delete;
    ScopedTempCwd(ScopedTempCwd&&) = delete;
    ScopedTempCwd& operator=(ScopedTempCwd&&) = delete;

private:
    std::filesystem::path previous_;
    TempDir dir_;
};

// Parse the given argv, failing the test (via exception) if parsing returns
// nullopt. Used by the positive parse_args tests that expect success.
[[nodiscard]] ParsedArgs parse_or_fail(ArgvBuilder& args) {
    auto parsed = parse_args(args.argc(), args.argv());

    if (!parsed) {
        throw std::runtime_error{"parse_args unexpectedly returned nullopt"};
    }

    return *parsed;
}
} // namespace

// ─── Exit code constant tests ───

static void test_exit_code_values() {
    ASSERT_EQ(exit_code::success, 0);
    ASSERT_EQ(exit_code::runtime_error, 1);
    ASSERT_EQ(exit_code::type_error, 2);
    ASSERT_EQ(exit_code::syntax_error, 3);
    ASSERT_EQ(exit_code::compile_error, 4);
    ASSERT_EQ(exit_code::usage_error, 5);
}

// ─── from_diagnostic_category mapping tests ───

static void test_from_diagnostic_category_runtime() {
    ASSERT_EQ(exit_code::from_diagnostic_category(DiagnosticCategory::Runtime),
              exit_code::runtime_error);
}

static void test_from_diagnostic_category_type() {
    ASSERT_EQ(exit_code::from_diagnostic_category(DiagnosticCategory::Type), exit_code::type_error);
}

static void test_from_diagnostic_category_syntax() {
    ASSERT_EQ(exit_code::from_diagnostic_category(DiagnosticCategory::Syntax),
              exit_code::syntax_error);
}

static void test_from_diagnostic_category_compile() {
    ASSERT_EQ(exit_code::from_diagnostic_category(DiagnosticCategory::Compile),
              exit_code::compile_error);
}

static void test_from_diagnostic_category_warning() {
    ASSERT_EQ(exit_code::from_diagnostic_category(DiagnosticCategory::Warning), exit_code::success);
}

// ─── Levenshtein distance tests ───

static void test_levenshtein_identical() {
    ASSERT_EQ(levenshtein_distance("hello", "hello"), 0U);
}

static void test_levenshtein_empty_strings() {
    ASSERT_EQ(levenshtein_distance("", ""), 0U);
}

static void test_levenshtein_one_empty() {
    ASSERT_EQ(levenshtein_distance("abc", ""), 3U);
    ASSERT_EQ(levenshtein_distance("", "xyz"), 3U);
}

static void test_levenshtein_single_insertion() {
    ASSERT_EQ(levenshtein_distance("--hep", "--help"), 1U);
}

static void test_levenshtein_single_deletion() {
    ASSERT_EQ(levenshtein_distance("--helpp", "--help"), 1U);
}

static void test_levenshtein_single_substitution() {
    ASSERT_EQ(levenshtein_distance("--hepp", "--help"), 1U);
}

static void test_levenshtein_two_edits() {
    ASSERT_EQ(levenshtein_distance("--hpel", "--help"), 2U);
}

static void test_levenshtein_completely_different() {
    const auto dist = levenshtein_distance("--help", "--version");

    ASSERT_TRUE(dist > 2);
}

// ─── suggest_flag tests ───

static void test_suggest_flag_exact_match() {
    // Distance 0 — exact match is within threshold.
    ASSERT_EQ(suggest_flag("--help"), "--help");
}

static void test_suggest_flag_close_typo() {
    ASSERT_EQ(suggest_flag("--helf"), "--help");
}

static void test_suggest_flag_short_typo() {
    ASSERT_EQ(suggest_flag("-hp"), "-h");
}

static void test_suggest_flag_version_typo() {
    ASSERT_EQ(suggest_flag("--versin"), "--version");
}

static void test_suggest_flag_test_typo() {
    ASSERT_EQ(suggest_flag("--tets"), "--test");
}

static void test_suggest_flag_no_match() {
    // Completely unrelated — should return empty.
    ASSERT_TRUE(suggest_flag("--xyzzy").empty());
}

static void test_suggest_flag_box_typo() {
    ASSERT_EQ(suggest_flag("--bx"), "--box");
}

static void test_suggest_flag_strict_typo() {
    ASSERT_EQ(suggest_flag("--strct"), "--strict");
}

// ─── run_file / check_file / run_tests_file tests ───

static void test_run_file_nonexistent() {
    const int code = run_file("__nonexistent__.luma", RunOptions{});

    ASSERT_TRUE(code != exit_code::success);
}

static void test_check_file_nonexistent() {
    const int code = check_file("__nonexistent__.luma");

    ASSERT_TRUE(code != exit_code::success);
}

static void test_run_tests_file_nonexistent() {
    const int code = run_tests_file("__nonexistent__.luma");

    ASSERT_TRUE(code != exit_code::success);
}

static void test_run_tests_file_passing() {
    // A file whose @test functions all pass exits successfully.
    const TempFile file{"@test\n"
                        "function void test_arithmetic() {\n"
                        "    assert(2 + 2 == 4)\n"
                        "}\n"};

    const int code = run_tests_file(file.path_string());

    ASSERT_EQ(code, exit_code::success);
}

static void test_run_tests_file_failing() {
    // A failing assertion inside a @test must surface as a runtime error so
    // the CLI exit code is non-zero (CI can detect the failure).
    const TempFile file{"@test\n"
                        "function void test_wrong() {\n"
                        "    assert(1 == 2, \"one is not two\")\n"
                        "}\n"};

    const int code = run_tests_file(file.path_string());

    ASSERT_EQ(code, exit_code::runtime_error);
}

static void test_run_tests_file_no_tests() {
    // A file with no @test functions is not an error: the runner reports
    // "no tests found" and exits successfully.
    const TempFile file{"function integer _helper() {\n"
                        "    return 42\n"
                        "}\n"};

    const int code = run_tests_file(file.path_string());

    ASSERT_EQ(code, exit_code::success);
}

static void test_run_file_valid_program() {
    const int code = run_file("examples/language-features/hello.luma", RunOptions{});

    ASSERT_EQ(code, exit_code::success);
}

static void test_check_file_valid_program() {
    const int code = check_file("examples/language-features/hello.luma");

    ASSERT_EQ(code, exit_code::success);
}

static void test_run_file_with_strict() {
    const int code = run_file("examples/language-features/hello.luma", RunOptions{.strict = true});

    ASSERT_EQ(code, exit_code::success);
}

static void test_run_file_with_sandbox() {
    const int code = run_file("examples/language-features/hello.luma", RunOptions{.sandbox = true});

    ASSERT_EQ(code, exit_code::success);
}

// ─── Version and extension constants ───

static void test_version_string() {
    ASSERT_FALSE(luma_version.empty());
    ASSERT_TRUE(luma_version.find('.') != std::string_view::npos);
}

static void test_extension_constant() {
    ASSERT_EQ(luma_extension, ".luma");
}

// ─── Levenshtein edge cases ───

static void test_levenshtein_symmetric() {
    ASSERT_EQ(levenshtein_distance("abc", "def"), levenshtein_distance("def", "abc"));
}

static void test_levenshtein_single_char() {
    ASSERT_EQ(levenshtein_distance("a", "b"), 1U);
    ASSERT_EQ(levenshtein_distance("a", "a"), 0U);
}

// ─── suggest_flag edge cases ───

static void test_suggest_flag_check_typo() {
    ASSERT_EQ(suggest_flag("--chek"), "--check");
}

static void test_suggest_flag_no_match_long() {
    ASSERT_TRUE(suggest_flag("--foobar").empty());
}

// ─── RunOptions-based run_file tests ───

static void test_run_file_with_optimize_zero() {
    RunOptions opts;
    opts.optimize_level = OptimizationLevel::None;
    const int code = run_file("examples/language-features/hello.luma", opts);

    ASSERT_EQ(code, exit_code::success);
}

static void test_run_file_with_optimize_two() {
    RunOptions opts;
    opts.optimize_level = OptimizationLevel::Full;
    const int code = run_file("examples/language-features/hello.luma", opts);

    ASSERT_EQ(code, exit_code::success);
}

static void test_run_file_with_strict_and_sandbox() {
    RunOptions opts;
    opts.strict = true;
    opts.sandbox = true;
    const int code = run_file("examples/language-features/hello.luma", opts);

    ASSERT_EQ(code, exit_code::success);
}

// ─── Flag suggestion for new flags ───

static void test_suggest_flag_optimize_typo() {
    ASSERT_EQ(suggest_flag("--optimze"), "--optimize");
}

static void test_suggest_flag_verify_typo() {
    ASSERT_EQ(suggest_flag("--veify"), "--verify");
}

// ─── --strict: warnings-as-errors behaviour ───
//
// The core contract of --strict is that lint/type warnings become fatal:
// a program that compiles and runs cleanly without --strict must instead
// exit with a type error when --strict is set. The clean-code path (no
// warnings still passes under --strict) is covered by
// test_run_file_with_strict and, end-to-end, by every Luma feature test
// (each runs with `--strict --test`). These tests close the failure path.

// A small program that is valid and runnable but carries a W0001 unused-
// variable warning — the minimal trigger for strict-mode escalation.
static constexpr std::string_view k_program_with_warning = "@main\n"
                                                           "function void main() {\n"
                                                           "    integer unused = 42\n"
                                                           "    print(\"hi\")\n"
                                                           "}\n";

static void test_check_strict_warnings_strict_with_warning() {
    PipelineResult result;
    Diagnostic warn;
    warn.severity = Severity::Warning;
    warn.message = "synthetic warning";
    result.diagnostics.push_back(std::move(warn));

    ASSERT_EQ(check_strict_warnings(result, /*strict=*/true), exit_code::type_error);
}

static void test_check_strict_warnings_strict_without_warning() {
    const PipelineResult result;

    ASSERT_EQ(check_strict_warnings(result, /*strict=*/true), exit_code::success);
}

static void test_check_strict_warnings_nonstrict_with_warning() {
    PipelineResult result;
    Diagnostic warn;
    warn.severity = Severity::Warning;
    warn.message = "synthetic warning";
    result.diagnostics.push_back(std::move(warn));

    ASSERT_EQ(check_strict_warnings(result, /*strict=*/false), exit_code::success);
}

static void test_check_file_strict_fails_on_warning() {
    const TempFile file{std::string{k_program_with_warning}};

    ASSERT_EQ(check_file(file.path_string(), /*strict=*/true), exit_code::type_error);
}

static void test_check_file_nonstrict_succeeds_on_warning() {
    const TempFile file{std::string{k_program_with_warning}};

    ASSERT_EQ(check_file(file.path_string(), /*strict=*/false), exit_code::success);
}

static void test_run_file_strict_fails_on_warning() {
    const TempFile file{std::string{k_program_with_warning}};

    ASSERT_EQ(run_file(file.path_string(), RunOptions{.strict = true}), exit_code::type_error);
}

static void test_run_file_nonstrict_succeeds_on_warning() {
    const TempFile file{std::string{k_program_with_warning}};

    ASSERT_EQ(run_file(file.path_string(), RunOptions{.strict = false}), exit_code::success);
}

static void test_run_file_strict_bypasses_warm_cache() {
    // Regression: a warm compilation cache must not let a --strict run skip the
    // warning re-check.  Cached artifacts record no warnings, so serving one
    // would let a program that should fail (warnings-as-errors) exit 0.
    const TempFile file{std::string{k_program_with_warning}};

    CompilationCache cache;

    // A non-strict run succeeds and populates the cache.
    ASSERT_EQ(run_file(file.path_string(), RunOptions{.strict = false}, cache), exit_code::success);

    // The subsequent --strict run must re-check warnings rather than be served
    // the warning-free cached artifact.
    ASSERT_EQ(run_file(file.path_string(), RunOptions{.strict = true}, cache),
              exit_code::type_error);
}

static void test_run_tests_file_strict_fails_on_warning() {
    // A functionally-correct @test file that still trips a lint warning must
    // fail under --strict before any test body executes.
    const TempFile file{"@test\n"
                        "function void test_ok() {\n"
                        "    integer unused = 42\n"
                        "    assert(true)\n"
                        "}\n"};

    ASSERT_EQ(run_tests_file(file.path_string(), /*strict=*/true), exit_code::type_error);
}

static void test_run_tests_file_nonstrict_succeeds_on_warning() {
    const TempFile file{"@test\n"
                        "function void test_ok() {\n"
                        "    integer unused = 42\n"
                        "    assert(true)\n"
                        "}\n"};

    ASSERT_EQ(run_tests_file(file.path_string(), /*strict=*/false), exit_code::success);
}

// ─── parse_args: commands, flags, and positional handling (positive) ───

static void test_parse_args_no_args_defaults_to_run() {
    ArgvBuilder args{"luma"};

    const auto parsed = parse_or_fail(args);

    ASSERT_EQ(parsed.command, Command::Run);
    ASSERT_TRUE(parsed.file_path.empty());
    ASSERT_TRUE(parsed.program_args.empty());
}

static void test_parse_args_file_path() {
    ArgvBuilder args{"luma", "program.luma"};

    const auto parsed = parse_or_fail(args);

    ASSERT_EQ(parsed.command, Command::Run);
    ASSERT_EQ(parsed.file_path, "program.luma");
    ASSERT_TRUE(parsed.program_args.empty());
}

static void test_parse_args_program_args_collected_after_file() {
    // Everything after the file path belongs to the program, even tokens that
    // would otherwise look like luma flags.
    ArgvBuilder args{"luma", "program.luma", "alpha", "--strict", "7"};

    const auto parsed = parse_or_fail(args);

    ASSERT_EQ(parsed.file_path, "program.luma");
    ASSERT_FALSE(parsed.strict);
    ASSERT_EQ(parsed.program_args.size(), 3U);
    ASSERT_EQ(parsed.program_args[0], "alpha");
    ASSERT_EQ(parsed.program_args[1], "--strict");
    ASSERT_EQ(parsed.program_args[2], "7");
}

static void test_parse_args_help_long_and_short() {
    ArgvBuilder long_form{"luma", "--help"};
    ASSERT_EQ(parse_or_fail(long_form).command, Command::Help);

    ArgvBuilder short_form{"luma", "-h"};
    ASSERT_EQ(parse_or_fail(short_form).command, Command::Help);
}

static void test_parse_args_version_long_and_short() {
    ArgvBuilder long_form{"luma", "--version"};
    ASSERT_EQ(parse_or_fail(long_form).command, Command::Version);

    ArgvBuilder short_form{"luma", "-v"};
    ASSERT_EQ(parse_or_fail(short_form).command, Command::Version);
}

static void test_parse_args_repl_long_and_short() {
    ArgvBuilder long_form{"luma", "--repl"};
    ASSERT_EQ(parse_or_fail(long_form).command, Command::Repl);

    ArgvBuilder short_form{"luma", "-r"};
    ASSERT_EQ(parse_or_fail(short_form).command, Command::Repl);
}

static void test_parse_args_eval_long_and_short() {
    ArgvBuilder long_form{"luma", "--eval"};
    ASSERT_EQ(parse_or_fail(long_form).command, Command::Eval);

    ArgvBuilder short_form{"luma", "-e"};
    ASSERT_EQ(parse_or_fail(short_form).command, Command::Eval);
}

static void test_parse_args_test_command() {
    ArgvBuilder args{"luma", "--test", "suite.luma"};

    const auto parsed = parse_or_fail(args);

    ASSERT_EQ(parsed.command, Command::Test);
    ASSERT_EQ(parsed.file_path, "suite.luma");
}

static void test_parse_args_check_command() {
    ArgvBuilder args{"luma", "--check", "suite.luma"};

    const auto parsed = parse_or_fail(args);

    ASSERT_EQ(parsed.command, Command::Check);
    ASSERT_EQ(parsed.file_path, "suite.luma");
}

static void test_parse_args_pkg_subcommand_collects_args() {
    ArgvBuilder args{"luma", "pkg", "init", "--whatever"};

    const auto parsed = parse_or_fail(args);

    ASSERT_EQ(parsed.command, Command::Pkg);
    ASSERT_EQ(parsed.program_args.size(), 2U);
    ASSERT_EQ(parsed.program_args[0], "init");
    ASSERT_EQ(parsed.program_args[1], "--whatever");
}

static void test_parse_args_boolean_flags_long() {
    ArgvBuilder args{"luma", "--strict", "--box", "--verify", "program.luma"};

    const auto parsed = parse_or_fail(args);

    ASSERT_TRUE(parsed.strict);
    ASSERT_TRUE(parsed.sandbox);
    ASSERT_TRUE(parsed.verify);
    ASSERT_EQ(parsed.command, Command::Run);
    ASSERT_EQ(parsed.file_path, "program.luma");
}

static void test_parse_args_boolean_flags_short() {
    ArgvBuilder args{"luma", "-s", "-b", "program.luma"};

    const auto parsed = parse_or_fail(args);

    ASSERT_TRUE(parsed.strict);
    ASSERT_TRUE(parsed.sandbox);
}

static void test_parse_args_optimize_default_is_one() {
    ArgvBuilder args{"luma", "program.luma"};

    ASSERT_EQ(parse_or_fail(args).optimize, 1);
}

static void test_parse_args_optimize_combined() {
    ArgvBuilder zero{"luma", "-O0", "program.luma"};
    ASSERT_EQ(parse_or_fail(zero).optimize, 0);

    ArgvBuilder one{"luma", "-O1", "program.luma"};
    ASSERT_EQ(parse_or_fail(one).optimize, 1);

    ArgvBuilder two{"luma", "-O2", "program.luma"};
    ASSERT_EQ(parse_or_fail(two).optimize, 2);
}

static void test_parse_args_optimize_separated() {
    ArgvBuilder short_sep{"luma", "-O", "2", "program.luma"};
    const auto parsed = parse_or_fail(short_sep);
    ASSERT_EQ(parsed.optimize, 2);
    ASSERT_EQ(parsed.file_path, "program.luma");

    ArgvBuilder long_sep{"luma", "--optimize", "0", "program.luma"};
    ASSERT_EQ(parse_or_fail(long_sep).optimize, 0);
}

// ─── parse_args: error and warning paths (negative) ───

static void test_parse_args_unknown_long_flag_returns_nullopt() {
    ArgvBuilder args{"luma", "--bogus"};

    const CapturedStreams captured{std::cout, std::cerr};

    ASSERT_FALSE(parse_args(args.argc(), args.argv()).has_value());
}

static void test_parse_args_unknown_short_flag_returns_nullopt() {
    ArgvBuilder args{"luma", "-z"};

    const CapturedStreams captured{std::cout, std::cerr};

    ASSERT_FALSE(parse_args(args.argc(), args.argv()).has_value());
}

static void test_parse_args_unknown_flag_emits_suggestion() {
    // A close typo should be rejected and produce a "did you mean" suggestion.
    ArgvBuilder args{"luma", "--helf"};

    const CapturedStreams captured{std::cout, std::cerr};
    const auto parsed = parse_args(args.argc(), args.argv());
    const std::string output = captured.str();

    ASSERT_FALSE(parsed.has_value());
    ASSERT_TRUE(output.find("did you mean") != std::string::npos);
    ASSERT_TRUE(output.find("--help") != std::string::npos);
}

static void test_parse_args_combined_optimize_invalid_returns_nullopt() {
    // -O5 is not a valid combined optimize form, so it is an unknown option.
    ArgvBuilder args{"luma", "-O5"};

    const CapturedStreams captured{std::cout, std::cerr};

    ASSERT_FALSE(parse_args(args.argc(), args.argv()).has_value());
}

static void test_parse_args_optimize_separated_invalid_warns() {
    // An out-of-range separated level is ignored with a warning; parsing still
    // succeeds and the optimization level stays at its default.
    ArgvBuilder args{"luma", "-O", "9"};

    const CapturedStreams captured{std::cout, std::cerr};
    const auto parsed = parse_args(args.argc(), args.argv());
    const std::string output = captured.str();

    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->optimize, 1);
    ASSERT_TRUE(output.find("invalid optimization level") != std::string::npos);
}

static void test_parse_args_optimize_missing_value_warns() {
    ArgvBuilder args{"luma", "--optimize"};

    const CapturedStreams captured{std::cout, std::cerr};
    const auto parsed = parse_args(args.argc(), args.argv());
    const std::string output = captured.str();

    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->optimize, 1);
    ASSERT_TRUE(output.find("requires a level") != std::string::npos);
}

// ─── is_known_flag ───

static void test_is_known_flag_long_forms() {
    ASSERT_TRUE(is_known_flag("--help"));
    ASSERT_TRUE(is_known_flag("--optimize"));
    ASSERT_TRUE(is_known_flag("--verify"));
}

static void test_is_known_flag_short_forms() {
    ASSERT_TRUE(is_known_flag("-h"));
    ASSERT_TRUE(is_known_flag("-O"));
    ASSERT_TRUE(is_known_flag("-s"));
}

static void test_is_known_flag_unknown() {
    ASSERT_FALSE(is_known_flag("--bogus"));
    ASSERT_FALSE(is_known_flag("-z"));
    // The empty string must not match --verify's empty short form.
    ASSERT_FALSE(is_known_flag(""));
}

// ─── ParsedArgs / RunOptions conversions ───

static void test_parsed_args_to_run_options_conversion() {
    ParsedArgs args;
    args.strict = true;
    args.sandbox = true;
    args.verify = true;
    args.optimize = 0;
    args.program_args = {"one", "two"};

    const RunOptions options = args.to_run_options();

    ASSERT_TRUE(options.strict);
    ASSERT_TRUE(options.sandbox);
    ASSERT_TRUE(options.verify);
    ASSERT_EQ(options.optimize_level, OptimizationLevel::None);
    ASSERT_EQ(options.args.size(), 2U);
    ASSERT_EQ(options.args[0], "one");
    ASSERT_EQ(options.args[1], "two");
}

static void test_parsed_args_to_run_options_optimize_full() {
    ParsedArgs args;
    args.optimize = 2;

    ASSERT_EQ(args.to_run_options().optimize_level, OptimizationLevel::Full);
}

static void test_run_options_to_compiler_profile_conversion() {
    RunOptions options;
    options.strict = true;
    options.verify = true;
    options.optimize_level = OptimizationLevel::Peephole;

    const CompilerProfile profile = options.to_compiler_profile();

    ASSERT_TRUE(profile.strict);
    ASSERT_TRUE(profile.verify);
    ASSERT_EQ(profile.optimize_level, OptimizationLevel::Peephole);
}

// ─── run_pkg_command (luma init) ───

static void test_run_pkg_init_creates_manifest() {
    const ScopedTempCwd temp_cwd;

    ParsedArgs args;
    args.command = Command::Pkg;

    const CapturedStreams captured{std::cout, std::cerr};
    const int code = run_pkg_command(args);

    ASSERT_EQ(code, exit_code::success);
    ASSERT_TRUE(std::filesystem::exists("luma.json"));
}

static void test_run_pkg_init_fails_when_manifest_exists() {
    const ScopedTempCwd temp_cwd;

    const TempFile manifest{"luma.json", "{}"};

    ParsedArgs args;
    args.command = Command::Pkg;

    const CapturedStreams captured{std::cout, std::cerr};

    ASSERT_EQ(run_pkg_command(args), exit_code::usage_error);
}

static void test_run_pkg_help_succeeds() {
    const CapturedStreams captured{std::cout, std::cerr};

    ParsedArgs explicit_help;
    explicit_help.command = Command::Pkg;
    explicit_help.program_args = {"help"};
    ASSERT_EQ(run_pkg_command(explicit_help), exit_code::success);
}

int main() {
    // Exit code constants.
    RUN(test_exit_code_values);

    // from_diagnostic_category mapping.
    RUN(test_from_diagnostic_category_runtime);
    RUN(test_from_diagnostic_category_type);
    RUN(test_from_diagnostic_category_syntax);
    RUN(test_from_diagnostic_category_compile);
    RUN(test_from_diagnostic_category_warning);

    // Levenshtein distance.
    RUN(test_levenshtein_identical);
    RUN(test_levenshtein_empty_strings);
    RUN(test_levenshtein_one_empty);
    RUN(test_levenshtein_single_insertion);
    RUN(test_levenshtein_single_deletion);
    RUN(test_levenshtein_single_substitution);
    RUN(test_levenshtein_two_edits);
    RUN(test_levenshtein_completely_different);

    // Flag suggestions.
    RUN(test_suggest_flag_exact_match);
    RUN(test_suggest_flag_close_typo);
    RUN(test_suggest_flag_short_typo);
    RUN(test_suggest_flag_version_typo);
    RUN(test_suggest_flag_test_typo);
    RUN(test_suggest_flag_no_match);
    RUN(test_suggest_flag_box_typo);
    RUN(test_suggest_flag_strict_typo);

    // run_file / check_file / run_tests_file.
    RUN(test_run_file_nonexistent);
    RUN(test_check_file_nonexistent);
    RUN(test_run_tests_file_nonexistent);
    RUN(test_run_tests_file_passing);
    RUN(test_run_tests_file_failing);
    RUN(test_run_tests_file_no_tests);
    RUN(test_run_file_valid_program);
    RUN(test_check_file_valid_program);
    RUN(test_run_file_with_strict);
    RUN(test_run_file_with_sandbox);

    // Constants.
    RUN(test_version_string);
    RUN(test_extension_constant);

    // Levenshtein edge cases.
    RUN(test_levenshtein_symmetric);
    RUN(test_levenshtein_single_char);

    // suggest_flag edge cases.
    RUN(test_suggest_flag_check_typo);
    RUN(test_suggest_flag_no_match_long);

    // RunOptions-based tests.
    RUN(test_run_file_with_optimize_zero);
    RUN(test_run_file_with_optimize_two);
    RUN(test_run_file_with_strict_and_sandbox);

    // Flag suggestion for new flags.
    RUN(test_suggest_flag_optimize_typo);
    RUN(test_suggest_flag_verify_typo);

    // --strict: warnings-as-errors behaviour.
    RUN(test_check_strict_warnings_strict_with_warning);
    RUN(test_check_strict_warnings_strict_without_warning);
    RUN(test_check_strict_warnings_nonstrict_with_warning);
    RUN(test_check_file_strict_fails_on_warning);
    RUN(test_check_file_nonstrict_succeeds_on_warning);
    RUN(test_run_file_strict_fails_on_warning);
    RUN(test_run_file_nonstrict_succeeds_on_warning);
    RUN(test_run_file_strict_bypasses_warm_cache);
    RUN(test_run_tests_file_strict_fails_on_warning);
    RUN(test_run_tests_file_nonstrict_succeeds_on_warning);

    // parse_args — commands, flags, positionals (positive).
    RUN(test_parse_args_no_args_defaults_to_run);
    RUN(test_parse_args_file_path);
    RUN(test_parse_args_program_args_collected_after_file);
    RUN(test_parse_args_help_long_and_short);
    RUN(test_parse_args_version_long_and_short);
    RUN(test_parse_args_repl_long_and_short);
    RUN(test_parse_args_eval_long_and_short);
    RUN(test_parse_args_test_command);
    RUN(test_parse_args_check_command);
    RUN(test_parse_args_pkg_subcommand_collects_args);
    RUN(test_parse_args_boolean_flags_long);
    RUN(test_parse_args_boolean_flags_short);
    RUN(test_parse_args_optimize_default_is_one);
    RUN(test_parse_args_optimize_combined);
    RUN(test_parse_args_optimize_separated);

    // parse_args — error and warning paths (negative).
    RUN(test_parse_args_unknown_long_flag_returns_nullopt);
    RUN(test_parse_args_unknown_short_flag_returns_nullopt);
    RUN(test_parse_args_unknown_flag_emits_suggestion);
    RUN(test_parse_args_combined_optimize_invalid_returns_nullopt);
    RUN(test_parse_args_optimize_separated_invalid_warns);
    RUN(test_parse_args_optimize_missing_value_warns);

    // is_known_flag.
    RUN(test_is_known_flag_long_forms);
    RUN(test_is_known_flag_short_forms);
    RUN(test_is_known_flag_unknown);

    // ParsedArgs / RunOptions conversions.
    RUN(test_parsed_args_to_run_options_conversion);
    RUN(test_parsed_args_to_run_options_optimize_full);
    RUN(test_run_options_to_compiler_profile_conversion);

    // run_pkg_command (luma init).
    RUN(test_run_pkg_init_creates_manifest);
    RUN(test_run_pkg_init_fails_when_manifest_exists);
    RUN(test_run_pkg_help_succeeds);

    return SUMMARY();
}
