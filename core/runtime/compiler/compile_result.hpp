#ifndef LUMA_COMPILER_COMPILE_RESULT_HPP
#define LUMA_COMPILER_COMPILE_RESULT_HPP

#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "runtime/compiler/chunk.hpp"

namespace luma {

// Clean compiled bytecode — the artifact produced by a successful compilation.
// This is what the pipeline stores, the cache holds, and the VM executes.
// It carries no diagnostics or status flags: by definition, if you have a
// CompileArtifact, compilation succeeded and the bytecode is ready to run.
struct CompileArtifact {
    CompiledFunction top_level;
    std::vector<CompiledFunction> functions;

    CompileArtifact() = default;
    CompileArtifact(const CompileArtifact&) = default;
    CompileArtifact& operator=(const CompileArtifact&) = default;
    CompileArtifact(CompileArtifact&&) = default;
    CompileArtifact& operator=(CompileArtifact&&) = default;
    ~CompileArtifact() = default;

    // Apply `fn` to every compiled chunk: each function first (in declaration
    // order), then the top-level chunk.  Centralising the "functions then
    // top-level" traversal lets per-chunk passes iterate uniformly and can
    // never forget the top-level chunk.
    void for_each_chunk(auto&& fn) {
        for (auto& func : functions) {
            fn(func);
        }
        fn(top_level);
    }
};

// Raw result of Compiler::compile() — includes the compiled bytecode,
// any diagnostics emitted during compilation, and a success flag.
// Consumers (e.g. CompilerPass) drain diagnostics into the pipeline result
// and promote the bytecode into a CompileArtifact on success.
struct CompileResult {
    CompiledFunction top_level;
    std::vector<CompiledFunction> functions;
    std::vector<Diagnostic> diagnostics;
    bool success{true};

    CompileResult() = default;
    CompileResult(const CompileResult&) = default;
    CompileResult& operator=(const CompileResult&) = default;
    CompileResult(CompileResult&&) = default;
    CompileResult& operator=(CompileResult&&) = default;
    ~CompileResult() = default;
};

} // namespace luma

#endif // LUMA_COMPILER_COMPILE_RESULT_HPP
