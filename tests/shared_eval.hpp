// Shared test pipeline evaluators.
//
// Provides eval() variants used across stdlib and integration tests.
// Each function runs the full Luma pipeline (lex → parse → compile → VM)
// and returns the resulting Value.

#ifndef LUMA_SHARED_EVAL_HPP
#define LUMA_SHARED_EVAL_HPP

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/linter/linter.hpp"
#include "analysis/types/type_checker.hpp"
#include "lex_parse_util.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/stdlib_registry.hpp"
#include "runtime/vm/vm.hpp"
#include "test_framework.hpp"

namespace luma::test {

// ─── Pipeline sub-helpers ─────────────────────────────────────────
//
// lex_and_parse_valid() and describe_diagnostics() live in lex_parse_util.hpp —
// a front-end-only header — so pure-analysis test TUs can reuse them without
// dragging in the backend below.

// Compile a parsed program in REPL mode, throwing (with the compiler's
// diagnostics) on failure.
[[nodiscard]] inline CompileResult compile_for_eval(const Program& program) {
    Compiler compiler;
    auto result = compiler.compile(program, /*repl_mode=*/true);

    if (!result.success) {
        throw std::runtime_error{"compilation failed:" + describe_diagnostics(result.diagnostics)};
    }

    return result;
}

// ─── Full-pipeline evaluators ─────────────────────────────────────

// Controls whether the analysis passes (type checking + linting) run.
enum class EvalMode {
    checked,  // full pipeline: type checking + linting
    unchecked // minimal pipeline: skip type checking and linting
};

// Shared, immutable standard-library prototype environment.
//
// register_all() builds every stdlib module namespace (each defining dozens of
// native functions), so doing it per eval() would throw away almost all of that
// work for a source that references a single module.  Instead we register the
// full stdlib exactly once per test process into a prototype scope, and layer a
// fresh child environment on top for each eval() (see eval_impl).
//
// Why this is safe to share across every eval():
//   * Every stdlib binding is immutable (registered via define(..., false)).
//     The VM's SetGlobal handler throws (runtime_error is [[noreturn]]) before
//     it could mutate an immutable binding, so no eval can ever write into the
//     prototype.
//   * Globals a test source defines land in the per-eval child scope —
//     Environment::define_or_assign() only touches the current scope, never a
//     parent — so evals stay isolated from one another exactly as before.
//   * The prototype's binding map is populated once and never mutated again,
//     giving it pointer stability and making concurrent read-only lookups
//     (e.g. from task-spawning concurrency tests, which deep-copy the chain
//     into independent environments) data-race free.
//
// make_std_env() below deliberately stays eager: the catalog-conformance and
// sandbox tests enumerate or own the returned environment directly.
[[nodiscard]] inline const EnvPtr& eval_std_prototype() {
    static const EnvPtr prototype = [] {
        auto env = Environment::create();
        register_all(env);
        return env;
    }();
    return prototype;
}

// Unified evaluator — runs the Luma pipeline with the given mode. A lexer or
// parser error always fails the evaluation (see lex_and_parse_valid).
[[nodiscard]] inline Value eval_impl(const std::string& source, EvalMode mode) {
    const auto program = lex_and_parse_valid(source);

    if (mode == EvalMode::checked) {
        TypeChecker checker;
        const auto errors = checker.check(program, /*require_main=*/false);

        if (!errors.empty()) {
            throw std::runtime_error{"Type check failed:" + describe_diagnostics(errors)};
        }

        Linter linter;
        [[maybe_unused]] const auto warnings = linter.lint(program);
    }

    const auto result = compile_for_eval(program);

    // Layer a fresh child scope over the shared stdlib prototype so this eval's
    // own globals stay isolated while the modules are registered only once.
    const auto env = Environment::create(eval_std_prototype());

    VM vm{env};

    return vm.execute_function(result.top_level, result.functions);
}

// Evaluate source through the full pipeline with type checking and linting.
// Used by integration tests that need to verify correctness of the entire chain.
[[nodiscard]] inline Value eval_checked(const std::string& source) {
    return eval_impl(source, EvalMode::checked);
}

// Evaluate source through a minimal pipeline (no type checking or linting).
// Used by stdlib unit tests that exercise individual functions. Because the
// type checker does not run, an error-path test must pin the expected message
// (ASSERT_EVAL_THROWS): a typo in the source surfaces as a runtime
// "undefined variable" error, not as a compile error.
[[nodiscard]] inline Value eval(const std::string& source) {
    return eval_impl(source, EvalMode::unchecked);
}

// ─── Environment helpers ──────────────────────────────────────────

// Create an environment with the full standard library registered.
[[nodiscard]] inline EnvPtr make_std_env() {
    const auto env = Environment::create();
    register_all(env, /*sandbox=*/false);
    return env;
}

// Create an environment with the standard library registered in sandbox mode
// (filesystem, network, and process access withheld).
[[nodiscard]] inline EnvPtr make_sandboxed_std_env() {
    const auto env = Environment::create();
    register_all(env, /*sandbox=*/true);
    return env;
}

// ─── Value assertion helpers ──────────────────────────────────────

// Check whether a Value holds a successful result.
[[nodiscard]] inline bool is_success_result(const Value& v) {
    return v.is_result() && v.as_result()->is_success;
}

// Fail unless `value` is a result with the `expected_outcome` ("success" or
// "failure").
inline void assert_result_outcome(const Value& value, std::string_view macro_name,
                                  std::string_view expected_outcome,
                                  std::string_view expression_text, const AssertionSite& site) {
    if (!value.is_result()) {
        fail_at(site, std::string{macro_name} + " failed: " + std::string{expression_text} +
                          " is not a result: " + value.to_string());
    }

    const bool is_success = value.as_result()->is_success;
    const std::string_view actual_outcome = is_success ? "success" : "failure";

    if (actual_outcome != expected_outcome) {
        fail_at(site, std::string{macro_name} + " failed: " + std::string{expression_text} +
                          " is " + value.to_string());
    }
}

// Evaluate `source` and return the inner value of its successful result,
// failing the assertion when the program yields anything else.
[[nodiscard]] inline Value eval_success_inner(const std::string& source,
                                              std::string_view macro_name,
                                              const AssertionSite& site) {
    const auto value = eval(source);
    assert_result_outcome(value, macro_name, "success", source, site);
    return *value.as_result()->owned_inner;
}

} // namespace luma::test

