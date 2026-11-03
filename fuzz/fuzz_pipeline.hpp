#ifndef LUMA_FUZZ_FUZZ_PIPELINE_HPP
#define LUMA_FUZZ_FUZZ_PIPELINE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "analysis/pipeline/pipeline.hpp"
#include "analysis/pipeline/type_checker_pass.hpp"
#include "analysis/source/source_manager.hpp"
#include "common/resource_limits.hpp"
#include "fuzz_frontend.hpp"
#include "runtime/compiler/compile_result.hpp"
#include "runtime/compiler/compiled_function.hpp"
#include "runtime/compiler/compiler_pass.hpp"
#include "runtime/compiler/optimizer.hpp"
#include "runtime/compiler/optimizer_pass.hpp"
#include "runtime/compiler/verifier_pass.hpp"
#include "runtime/include/include_resolver.hpp"
#include "runtime/interpreter/environment.hpp"
#include "runtime/stdlib/common/stdlib_registry.hpp"
#include "runtime/vm/vm.hpp"

// Back-end pipeline helpers: include resolution, compilation, optimisation,
// verification, and sandboxed execution.  These pull in runtime/VM headers, so
// only targets that link against luma_core should include this header.

namespace luma::fuzz {

// Tight resource limits applied once per fuzzer process (via
// LLVMFuzzerInitialize) to keep execution bounded.  The VM reads the global
// ResourceLimits directly, so setting them once at start-up is sufficient —
// no per-iteration mutation is needed.
inline constexpr int k_fuzz_max_call_depth = 64;
inline constexpr std::size_t k_fuzz_max_container_size = 10'000;
inline constexpr std::size_t k_fuzz_max_string_size = 65'536;
// Regex inputs stay at the stack-safe production default (32 KiB): the
// backtracking engine's recursion depth grows with the input, so a larger
// fuzz value would only find the stack overflow the default exists to avoid.
inline constexpr std::size_t k_fuzz_max_regex_input_size = 32'768;
inline constexpr std::int64_t k_fuzz_max_string_repeat = 1'000;
inline constexpr std::int64_t k_fuzz_max_levenshtein_input = 256;
inline constexpr std::size_t k_fuzz_max_task_queue_size = 1'000;
// The loop budgets are the main bound on execution time: at the default
// (10 million back-edges) a single runaway `while` costs seconds per input,
// and the total budget — off by default — is what bounds nested `for` loops,
// which reset the shared while budget on entry.
inline constexpr std::int64_t k_fuzz_max_loop_iterations = 100'000;
inline constexpr std::int64_t k_fuzz_max_total_loop_iterations = 1'000'000;

// Apply the fuzzing resource-limit profile.  Call once from
// LLVMFuzzerInitialize in any target that executes bytecode.
inline void apply_fuzz_resource_limits() {
    ResourceLimits::max_call_depth = k_fuzz_max_call_depth;

    ResourceLimits::max_array_size = k_fuzz_max_container_size;
    ResourceLimits::max_dictionary_size = k_fuzz_max_container_size;
    ResourceLimits::max_queue_size = k_fuzz_max_container_size;
    ResourceLimits::max_stack_size = k_fuzz_max_container_size;
    ResourceLimits::max_set_size = k_fuzz_max_container_size;
    ResourceLimits::max_json_elements = k_fuzz_max_container_size;
    ResourceLimits::max_channel_queue_size = k_fuzz_max_container_size;

    ResourceLimits::max_string_size = k_fuzz_max_string_size;
    ResourceLimits::max_pad_width = k_fuzz_max_string_size;
    ResourceLimits::max_regex_input_size = k_fuzz_max_regex_input_size;
    ResourceLimits::max_string_repeat = k_fuzz_max_string_repeat;
    ResourceLimits::max_levenshtein_input = k_fuzz_max_levenshtein_input;

    ResourceLimits::max_task_queue_size = k_fuzz_max_task_queue_size;
    ResourceLimits::max_while_iterations = k_fuzz_max_loop_iterations;
    ResourceLimits::max_total_loop_iterations = k_fuzz_max_total_loop_iterations;
}

// The back-end profile a build mirrors.  Production compiles at -O1 by
// default, runs the verifier only for --verify (or a cached .lumc), and executes
// verified bytecode on a fast path that skips the VM's bounds checks — so the
// fuzzers must cover every combination, not just the unoptimised one.
struct BuildOptions {
    int optimize_level{0}; // 0 = no OptimizerPass, like OptimizationLevel::None.
    bool verify{false};
};

// Derive the build profile from the input size so that every profile is
// explored deterministically (a crashing input always reproduces) without
// consuming any of the bytes the lexer sees.
[[nodiscard]] inline BuildOptions build_options_for(std::size_t input_size) {
    constexpr auto level_count = static_cast<std::size_t>(Optimizer::k_max_level) + 1;
    return BuildOptions{.optimize_level = static_cast<int>(input_size % level_count),
                        .verify = (input_size / level_count) % 2 == 1};
}

// Result of a back-end build: every diagnostic the passes produced plus the
// bytecode when the build succeeded.
struct BuildOutcome {
    PipelineResult result;
    std::optional<CompileArtifact> artifact;

