#ifndef LUMA_COMPILER_OPTIMIZER_PASS_HPP
#define LUMA_COMPILER_OPTIMIZER_PASS_HPP

#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/pipeline/pipeline.hpp"
#include "runtime/compiler/compile_result.hpp"
#include "runtime/compiler/optimizer.hpp"

namespace luma {

// Runs the bytecode optimizer on all compiled functions.
// Reads and rewrites the caller-owned CompileArtifact slot populated by
// CompilerPass.  Must be placed after the CompilerPass in the pipeline.
class OptimizerPass : public Pass {
public:
    explicit OptimizerPass(std::optional<CompileArtifact>& artifact, int level = 1)
        : artifact_{artifact}, level_{level} {}

    [[nodiscard]] std::string name() const override {
        return std::string{pass_name::optimize};
    }

    [[nodiscard]] std::vector<std::string_view> required_passes() const override {
        return {pass_name::compile};
    }

    bool run(Program& /*program*/, PipelineResult& /*result*/) override {
        if (!artifact_.has_value()) {
            // This pass requires CompilerPass; if the artifact is missing the
            // dependency check should have caught it.  Assert in debug builds.
            assert(false && "OptimizerPass: no artifact despite CompilerPass dependency");
            return true;
        }

        auto& artifact = *artifact_;
        Optimizer optimizer{level_};

        // Cross-function inlining (before per-chunk optimisation so inlined
        // code benefits from subsequent peephole/folding passes).
        if (level_ >= 2) {
            artifact.for_each_chunk([&](CompiledFunction& func) {
                (void)optimizer.inline_small_functions(func.mutable_chunk(), artifact.functions);
            });
        }

        // Optimize every chunk (functions then top-level).
        artifact.for_each_chunk(
            [&](CompiledFunction& func) { (void)optimizer.optimize(func.mutable_chunk()); });

        return true;
    }

private:
    std::optional<CompileArtifact>& artifact_;
    int level_;
};

} // namespace luma

#endif // LUMA_COMPILER_OPTIMIZER_PASS_HPP
