#ifndef LUMA_PIPELINE_PIPELINE_HPP
#define LUMA_PIPELINE_PIPELINE_HPP

#include <algorithm>
#include <chrono>
#include <format>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "common/string_hash.hpp"

namespace luma {

struct Program;

// Canonical pass name constants.
// Use these instead of string literals to prevent typos in name() and
// required_passes() overrides.  Adding a new pass?  Add its constant here.
namespace pass_name {

inline constexpr std::string_view type_check = "type-check";
inline constexpr std::string_view lint = "lint";
inline constexpr std::string_view compile = "compile";
inline constexpr std::string_view optimize = "optimize";
inline constexpr std::string_view verify = "verify";

} // namespace pass_name

// Timing information for a single pipeline pass.
struct PassTiming {
    std::string name;
    std::chrono::microseconds duration{0};
};

// Move all diagnostics from `source` into `target`.
// Provides a single, consistent diagnostic-transfer pattern for all
// pipeline passes — prefer this over hand-rolled loops.
inline void merge_diagnostics(std::vector<Diagnostic>& target, std::vector<Diagnostic>&& source) {
    target.insert(target.end(), std::make_move_iterator(source.begin()),
                  std::make_move_iterator(source.end()));
}

// Const-ref overload: copies diagnostics when a move is not possible
// (e.g. the source exposes only a const reference).
inline void merge_diagnostics(std::vector<Diagnostic>& target,
                              const std::vector<Diagnostic>& source) {
    target.insert(target.end(), source.begin(), source.end());
}

// Result of running the pipeline — the diagnostics (errors + warnings)
// collected along the way plus per-pass timing.  The framework is
// deliberately artifact-agnostic: back-end passes (compile/optimize/verify)
// exchange the compiled bytecode through a caller-owned slot, so this generic
// result stays coupled only to the AST and diagnostics.
struct PipelineResult {
    std::vector<Diagnostic> diagnostics;
    std::vector<PassTiming> timings;

    // Short-circuit helpers — each scans at most until the first matching
    // diagnostic, so they are O(1) in the common case where errors exist.
    [[nodiscard]] bool has_errors() const {
        return std::ranges::any_of(
            diagnostics, [](const Diagnostic& d) { return d.severity == Severity::Error; });
    }

    [[nodiscard]] bool has_warnings() const {
        return std::ranges::any_of(
            diagnostics, [](const Diagnostic& d) { return d.severity == Severity::Warning; });
    }

    [[nodiscard]] std::size_t error_count() const {
        return static_cast<std::size_t>(std::ranges::count_if(
            diagnostics, [](const Diagnostic& d) { return d.severity == Severity::Error; }));
    }

    [[nodiscard]] std::size_t warning_count() const {
        return static_cast<std::size_t>(std::ranges::count_if(
            diagnostics, [](const Diagnostic& d) { return d.severity == Severity::Warning; }));
    }
};

// A single pass in the compilation pipeline.
class Pass {
public:
    virtual ~Pass() noexcept = default;

    [[nodiscard]] virtual std::string name() const = 0;

    // Run this pass.  Returns true if the pass succeeded (no errors).
    // Diagnostics should be appended to the result.
    [[nodiscard]] virtual bool run(Program& program, PipelineResult& result) = 0;

    // Returns the set of pass names that must run before this pass.
    // Override to declare dependencies.  Use pass_name:: constants.
    // Default: no dependencies.
    [[nodiscard]] virtual std::vector<std::string_view> required_passes() const {
        return {};
    }

