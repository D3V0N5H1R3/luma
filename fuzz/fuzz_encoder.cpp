#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "common/base64_codec.hpp"
#include "common/url_codec.hpp"
#include "fuzz_harness.hpp"
#include "fuzz_oracle.hpp"

// LibFuzzer entry point for the Encoder module's Base64 and URL codecs
// (core/common/base64_codec.hpp and core/common/url_codec.hpp).
//
// The decoders behind Encoder.decode_base64, Encoder.decode_base64url and
// Encoder.decode_url parse untrusted text — Base64 blobs and percent-encoded
// strings read from files, HTTP traffic or user input.  base64_decode_with
// walks the input four characters at a time, implicitly padding a short tail
// and rejecting stray alphabet characters; url_decode scans for '%XX' escapes
// with manual two-character look-ahead and folds '+' to space.  Both sit
// alongside the JSON, CSV, datetime and compression decoders as directly-fuzzed
// trust-boundary parsers rather than being reached only indirectly through the
// VM by fuzz_structured.  Arbitrary bytes must never crash them, read out of
// bounds, or exhaust memory.
//
// Two oracles run on top of the never-crash contract:
//   1. Round-trip: every encoder output must decode back to the exact input.
//      decode(encode(x)) == x for Base64, Base64URL and URL percent-encoding.
//      The URL encoder never emits '+', so the decoder's '+'-to-space fold can
//      never perturb the identity; the Base64 alphabets are a strict superset
//      of what each decoder accepts, so a freshly encoded blob always decodes.
//   2. Decoder stability: when a decoder accepts the *raw* fuzz input, feeding
//      its output back through the matching encoder and decoding again must
//      reproduce the same decoded bytes (idempotence on the decoder's own
//      output).  This pins down the lenient Base64 decoder, which canonicalises
//      unpadded or mid-stream-padded input on the round-trip.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            using luma::base64_decode;
            using luma::base64_encode;
            using luma::base64url_decode;
            using luma::base64url_encode;
            using luma::url_decode;
            using luma::url_encode;

            // ── Never-crash: arbitrary bytes are decoded or rejected, never crash.
            const auto raw_b64 = base64_decode(input);
            luma::fuzz::do_not_optimize(raw_b64.has_value());

            const auto raw_b64url = base64url_decode(input);
            luma::fuzz::do_not_optimize(raw_b64url.has_value());

            const auto raw_url = url_decode(input);
            luma::fuzz::do_not_optimize(raw_url.has_value());

            // ── Oracle 1: encode → decode is the identity for every codec.
            luma::fuzz::check_roundtrip(
                input, [](const std::string& s) { return base64_encode(s); },
                [](const std::string& s) { return base64_decode(s); });
            luma::fuzz::check_roundtrip(
                input, [](const std::string& s) { return base64url_encode(s); },
                [](const std::string& s) { return base64url_decode(s); });

            const auto url = url_encode(input);
            const auto reurl = url_decode(url);
            if (!reurl.has_value() || *reurl != input) {
                luma::fuzz::trap(); // url encode/decode is not a faithful round-trip.
            }

            // ── Oracle 2: a value a decoder accepts must re-encode and decode back
            //    to the same bytes (idempotence on the decoder's own output).
            luma::fuzz::check_decoder_stable(
                input, [](const std::string& s) { return base64_encode(s); },
                [](const std::string& s) { return base64_decode(s); });
            luma::fuzz::check_decoder_stable(
                input, [](const std::string& s) { return base64url_encode(s); },
                [](const std::string& s) { return base64url_decode(s); });

            if (raw_url.has_value()) {
                const auto reencoded = url_encode(*raw_url);
                const auto second = url_decode(reencoded);
                if (!second.has_value() || *second != *raw_url) {
                    luma::fuzz::trap();
                }
            }
        });
}
