#include <optional>
#include <string>

#include "fuzz_harness.hpp"
#include "fuzz_pipeline.hpp"
#include "runtime/compiler/optimizer.hpp"
#include "runtime/compiler/optimizer_pass.hpp"
#include "runtime/compiler/verifier_pass.hpp"

// LibFuzzer entry point for the Luma bytecode optimizer.
// Exercises lex → parse → type-check → compile, then runs the production
// OptimizerPass at every optimisation level over the whole artifact — every
// function chunk and the top-level chunk, plus the level-2 cross-function
// inlining — exactly as compile_program() does.
//
// Verifier oracle: the optimiser must preserve verifiability.  When the
// unoptimised bytecode passes the verifier, the optimised bytecode at every
// level must pass it too; otherwise `luma -O<n> --verify` would reject a
// program that `luma --verify` accepts.  (A compiler-output rejection is
// fuzz_compiler's oracle, so such inputs are skipped here.)
namespace {

// Run the production VerifierPass over a copy of the artifact.
[[nodiscard]] bool verifies(luma::Program& program, std::optional<luma::CompileArtifact> artifact) {
    luma::PipelineResult scratch;
    return luma::VerifierPass{artifact}.run(program, scratch);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::k_max_input_size, [&](const std::string& input) {
            auto program = luma::fuzz::parse_clean(input);
            if (!program) {
                return;
            }

            const auto outcome = luma::fuzz::build(*program, luma::fuzz::BuildOptions{});
            if (!outcome.ok() || !verifies(*program, outcome.artifact)) {
                return;
            }

            for (int level = 1; level <= luma::Optimizer::k_max_level; ++level) {
                std::optional<luma::CompileArtifact> optimized = outcome.artifact;
                luma::PipelineResult scratch;
                (void)luma::OptimizerPass{optimized, level}.run(*program, scratch);

                if (!verifies(*program, optimized)) {
                    luma::fuzz::trap(); // the optimiser broke verifiable bytecode.
                }
            }
        });
}
