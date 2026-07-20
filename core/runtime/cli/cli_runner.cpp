#include "runtime/cli/cli.hpp"
#include "runtime/cli/cli_internal.hpp"

// ── CLI error handling policy ─────────────────────────────────────────────────
//
//   Compilation/analysis errors:
//     Returned as a ProgramLoadResult (errors field) or DiagnosticCollector
//     results from Pipeline::run().  The caller calls report_diagnostics(),
//     which routes them through DiagnosticRenderer for rich source-context
//     formatting, then maps the result to an exit code.
//
//   Runtime errors:
//     Caught at the top-level with_error_handling() boundary (cli_internal.hpp).
//     RuntimeError exceptions are rendered as single-span diagnostics; other
//     std::runtime_error exceptions are written to std::cerr.  All are mapped
//     to a stable exit code (exit_code::*).
//
//   Programmer errors (pre-condition violations, logic errors):
//     Asserted or thrown as std::logic_error.  These propagate through
//     with_error_handling(), which maps them to exit_code::runtime_error after
//     writing to std::cerr.  They indicate bugs in the interpreter, not in
//     user code.
//
// ─────────────────────────────────────────────────────────────────────────────

#include <filesystem>
#include <iostream>

#include "runtime/compiler/bytecode_serializer.hpp"
#include "runtime/compiler/compilation_cache.hpp"
#include "runtime/compiler/compile_result.hpp"
#include "runtime/stdlib/common/stdlib_registry.hpp"
#include "runtime/stdlib/system/process_module.hpp"
#include "runtime/vm/vm.hpp"

namespace luma {

namespace {

// Fibonacci hashing (golden ratio) — distributes bits evenly for cache key computation.
constexpr std::uint64_t k_hash_mix_golden_ratio = 0x9e3779b97f4a7c15ULL;

// Mix an optimization level into a content hash so different -O flags produce
// distinct disk-cache keys.
[[nodiscard]] std::uint64_t compute_cache_key(std::uint64_t source_hash,
                                              OptimizationLevel optimize_level) {
    return source_hash ^ (static_cast<std::uint64_t>(optimize_level) * k_hash_mix_golden_ratio);
}

// Attempt to load compiled bytecode from the on-disk cache.
// Returns a CompileArtifact on a hit, or nullopt on a miss or validation failure.
[[nodiscard]] std::optional<CompileArtifact>
try_load_from_disk_cache(const std::filesystem::path& cache_file, std::uint64_t cache_key) {
    auto disk = BytecodeSerializer::read_file(cache_file, cache_key);

    if (!disk) {
        return std::nullopt;
    }

    CompileArtifact artifact;
    artifact.top_level = std::move(disk->top_level);
    artifact.functions = std::move(disk->functions);
    return artifact;
}

// Persist compiled bytecode to the on-disk cache.
// Emits a warning to stderr if the write fails.
void save_to_disk_cache(const std::filesystem::path& cache_file, const CompiledFunction& top_level,
                        const std::vector<CompiledFunction>& functions, std::uint64_t cache_key) {
    if (!BytecodeSerializer::write_file(cache_file, top_level, functions, cache_key)) {
        std::cerr << "Warning: failed to write bytecode cache to " << cache_file << "\n";
    }
}

// Set up a global environment with stdlib modules and execute compiled bytecode.
[[nodiscard]] int execute_bytecode(const CompiledFunction& top_level,
                                   const std::vector<CompiledFunction>& functions,
                                   const std::vector<std::string>& args, bool sandbox) {
    set_program_args(args);
    const auto global_env = Environment::create();
    register_all(global_env, sandbox);
    VM vm{global_env};
    vm.execute(functions, top_level);
    return exit_code::success;
}

} // namespace

int check_file(const std::string& path, bool strict) {
    return with_error_handling([&path, strict](SourceManager& source_manager) {
        // Check-only profile: type-check + lint, no bytecode compilation.  This
        // delegates to compile_program so the load → parse-report →
        // diagnostic-report → exit-code/strict scaffolding lives in one place.
        CompilerProfile profile;
        profile.compile = false;
        profile.strict = strict;

        const auto outcome = compile_program(path, source_manager, profile);

        if (outcome.success && !outcome.pipeline_result.has_warnings()) {
            std::cout << "No type errors found.\n";
        }

        return outcome.exit_code;
    });
}

int run_file(const std::string& path, const RunOptions& options) {
    // Process-global in-memory cache, complementing the on-disk cache.
    // Using a function-local static keeps it out of the public API while
    // still benefiting callers that invoke run_file() repeatedly (e.g., REPL).
    static CompilationCache default_cache;
    return run_file(path, options, default_cache);
}

int run_file(const std::string& path, const RunOptions& options, CompilationCache& cache) {
    return with_error_handling([&path, &options, &cache](SourceManager& source_manager) {
        // Load source text so the cache can hash it for invalidation.
        const auto& source_file = source_manager.load(path);
        const CompilationCache::Options cache_opts{
            options.optimize_level != OptimizationLevel::None, false};

        // Strict mode treats warnings as errors, but cached artifacts record no
        // warnings — serving one would skip check_strict_warnings() and let a
        // program that should fail (exit 2) run and exit 0.  Force the full
        // compile path whenever strict is requested so warnings are re-checked.
        if (!options.strict) {
            if (auto cached = cache.get(path, source_file.text, cache_opts)) {
                return execute_bytecode(cached->top_level, cached->functions, options.args,
                                        options.sandbox);
            }
        }

        // Disk cache — survives across process restarts.
        const auto raw_hash = BytecodeSerializer::hash_source(source_file.text);
        const auto cache_key = compute_cache_key(raw_hash, options.optimize_level);
        const auto cache_file = BytecodeSerializer::cache_path_for(path);

        if (!options.strict) {
            if (auto cr = try_load_from_disk_cache(cache_file, cache_key)) {
                cache.put(path, source_file.text, *cr, cache_opts);
                return execute_bytecode(cr->top_level, cr->functions, options.args,
                                        options.sandbox);
            }
        }

        const CompilerProfile config = options.to_compiler_profile();

        auto outcome = compile_program(path, source_manager, config);

        if (!outcome.success) {
            return outcome.exit_code;
        }

        const auto* compile_result = extract_compile_result(outcome.artifact);

        cache.put(path, source_file.text, *compile_result, cache_opts);
        save_to_disk_cache(cache_file, compile_result->top_level, compile_result->functions,
                           cache_key);

        return execute_bytecode(compile_result->top_level, compile_result->functions, options.args,
                                options.sandbox);
    });
}

} // namespace luma
