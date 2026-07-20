#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "analysis/source/source_manager.hpp"
#include "common/resource_limits.hpp"
#include "fuzz_frontend.hpp"
#include "runtime/compiler/compile_result.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/optimizer.hpp"
#include "runtime/include/include_resolver.hpp"
#include "runtime/interpreter/environment.hpp"
#include "runtime/stdlib/common/stdlib_registry.hpp"
#include "runtime/vm/vm.hpp"

// Back-end pipeline helpers: include resolution, compilation, optimisation,
// and sandboxed execution.  These pull in runtime/VM headers, so only targets
// that link against luma_core should include this header.

namespace luma::fuzz {

// Tight resource limits applied once per fuzzer process (via
// LLVMFuzzerInitialize) to keep execution bounded.  The VM reads the global
// ResourceLimits directly, so setting them once at start-up is sufficient —
// no per-iteration mutation is needed.
inline constexpr int k_fuzz_max_call_depth = 64;
inline constexpr std::size_t k_fuzz_max_container_size = 10'000;
inline constexpr std::size_t k_fuzz_max_string_size = 65'536;
inline constexpr std::int64_t k_fuzz_max_string_repeat = 1'000;

// Apply the fuzzing resource-limit profile.  Call once from
// LLVMFuzzerInitialize in any target that executes bytecode.
inline void apply_fuzz_resource_limits() {
    ResourceLimits::max_call_depth = k_fuzz_max_call_depth;
    ResourceLimits::max_array_size = k_fuzz_max_container_size;
    ResourceLimits::max_dictionary_size = k_fuzz_max_container_size;
    ResourceLimits::max_string_size = k_fuzz_max_string_size;
    ResourceLimits::max_string_repeat = k_fuzz_max_string_repeat;
}

// Run lex → parse → resolve → type-check and return a compile-ready AST.
// Returns nullopt when the input has syntax or type errors, which is the
// gate every compile/execute target shares: the compiler assumes a fully
// resolved and typed AST, and feeding it recovered-but-invalid output can
// trigger null dereferences.
[[nodiscard]] inline std::optional<Program> compile_ready_program(const std::string& input) {
    auto parsed = parse(input);
    if (!parsed.ok) {
        return std::nullopt;
    }

    do_not_optimize(resolve(parsed.program).size());

    if (has_error(type_check(parsed.program))) {
        return std::nullopt;
    }

    return std::move(parsed.program);
}

// Compile a resolved, type-checked program to bytecode.
[[nodiscard]] inline CompileResult compile(Program& program) {
    Compiler compiler;
    return compiler.compile(program, /*repl_mode=*/false);
}

// Lex → parse → include-resolution.  Exercises pathological include paths and
// circular references.
inline void resolve_includes(Program& program) {
    SourceManager source_manager;
    IncludeResolver resolver{source_manager};
    do_not_optimize(resolver.resolve(program));
    do_not_optimize(resolver.get_diagnostics().size());
}

// Full pipeline: front-end → compile → execute under the sandboxed stdlib.
// Shared by the fuzz_vm and fuzz_structured targets.
inline void run_full_pipeline(const std::string& input) {
    auto program = compile_ready_program(input);
    if (!program) {
        return;
    }

    auto result = compile(*program);
    if (!result.success) {
        return;
    }

    auto env = Environment::create();
    register_all(env, /*sandbox=*/true);

    VM vm{env};
    vm.execute(result.functions, result.top_level);
}

} // namespace luma::fuzz
