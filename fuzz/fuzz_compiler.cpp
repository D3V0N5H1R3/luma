#include "fuzz_harness.hpp"
#include "fuzz_pipeline.hpp"

// LibFuzzer entry point for the Luma compiler.
// Exercises the front-end pipeline: lex → parse → resolve → type-check →
// compile.  The resolver and type checker are required because the compiler
// expects a fully resolved and typed AST — feeding it raw parsed output can
// cause null-pointer dereferences on malformed but parseable input.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            auto program = luma::fuzz::compile_ready_program(input);
            if (!program) {
                return;
            }

            const auto result = luma::fuzz::compile(*program);

            luma::fuzz::do_not_optimize(result.top_level.chunk().code.size());
            luma::fuzz::do_not_optimize(result.top_level.chunk().constants.size());
            luma::fuzz::do_not_optimize(result.functions.size());
        });
}
