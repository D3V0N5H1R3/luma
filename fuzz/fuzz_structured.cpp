#include <cstddef>
#include <cstdint>
#include <string>

#include <fuzzer/FuzzedDataProvider.h>

#include "fuzz_harness.hpp"
#include "fuzz_pipeline.hpp"
#include "fuzz_source_generator.hpp"

// Structured fuzzer that generates syntactically plausible Luma source from
// fuzz input, reaching deeper code paths than raw byte fuzzing.  The grammar
// generator lives in fuzz_source_generator.hpp and is driven here by
// LibFuzzer's FuzzedDataProvider.
extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/) {
    luma::fuzz::apply_fuzz_resource_limits();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < luma::fuzz::gen::min_structured_input ||
        size > luma::fuzz::gen::max_structured_input) {
        return 0;
    }

    FuzzedDataProvider fdp{data, size};
    const auto input = luma::fuzz::gen::generate_program(fdp);

    return luma::fuzz::run([&] { luma::fuzz::run_full_pipeline(input); });
}
