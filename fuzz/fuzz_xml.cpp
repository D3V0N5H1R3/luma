#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>

#include "fuzz_harness.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/text/xml_module.hpp"

// LibFuzzer entry point for the stdlib Xml parser
// (core/runtime/stdlib/text/xml_module_parser.cpp).
//
// This is the hand-written recursive-descent parser behind Xml.deserialize,
// Xml.deserialize_file and Xml.is_valid — the trust boundary that decodes
// untrusted XML text handed to a Luma program (string literals, file contents,
// network payloads).  It walks arbitrary bytes by hand: it skips an optional
// <?xml …?> declaration, rejects <!DOCTYPE …> (external-entity injection
// guard), decodes the five predefined entities (&lt; &gt; &amp; &apos; &quot;),
// reads <!-- comments --> and <![CDATA[ … ]]> sections, parses attributes and
// matched start/end tags, and enforces nesting-depth, child-count and string
// resource limits.  Arbitrary bytes must never crash it, read out of bounds, or
// exhaust memory.  It sits alongside the JSON, CSV, DateTime and KeyValueStore
// decoders as a directly-fuzzed trust boundary, rather than being reached only
// shallowly through sanitised string literals by fuzz_structured.
//
// Two oracles run on top of the never-crash contract:
//   * Canonical re-parse: any tree the parser produces must serialise to text
//     the parser accepts again.  A canonical serialisation that fails to
//     re-parse would be a genuine round-trip bug, so the re-parse is checked
//     outside the tolerated-exception path and traps on failure.
//   * Byte-idempotence: serialising that re-parsed tree must reproduce the exact
//     same canonical text (serialize == reserialize).  The compact serializer
//     emits no incidental whitespace, escapes text/attribute content through the
//     five entities, sanitises comment "--" runs and splits CDATA "]]>" markers,
//     so a parser-produced tree has a single stable canonical form.
//
// Default resource limits apply (this target does not tighten them): with the
// 64 KiB input cap, a text node escaped at most ~4x stays far below the 256 MB
// string limit, so the canonical re-parse can never be rejected by a limit that
// the first parse already satisfied.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            // First parse: malformed input legitimately throws, which the shared
            // run() wrapper tolerates.
            const std::shared_ptr<luma::XmlValue> node = luma::xml_parse_string(input);

            // From here the text is the serializer's own canonical output and MUST
            // be accepted again, and serialising the re-parsed tree must reproduce
            // it byte-for-byte.
            std::string serialized;
            luma::xml_serialize_value(*node, serialized, luma::XmlSerializeOptions{});

            try {
                const std::shared_ptr<luma::XmlValue> reparsed = luma::xml_parse_string(serialized);

                std::string reserialized;
                luma::xml_serialize_value(*reparsed, reserialized, luma::XmlSerializeOptions{});

                if (reserialized != serialized) {
                    luma::fuzz::trap(); // Canonical serialisation is not idempotent.
                }
            } catch (const std::exception&) {
                luma::fuzz::trap(); // Canonical serialisation failed to re-parse.
            }
        });
}
