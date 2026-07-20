#include "fuzz_frontend.hpp"
#include "fuzz_harness.hpp"

// LibFuzzer entry point for the Luma lexer.
// Feeds arbitrary byte sequences to the lexer and ensures it doesn't crash.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size,
        [&](const std::string& input) { luma::fuzz::consume_tokens(luma::fuzz::lex(input)); });
}