    // Whether this pass should still run even if a previous pass failed.
    // Override to return true for advisory/warning-only passes (e.g. lint).
    [[nodiscard]] virtual bool run_after_failure() const noexcept {
        return false;
    }

protected:
    Pass() = default;
    Pass(const Pass&) = default;
    Pass& operator=(const Pass&) = default;
    Pass(Pass&&) = default;
    Pass& operator=(Pass&&) = default;
};

// Composes multiple passes into a sequential pipeline.
// Stops on the first pass that produces errors.
//
// ── Dependency Validation ──────────────────────────────────────────────────
// The pipeline enforces pass ordering through two complementary mechanisms:
//
//   1. Build-time (Builder::validate_dependencies):
//      Called by Builder::build(). Walks the registered passes in order and
//      ensures every name listed in Pass::required_passes() appears before
//      the declaring pass. Throws std::logic_error on violation. This catches
//      mis-ordered pipelines before the first run.
//
//   2. Run-time (Pipeline::run):
//      Guards each pass just before execution. If a required predecessor pass
//      did not complete successfully (e.g. it was skipped after an earlier
//      failure), an internal-error diagnostic is emitted. This is a safety
//      net — the build-time check should catch ordering bugs first.
//
// Passes declare their dependencies by overriding Pass::required_passes().
// A pass with no overriding of required_passes() has no declared dependencies.
//
// ── Standard Pass Execution Order ─────────────────────────────────────────
// The canonical compilation pipeline is assembled in
//   core/runtime/compiler/compilation_pipeline.hpp (compile_program)
// and follows this order:
//
//   [1] TypeCheckerPass   — static type checking (required for Lint/Compiler)
//   [2] LintPass          — code quality warnings (requires TypeCheckerPass)
//   [3] CompilerPass      — AST → bytecode (requires TypeCheckerPass)
//   [4] OptimizerPass     — bytecode optimisation (optional; requires Compiler)
//   [5] VerifierPass      — bytecode integrity check (optional; requires Compiler)
//
// Passes [1]–[2] are front-end passes and live in this library
// (core/analysis/pipeline/).  The back-end passes [3]–[5] live next to the code
// they wrap in core/runtime/compiler/ and exchange the compiled bytecode
// through a caller-owned std::optional<CompileArtifact> slot, so this generic
// framework stays coupled only to the AST and diagnostics.  The check-only path
// (cli_runner.cpp::check_file) sets CompilerProfile::compile = false, so
// compile_program runs only [1] and [2].
//
// Usage (front-end passes shown; back-end passes additionally take the
// artifact slot — see compilation_pipeline.cpp):
//     auto pipeline = Pipeline::builder()
//         .add<TypeCheckerPass>()
//         .add<LintPass>()
//         .build();
//
//     auto result = pipeline.run(program);
class Pipeline {
public:
    // Fluent builder for constructing pipelines.
    class Builder {
    public:
        Builder() = default;
        Builder(Builder&&) = default;
        Builder& operator=(Builder&&) = default;
        Builder(const Builder&) = delete;
        Builder& operator=(const Builder&) = delete;

        // Add a pass constructed with the given arguments.
        template <typename T, typename... Args> [[nodiscard]] Builder& add(Args&&... args) {
            passes_.push_back(std::make_unique<T>(std::forward<Args>(args)...));

            return *this;
        }

        // Conditionally add a pass.
        template <typename T, typename... Args>
        [[nodiscard]] Builder& add_if(bool condition, Args&&... args) {
            if (condition) {
                passes_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
            }

            return *this;
        }

        [[nodiscard]] Pipeline build() {
            // An empty pipeline is allowed (e.g. all passes were add_if(false)).
            // Dependency validation is only meaningful when passes are present.
            if (!passes_.empty()) {
                validate_dependencies();
            }
            return Pipeline{std::move(passes_)};
        }

    private:
        void validate_dependencies() const {
            StringSet seen_passes;

            for (const auto& pass : passes_) {
                for (const auto& req : pass->required_passes()) {
                    if (!seen_passes.contains(req)) {
                        throw std::logic_error{
                            std::format("Pipeline: pass '{}' requires '{}' "
                                        "which is not registered or appears later",
                                        pass->name(), req)};
                    }
                }

                seen_passes.insert(std::string{pass->name()});
            }
        }

        std::vector<std::unique_ptr<Pass>> passes_;
    };

    [[nodiscard]] static Builder builder() {
        return Builder{};
    }

    [[nodiscard]] PipelineResult run(Program& program);

private:
    explicit Pipeline(std::vector<std::unique_ptr<Pass>> passes);

    std::vector<std::unique_ptr<Pass>> passes_;
};

} // namespace luma

#endif // LUMA_PIPELINE_PIPELINE_HPP
