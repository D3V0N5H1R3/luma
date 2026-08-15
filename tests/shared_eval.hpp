// Shared test pipeline evaluators.
//
// Provides eval() variants used across stdlib and integration tests.
// Each function runs the full Luma pipeline (lex → parse → compile → VM)
// and returns the resulting Value.

#ifndef LUMA_SHARED_EVAL_HPP
#define LUMA_SHARED_EVAL_HPP

#include <stdexcept>
#include <string>
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
// lex_and_parse() lives in lex_parse_util.hpp — a front-end-only header — so
// pure-analysis test TUs can reuse it without dragging in the backend below.

// Compile a parsed program in REPL mode, throwing on failure.
[[nodiscard]] inline auto compile_for_eval(const Program& program) {
    Compiler compiler;
    auto result = compiler.compile(program, /*repl_mode=*/true);

    if (!result.success) {
        throw std::runtime_error{"compilation failed"};
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
// register_all() builds all 39 stdlib module namespaces (each defining dozens
// of native functions), so doing it per eval() throws away ~38/39 of that work
// for a source that references a single module.  Instead we register the full
// stdlib exactly once per test process into a prototype scope, and layer a
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

// Unified evaluator — runs the Luma pipeline with the given mode.
[[nodiscard]] inline Value eval_impl(const std::string& source, EvalMode mode) {
    const auto program = lex_and_parse(source);

    if (mode == EvalMode::checked) {
        TypeChecker checker;
        const auto errors = checker.check(program, /*require_main=*/false);

        if (!errors.empty()) {
            std::string msg = "Type check failed:";
            for (const auto& d : errors) {
                msg += "\n  " + d.message;
            }
            throw std::runtime_error{msg};
        }

        Linter linter;
        [[maybe_unused]] const auto warnings = linter.lint(program);
    }

    const auto result = compile_for_eval(program);

    // Layer a fresh child scope over the shared stdlib prototype so this eval's
    // own globals stay isolated while the 39 modules are registered only once.
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
// Used by stdlib unit tests that exercise individual functions.
[[nodiscard]] inline Value eval(const std::string& source) {
    return eval_impl(source, EvalMode::unchecked);
}

// Evaluate source and report whether it raised a runtime error.
// RuntimeError derives from std::runtime_error, so catching std::exception
// covers every failure mode the VM can surface.
[[nodiscard]] inline bool eval_throws(const std::string& source) {
    try {
        (void)eval(source);
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

// ─── Environment helpers ──────────────────────────────────────────

// Create an environment with the full standard library registered.
// Replaces the repeated Environment::create() + register_all(env) pair.
[[nodiscard]] inline EnvPtr make_std_env(bool sandbox = false) {
    const auto env = Environment::create();
    register_all(env, sandbox);
    return env;
}

// ─── Value assertion helpers ──────────────────────────────────────

// Check whether a Value holds a successful result.
[[nodiscard]] inline bool is_success_result(const Value& v) {
    return v.is_result() && v.as_result()->is_success;
}

// ─── Value assertion macros ──────────────────────────────────────

// Assert that a value is a successful result.
#define ASSERT_RESULT_SUCCESS(value)                                                               \
    ASSERT_TRUE((value).is_result());                                                              \
    ASSERT_TRUE((value).as_result()->is_success)

// Assert that a value is a failure result.
#define ASSERT_RESULT_FAILURE(value)                                                               \
    ASSERT_TRUE((value).is_result());                                                              \
    ASSERT_TRUE(!(value).as_result()->is_success)

// Assert that evaluating source code yields a successful result whose
// inner value equals the expected integer.
#define ASSERT_EVAL_INT(source, expected)                                                          \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        auto val_ = luma::test::eval(source);                                                      \
        ASSERT_RESULT_SUCCESS(val_);                                                               \
        ASSERT_EQ(val_.as_result()->owned_inner->as_integer(), (expected));                        \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

// Assert that evaluating source code yields a successful result whose
// inner value equals the expected string.
#define ASSERT_EVAL_STR(source, expected)                                                          \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        auto val_ = luma::test::eval(source);                                                      \
        ASSERT_RESULT_SUCCESS(val_);                                                               \
        ASSERT_EQ(val_.as_result()->owned_inner->as_string(), std::string(expected));              \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

// Assert that evaluating source code yields a successful result whose
// inner value equals the expected boolean.
#define ASSERT_EVAL_BOOL(source, expected)                                                         \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        auto val_ = luma::test::eval(source);                                                      \
        ASSERT_RESULT_SUCCESS(val_);                                                               \
        ASSERT_EQ(val_.as_result()->owned_inner->as_bool(), (expected));                           \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

// Assert that evaluating source code yields a successful result whose
// inner value equals the expected double.
#define ASSERT_EVAL_NUM(source, expected)                                                          \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        auto val_ = luma::test::eval(source);                                                      \
        ASSERT_RESULT_SUCCESS(val_);                                                               \
        ASSERT_EQ(val_.as_result()->owned_inner->as_number(), (expected));                         \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

// Assert that evaluating source code yields a failure result.
#define ASSERT_EVAL_FAILURE(source)                                                                \
    LUMA_MSVC_SUPPRESS_C4127_BEGIN                                                                 \
    do {                                                                                           \
        auto val_ = luma::test::eval(source);                                                      \
        ASSERT_RESULT_FAILURE(val_);                                                               \
    } while (false) LUMA_MSVC_SUPPRESS_C4127_END

} // namespace luma::test

#endif // LUMA_SHARED_EVAL_HPP
