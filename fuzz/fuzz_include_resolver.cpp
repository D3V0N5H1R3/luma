#include "fuzz_harness.hpp"
#include "fuzz_pipeline.hpp"

// LibFuzzer entry point for the Luma include resolver.
// Feeds arbitrary source text through lex → parse → include resolution.  The
// resolver must handle pathological include paths and circular references
// without crashing or hanging.  Parser errors are intentionally not gated so
// that malformed include declarations are exercised.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(data, size, luma::fuzz::max_input_size,
                                [&](const std::string& input) {
                                    auto parsed = luma::fuzz::parse(input);
                                    luma::fuzz::resolve_includes(parsed.program);
                                });
}
