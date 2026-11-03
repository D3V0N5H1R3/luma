#include "fuzz_frontend.hpp"
#include "fuzz_harness.hpp"

// LibFuzzer entry point for the Luma linter.
// Exercises lex → parse → type-check → lint exactly as production assembles
// them: the linter reads the annotations the type checker leaves on the AST, and
// LintPass still runs after a type-check failure.  The parser's error state is
// intentionally ignored: the linter must handle arbitrary recovered AST shapes
// without crashing.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::k_max_input_size, [&](const std::string& input) {
            auto parsed = luma::fuzz::parse(input);
            const auto result = luma::fuzz::type_check_and_lint(parsed.program);
            luma::fuzz::consume_diagnostics(result.diagnostics);
        });
}