// ─── Value assertion macros ──────────────────────────────────────

// Assert that a value is a successful result.
#define ASSERT_RESULT_SUCCESS(value)                                                               \
    luma::test::assert_result_outcome((value), "ASSERT_RESULT_SUCCESS", "success", #value,         \
                                      LUMA_ASSERTION_SITE)

// Assert that a value is a failure result.
#define ASSERT_RESULT_FAILURE(value)                                                               \
    luma::test::assert_result_outcome((value), "ASSERT_RESULT_FAILURE", "failure", #value,         \
                                      LUMA_ASSERTION_SITE)

// Assert that evaluating source code yields a successful result whose
// inner value equals the expected integer.
#define ASSERT_EVAL_INT(source, expected)                                                          \
    luma::test::assert_comparison<luma::test::Comparison::equal>(                                  \
        luma::test::eval_success_inner((source), "ASSERT_EVAL_INT", LUMA_ASSERTION_SITE)           \
            .as_integer(),                                                                         \
        (expected), "ASSERT_EVAL_INT", LUMA_ASSERTION_SITE)

// Assert that evaluating source code yields a successful result whose
// inner value equals the expected string.
#define ASSERT_EVAL_STR(source, expected)                                                          \
    luma::test::assert_comparison<luma::test::Comparison::equal>(                                  \
        luma::test::eval_success_inner((source), "ASSERT_EVAL_STR", LUMA_ASSERTION_SITE)           \
            .as_string(),                                                                          \
        std::string{expected}, "ASSERT_EVAL_STR", LUMA_ASSERTION_SITE)

// Assert that evaluating source code yields a successful result whose
// inner value equals the expected boolean.
#define ASSERT_EVAL_BOOL(source, expected)                                                         \
    luma::test::assert_comparison<luma::test::Comparison::equal>(                                  \
        luma::test::eval_success_inner((source), "ASSERT_EVAL_BOOL", LUMA_ASSERTION_SITE)          \
            .as_bool(),                                                                            \
        (expected), "ASSERT_EVAL_BOOL", LUMA_ASSERTION_SITE)

// Assert that evaluating source code yields a successful result whose
// inner value equals the expected double (exact comparison).
#define ASSERT_EVAL_NUM(source, expected)                                                          \
    luma::test::assert_comparison<luma::test::Comparison::equal>(                                  \
        luma::test::eval_success_inner((source), "ASSERT_EVAL_NUM", LUMA_ASSERTION_SITE)           \
            .as_number(),                                                                          \
        (expected), "ASSERT_EVAL_NUM", LUMA_ASSERTION_SITE)

// Assert that evaluating source code yields a failure result.
#define ASSERT_EVAL_FAILURE(source) ASSERT_RESULT_FAILURE(luma::test::eval(source))

// Assert that evaluating source code raises an error whose message contains
// `expected_substr`.
#define ASSERT_EVAL_THROWS(source, expected_substr)                                                \
    ASSERT_THROWS_WITH_MESSAGE(luma::test::eval(source), expected_substr)

#endif // LUMA_SHARED_EVAL_HPP
