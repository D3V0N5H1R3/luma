#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "fuzz_harness.hpp"
#include "runtime/stdlib/system/datetime_codec.hpp"

// LibFuzzer entry point for the DateTime module's ISO-8601 codec
// (core/runtime/stdlib/system/datetime_codec.hpp).
//
// parse_iso8601 is the trust-boundary parser behind DateTime.from_iso_string — a
// hand-written reader that walks untrusted text from Luma strings, pulling a
// "YYYY-MM-DD" date, an optional "THH:MM:SS" time, and an optional
// "Z" / "+HH:MM" / "-HH:MM" zone offset out of an istringstream by hand.  It
// sits alongside the JSON, CSV, bytecode and compression decoders as a
// directly-fuzzed codec rather than being reached only indirectly through the VM
// by fuzz_structured.  Arbitrary bytes must never crash it, read out of bounds,
// or exhaust memory.
//
// format_iso8601 is fuzzed too, with an arbitrary double drawn from the raw
// input bytes.  This exercises its range and non-finite guards directly: NaN
// compares false against both bounds, so it must be rejected before the
// static_cast<std::time_t> that would otherwise be undefined behaviour.
//
// One oracle runs on top of the never-crash contract:
//   * Round-trip on the parser's own output: when parse_iso8601 accepts input
//     and the resulting instant is representable, format_iso8601 must render it
//     to canonical "...Z" text that re-parses to exactly the same instant
//     (parse(format(parse(x))) == parse(x)).  parse_iso8601 works in whole
//     seconds, so no sub-second rounding can break the identity; instants that
//     fall outside the formattable range (e.g. a zone offset pushing past
//     year 9999) simply yield no text and are skipped.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            using luma::datetime::format_iso8601;
            using luma::datetime::parse_iso8601;

            // ── Never-crash: arbitrary bytes are parsed or rejected, never crash.
            const auto parsed = parse_iso8601(input);
            luma::fuzz::do_not_optimize(parsed.success);

            // ── Never-crash: format an arbitrary bit pattern as a timestamp so the
            // range / NaN / infinity guards are driven with hostile doubles.
            double raw_seconds = 0.0;
            if (size >= sizeof(raw_seconds)) {
                std::memcpy(&raw_seconds, data, sizeof(raw_seconds));
            }
            const auto raw_text = format_iso8601(raw_seconds);
            luma::fuzz::do_not_optimize(raw_text.has_value());

            // ── Oracle: a parsed instant that can be formatted must round-trip.
            if (parsed.success) {
                const auto text = format_iso8601(parsed.unix_seconds);

                if (text) {
                    const auto reparsed = parse_iso8601(*text);

                    if (!reparsed.success || reparsed.unix_seconds != parsed.unix_seconds) {
                        luma::fuzz::trap(); // format → parse is not a faithful round-trip.
                    }
                }
            }
        });
}
