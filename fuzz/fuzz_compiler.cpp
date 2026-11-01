#include "fuzz_harness.hpp"
#include "fuzz_pipeline.hpp"

// LibFuzzer entry point for the Luma compiler.
// Exercises the production back-end assembly: lex → parse → type-check →
// compile → optimise (at the input-derived level) → verify.  The type checker
// is required because the compiler expects a fully typed AST — feeding it raw
// parsed output can cause null-pointer dereferences on malformed but parseable
// input.
//
// Verifier oracle: bytecode the compiler and optimiser produce from a
// type-correct program must always pass the bytecode verifier.  A rejection
// would make `luma --verify` refuse a valid program, so it traps.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::k_max_input_size, [&](const std::string& input) {
            auto program = luma::fuzz::parse_clean(input);
            if (!program) {
                return;
            }

            auto options = luma::fuzz::build_options_for(input.size());
            options.verify = true;

            const auto outcome = luma::fuzz::build(*program, options);
            if (outcome.failed_verification()) {
                luma::fuzz::trap(); // the compiler emitted bytecode the verifier rejects.
            }

            if (!outcome.ok()) {
                return;
            }

            luma::fuzz::do_not_optimize(outcome.artifact->top_level.chunk().code.size());
            luma::fuzz::do_not_optimize(outcome.artifact->top_level.chunk().constants.size());
            luma::fuzz::do_not_optimize(outcome.artifact->functions.size());
        });
}
