#include "fuzz_frontend.hpp"
#include "fuzz_harness.hpp"

// LibFuzzer entry point for the Luma type checker.
// Exercises lex → parse → resolve → type-check.  Earlier-stage errors are
// intentionally not gated: the type checker must tolerate arbitrary, partially
// recovered ASTs without crashing.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            auto parsed = luma::fuzz::parse(input);
            luma::fuzz::consume_diagnostics(luma::fuzz::resolve(parsed.program));
            luma::fuzz::consume_diagnostics(luma::fuzz::type_check(parsed.program));
        });
}
