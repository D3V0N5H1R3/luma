#ifndef LUMA_COMPILER_COMPILER_PASS_HPP
#define LUMA_COMPILER_COMPILER_PASS_HPP

#include <optional>
#include <string>
#include <utility>

#include "analysis/pipeline/pipeline.hpp"
#include "runtime/compiler/compile_result.hpp"
#include "runtime/compiler/compiler.hpp"

namespace luma {

// Compiles the AST to bytecode.
// On success, writes the CompileArtifact into the caller-owned slot supplied
// at construction — the generic PipelineResult stays artifact-agnostic, so the
// pass framework need not depend on the back-end.
// Note: TypeCheckerPass is not a hard dependency — the compiler operates
// on the AST directly.  In practice, always run TypeCheckerPass first so
// the AST is fully annotated and validated before compilation.
class CompilerPass : public Pass {
public:
    explicit CompilerPass(std::optional<CompileArtifact>& artifact) : artifact_{artifact} {}

    [[nodiscard]] std::string name() const override {
        return std::string{pass_name::compile};
    }

    bool run(Program& program, PipelineResult& result) override {
        Compiler compiler;

        auto compile_result = compiler.compile(program);

        merge_diagnostics(result.diagnostics, std::move(compile_result.diagnostics));

        if (!compile_result.success) {
            return false;
        }

        // Promote the bytecode into a clean CompileArtifact.
        // Diagnostics have already been moved to the pipeline result above;
        // the artifact carries only what the VM needs.
        CompileArtifact artifact;
        artifact.top_level = std::move(compile_result.top_level);
        artifact.functions = std::move(compile_result.functions);
        artifact_ = std::move(artifact);

        return true;
    }

private:
    std::optional<CompileArtifact>& artifact_;
};

} // namespace luma

#endif // LUMA_COMPILER_COMPILER_PASS_HPP
