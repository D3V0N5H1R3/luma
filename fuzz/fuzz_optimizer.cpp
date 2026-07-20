#include "fuzz_harness.hpp"
#include "fuzz_pipeline.hpp"
#include "runtime/compiler/optimizer.hpp"

// LibFuzzer entry point for the Luma bytecode optimizer.
// Exercises lex → parse → resolve → type-check → compile → optimize, running
// every optimization level over the compiled bytecode.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            auto program = luma::fuzz::compile_ready_program(input);
            if (!program) {
                return;
            }

            auto result = luma::fuzz::compile(*program);
            if (!result.success) {
                return;
            }

            for (int level = 0; level <= luma::Optimizer::k_max_level; ++level) {
                auto chunk_copy = result.top_level.chunk();
                luma::Optimizer optimizer{level};
                luma::fuzz::do_not_optimize(optimizer.optimize(chunk_copy));
            }
        });
}
