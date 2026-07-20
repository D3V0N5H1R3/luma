#include <exception>
#include <string>

#include "fuzz_harness.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/text/json_module.hpp"

// LibFuzzer entry point for the stdlib Json parser
// (core/runtime/stdlib/text/json_module_parser.cpp).
//
// This is the hand-written recursive-descent parser behind Json.deserialize,
// Json.is_valid, Json.get, Json.set, Json.merge, Json.get_path and
// Json.set_path — the trust boundary that decodes untrusted JSON text handed to
// a Luma program (string literals, file contents, network payloads).  It is
// distinct from the shared/json parser exercised by fuzz_json: it builds runtime
// Values, decodes \uXXXX (including UTF-16 surrogate pairs) by hand, rejects
// leading zeros, and enforces array/object/string/depth resource limits.
// Arbitrary bytes must never crash it, read out of bounds, or exhaust memory.
//
// Serialisation oracle: any Value the parser produces must serialise to text the
// parser accepts again.  A canonical serialisation that fails to re-parse would
// be a genuine round-trip bug, so the re-parse is checked outside the
// tolerated-exception path and traps on failure.  (Strict byte stability is
// intentionally not asserted because distinct-but-equal encodings — e.g. a
// "-0.0" double serialising to "-0", which re-parses as integer 0 — are a benign
// formatting choice, not a bug.)
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            // First parse: malformed input legitimately throws, which the shared
            // run() wrapper tolerates.
            const luma::Value value = luma::json_parse_string(input);

            // From here the text is the serializer's own canonical output and MUST
            // be accepted again.
            std::string serialized;
            luma::json_serialize_value(value, serialized, 0, 0, false);

            try {
                const luma::Value reparsed = luma::json_parse_string(serialized);

                std::string reserialized;
                luma::json_serialize_value(reparsed, reserialized, 0, 0, false);
                luma::fuzz::do_not_optimize(reserialized.size());
            } catch (const std::exception&) {
                luma::fuzz::trap(); // Canonical serialisation failed to re-parse.
            }
        });
}
