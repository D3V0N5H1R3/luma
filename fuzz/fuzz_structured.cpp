#include <cstddef>
#include <cstdint>
#include <string>

#include <fuzzer/FuzzedDataProvider.h>

#include "fuzz_harness.hpp"
#include "fuzz_pipeline.hpp"
#include "fuzz_source_generator.hpp"

// Structured fuzzer that generates Luma source from fuzz input, reaching deeper
// code paths than raw byte fuzzing.  Each input yields either a well-typed
// program, which runs through the full production back end (optimised and
// verified per the input-derived profile) on the VM, or a deliberately
// ill-typed chaos program for the front end.  The generator lives in
// fuzz_source_generator.hpp and is driven here by LibFuzzer's
// FuzzedDataProvider.
extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/) {
    luma::fuzz::apply_fuzz_resource_limits();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < luma::fuzz::gen::k_min_structured_input ||
        size > luma::fuzz::gen::k_max_structured_input) {
        return 0;
    }

    FuzzedDataProvider fdp{data, size};
    const auto input = luma::fuzz::gen::generate_program(fdp);

    return luma::fuzz::run([&] { luma::fuzz::run_full_pipeline(input); });
}
