#include "fuzz_frontend.hpp"
#include "fuzz_harness.hpp"

// LibFuzzer entry point for the Luma linter.
// Exercises lex → parse → lint.  The parser's error state is intentionally
// ignored: the linter must handle arbitrary AST shapes without crashing.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            auto parsed = luma::fuzz::parse(input);
            luma::fuzz::consume_diagnostics(luma::fuzz::lint(parsed.program));
        });
}