    [[nodiscard]] bool ok() const {
        return !result.has_errors() && artifact.has_value();
    }

    // True when the verifier rejected bytecode the compiler (and optimiser)
    // produced — a compiler or optimiser bug, never a property of the input.
    [[nodiscard]] bool failed_verification() const {
        return std::ranges::any_of(result.diagnostics, [](const Diagnostic& d) {
            return d.severity == Severity::Error && d.source == DiagnosticSource::Verify;
        });
    }
};

// Type-check → compile → [optimise] → [verify] through the production pass
// classes, assembled exactly like compile_program() (the linter is omitted: it
// only adds warnings, which never block a build).  The program must already
// have parsed cleanly — see parse_clean().
[[nodiscard]] inline BuildOutcome build(Program& program, const BuildOptions& options) {
    std::optional<CompileArtifact> artifact;

    auto pipeline =
        Pipeline::builder()
            .add<TypeCheckerPass>(/*require_main=*/false)
            .add<CompilerPass>(artifact)
            .add_if<OptimizerPass>(options.optimize_level > 0, artifact, options.optimize_level)
            .add_if<VerifierPass>(options.verify, artifact)
            .build();

    auto result = pipeline.run(program);
    return BuildOutcome{.result = std::move(result), .artifact = std::move(artifact)};
}

// Lex → parse → include-resolution.  Exercises pathological include paths and
// circular references.
inline void resolve_includes(Program& program) {
    SourceManager source_manager;
    IncludeResolver resolver{source_manager};
    do_not_optimize(resolver.resolve(program));
    do_not_optimize(resolver.get_diagnostics().size());
}

// Execute bytecode under the sandboxed stdlib.  Shared by the source-driven
// targets and by fuzz_bytecode_deserializer, which runs decoded .lumc files.
inline void execute(const std::vector<CompiledFunction>& functions,
                    const CompiledFunction& top_level) {
    auto env = Environment::create();
    register_all(env, /*sandbox=*/true);

    VM vm{env};
    vm.execute(functions, top_level);
}

// Full pipeline: front-end → compile → optimise/verify per the input-derived
// profile → execute under the sandboxed stdlib.  Shared by the fuzz_vm and
// fuzz_structured targets.
inline void run_full_pipeline(const std::string& input) {
    auto program = parse_clean(input);
    if (!program) {
        return;
    }

    const auto outcome = build(*program, build_options_for(input.size()));
    if (!outcome.ok()) {
        return;
    }

    execute(outcome.artifact->functions, outcome.artifact->top_level);
}

} // namespace luma::fuzz

#endif // LUMA_FUZZ_FUZZ_PIPELINE_HPP
