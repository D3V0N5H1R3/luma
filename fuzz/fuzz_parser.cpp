#include "fuzz_frontend.hpp"
#include "fuzz_harness.hpp"

// LibFuzzer entry point for the Luma parser.
// Tokenizes arbitrary input, then feeds tokens to the parser.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(data, size, luma::fuzz::max_input_size,
                                [&](const std::string& input) {
                                    const auto parsed = luma::fuzz::parse(input);
                                    luma::fuzz::do_not_optimize(parsed.program.declarations.size());
                                    luma::fuzz::do_not_optimize(parsed.program.statements.size());
                                });
}
