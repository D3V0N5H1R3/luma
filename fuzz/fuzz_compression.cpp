#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "fuzz_harness.hpp"
#include "fuzz_oracle.hpp"
#include "runtime/stdlib/system/compression_codec.hpp"

// LibFuzzer entry point for the Compression module's codec layer
// (core/runtime/stdlib/system/compression_codec.hpp).
//
// The decoders behind Compression.inflate, Compression.gunzip and
// Compression.decode_rle parse untrusted bytes — gzip streams read from files
// or the network, deflate blobs, and run-length data.  gunzip in particular
// walks a hand-written gzip header (FEXTRA / FNAME / FCOMMENT / FHCRC) with
// manual offset arithmetic, the kind of trust-boundary parser this project
// fuzzes alongside the JSON parser and bytecode deserializer.  Arbitrary bytes
// must never crash a decoder, read out of bounds, or exhaust memory.
//
// Two oracles run on top of the never-crash contract:
//   1. Round-trip: every encoder output must decode back to the exact input.
//      decode(encode(x)) == x for deflate/inflate, gzip/gunzip and RLE.
//   2. Decoder stability: when a decoder accepts the *raw* fuzz input, feeding
//      its output back through the matching encoder and decoding again must
//      reproduce the same decoded bytes (the value is in the decoder's domain).
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            using namespace luma::compression;

            // ── Decoders: arbitrary bytes must be rejected or decoded, never crash.
            const auto raw_inflated = deflate_decompress(input);
            luma::fuzz::do_not_optimize(raw_inflated.has_value());

            const auto raw_gunzipped = gzip_decompress(input);
            luma::fuzz::do_not_optimize(raw_gunzipped.has_value());

            const auto raw_unrle = rle_decode(input);
            luma::fuzz::do_not_optimize(raw_unrle.has_value());

            // ── Oracle 1: encode → decode is the identity for every codec.
            luma::fuzz::check_roundtrip(
                input, [](const std::string& s) { return deflate_compress(s); },
                [](const std::string& s) { return deflate_decompress(s); });
            luma::fuzz::check_roundtrip(
                input, [](const std::string& s) { return gzip_compress(s); },
                [](const std::string& s) { return gzip_decompress(s); });
            luma::fuzz::check_roundtrip(
                input, [](const std::string& s) { return rle_encode(s); },
                [](const std::string& s) { return rle_decode(s); });

            // ── Oracle 2: a value the RLE decoder accepts must re-encode and decode
            //    back to the same bytes (idempotence on the decoder's own output).
            luma::fuzz::check_decoder_stable(
                input, [](const std::string& s) { return rle_encode(s); },
                [](const std::string& s) { return rle_decode(s); });
        });
}
