#include "fuzz_harness.hpp"
#include "fuzz_pipeline.hpp"

// LibFuzzer entry point for the Luma VM.
// Exercises the full pipeline: lex → parse → resolve → type-check → compile →
// execute.  Uses sandboxed stdlib registration and tight resource limits to
// prevent runaway execution.
extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/) {
    luma::fuzz::apply_fuzz_resource_limits();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_vm_input_size,
        [&](const std::string& input) { luma::fuzz::run_full_pipeline(input); });
}
