#include <exception>
#include <string>

#include "fuzz_harness.hpp"
#include "json/json.hpp"

// LibFuzzer entry point for the shared JSON parser (shared/json).
//
// This parser decodes the JSON-RPC bodies exchanged with the language server
// (LSP) and debug adapter (DAP) over stdio — untrusted input crossing a
// process boundary.  Arbitrary bytes must never crash it.
//
// Serialisation oracle: any value the parser produces must serialise to text
// that the parser accepts again.  A canonical serialisation that fails to
// re-parse would be a genuine round-trip bug, so the re-parse is checked
// outside the tolerated-exception path and traps on failure.  (Strict byte
// stability is intentionally not asserted because distinct-but-equal numeric
// encodings — e.g. "-0" vs "0" — are a benign formatting choice, not a bug.)
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            // First parse: malformed input legitimately throws JsonParseError,
            // which the shared run() wrapper tolerates.
            const auto value = luma::json::parse(input);

            // From here the text is the parser's own canonical output and MUST be
            // accepted again.
            const auto serialized = value.to_string();
            try {
                const auto reparsed = luma::json::parse(serialized);
                luma::fuzz::do_not_optimize(reparsed.to_string().size());
            } catch (const std::exception&) {
                luma::fuzz::trap(); // Canonical serialisation failed to re-parse.
            }
        });
}
