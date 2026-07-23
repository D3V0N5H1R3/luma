#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "common/decimal.hpp"
#include "fuzz_harness.hpp"

// LibFuzzer entry point for the exact base-10 decimal parser
// (core/common/decimal.hpp).
//
// Decimal::parse is the trust-boundary reader behind Decimal.from_string — a
// hand-written scanner that walks untrusted text from Luma strings, pulling an
// optional sign, an integer part, an optional fractional part and an optional
// "eNN" exponent out of the bytes by hand under a digit-count limit
// (k_max_digits).  It sits alongside the JSON, CSV, XML, DateTime and
// compression decoders as a directly-fuzzed codec.  Arbitrary bytes must never
// crash it, read out of bounds, or exhaust memory.
//
// from_double is fuzzed too, with an arbitrary double drawn from the raw input
// bytes.  This drives its NaN / infinity guards directly (both must yield
// nullopt rather than feed a non-finite value into std::to_chars).
//
// One oracle runs on top of the never-crash contract:
//   * Round-trip on the parser's own output: whenever parse (or from_double)
//     yields a value, its canonical to_string() text must parse again to an
//     equal value (parse(to_string(x)) == x).  Equality is value-based and
//     scale-insensitive, so a canonical rendering that failed to re-parse — or
//     re-parsed to a different value — would be a genuine round-trip bug.
namespace {

// Assert that a decimal's canonical text re-parses to an equal value.
void check_roundtrip(const luma::Decimal& value) {
    const std::string text = value.to_string();
    const auto reparsed = luma::Decimal::parse(text);

    if (!reparsed.has_value() || !value.equals(*reparsed)) {
        luma::fuzz::trap(); // Canonical to_string() failed to round-trip.
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(data, size, luma::fuzz::max_input_size,
                                [&](const std::string& input) {
                                    // ── Never-crash: arbitrary bytes are parsed or rejected, never crash.
                                    const auto parsed = luma::Decimal::parse(input);
                                    luma::fuzz::do_not_optimize(parsed.has_value());

                                    // ── Oracle: an accepted value's canonical text must round-trip.
                                    if (parsed) {
                                        check_roundtrip(*parsed);
                                    }

                                    // ── Never-crash: build a decimal from a hostile double bit pattern
                                    // so the NaN / infinity guards are driven directly.
                                    double raw = 0.0;
                                    if (size >= sizeof(raw)) {
                                        std::memcpy(&raw, data, sizeof(raw));
                                    }
                                    const auto from_double = luma::Decimal::from_double(raw);
                                    luma::fuzz::do_not_optimize(from_double.has_value());

                                    // ── Oracle: a value from a finite double must also round-trip.
                                    if (from_double) {
                                        check_roundtrip(*from_double);
                                    }
                                });
}
