#ifndef LUMA_COMPILER_VERIFIER_PASS_HPP
#define LUMA_COMPILER_VERIFIER_PASS_HPP

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/pipeline/pipeline.hpp"
#include "runtime/compiler/compile_result.hpp"
#include "runtime/compiler/compiled_function.hpp"
#include "runtime/compiler/verifier.hpp"

namespace luma {

// Runs the bytecode verifier on all compiled functions.
// Reads the caller-owned CompileArtifact slot populated by CompilerPass.
// Must be placed after the CompilerPass (and optionally OptimizerPass).
// If verification fails, the pipeline halts with diagnostics.
class VerifierPass : public Pass {
public:
    explicit VerifierPass(std::optional<CompileArtifact>& artifact) : artifact_{artifact} {}

    [[nodiscard]] std::string name() const override {
        return std::string{pass_name::verify};
    }

    [[nodiscard]] std::vector<std::string_view> required_passes() const override {
        return {pass_name::compile};
    }

    bool run(Program& /*program*/, PipelineResult& result) override {
        if (!artifact_.has_value()) {
            return true; // No bytecode to verify.
        }

        auto& artifact = *artifact_;
        BytecodeVerifier verifier;
        bool all_valid = true;

        // Verify every chunk (functions then top-level).  The top-level chunk
        // is identified by address so its label matches the original messages
        // exactly, independent of the chunk's name.
        artifact.for_each_chunk([&](CompiledFunction& chunk) {
            const std::string label = (&chunk == &artifact.top_level)
                                          ? std::string{"in top-level"}
                                          : std::format("in '{}'", chunk.name);

            if (!verify_chunk(verifier, chunk, label, result)) {
                all_valid = false;
            }
        });

        return all_valid;
    }

private:
    // Verify a single chunk.  On success marks it verified and returns true;
    // on failure emits one diagnostic per verification error and returns false.
    static bool verify_chunk(BytecodeVerifier& verifier, CompiledFunction& chunk,
                             std::string_view label, PipelineResult& result) {
        auto errors = verifier.verify(chunk);

        if (errors.empty()) {
            chunk.mark_verified();
            return true;
        }

        for (const auto& err : errors) {
            result.diagnostics.push_back(
                diag::error(std::format("bytecode verification failed {}: {}", label, err.message))
                    .category(DiagnosticCategory::Compile)
                    .source(DiagnosticSource::Verify)
                    .build());
        }

        return false;
    }

    std::optional<CompileArtifact>& artifact_;
};

} // namespace luma

#endif // LUMA_COMPILER_VERIFIER_PASS_HPP
