#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "common/crc32.hpp"
#include "common/hex_codec.hpp"
#include "fuzz_harness.hpp"

// LibFuzzer entry point for the Hash module's first-party primitives
// (core/common/crc32.hpp and core/common/hex_codec.hpp).
//
// Two pieces of code behind the Hash module are hand-written and process
// untrusted bytes directly:
//   * crc32_hash — the CRC-32/ISO-HDLC checksum behind Hash.crc32, a table loop
//     over arbitrary input bytes.
//   * to_hex / from_hex_digit — the hex codec that renders every Hash digest
//     (md5, sha1, sha256, sha512, the HMAC variants and the *_file helpers all
//     return their bytes through to_hex) and decodes a hex nibble back.
// The digest algorithms themselves delegate to the vendored mbedtls library,
// which is fuzzed upstream, so they are intentionally out of scope here; this
// target pins down the Luma-authored checksum and hex-encoding layer that sits
// alongside the Base64/URL codecs covered by fuzz_encoder.  Arbitrary bytes
// must never crash these routines, read out of bounds, or exhaust memory.
//
// Three oracles run on top of the never-crash contract:
//   1. Hex round-trip: decoding to_hex(input) one nibble pair at a time with
//      from_hex_digit must reproduce the exact input bytes.  to_hex always emits
//      a valid lowercase-hex digit, so from_hex_digit can never reject its own
//      output (a -1 nibble here is a codec defect).
//   2. Hex output shape: to_hex(input) is exactly twice as long as the input,
//      the pointer/length and string_view overloads agree byte-for-byte, and
//      every emitted character is a valid hex digit.
//   3. CRC-32 anchors: the checksum of the canonical "123456789" check string is
//      0xCBF43926 and the empty input is 0 — fixed known answers that pin the
//      polynomial, initial value and final XOR against accidental change.  These
//      are input-independent, so they are verified once in LLVMFuzzerInitialize
//      rather than on every iteration.
extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/) {
    // ── Oracle 3: fixed CRC-32 known answers. ──
    if (luma::crc32_hash("123456789") != 0xCBF43926U || luma::crc32_hash(std::string{}) != 0U) {
        luma::fuzz::trap(); // CRC-32 constants drifted from the standard.
    }
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return luma::fuzz::run_text(
        data, size, luma::fuzz::max_input_size, [&](const std::string& input) {
            using luma::crc32_hash;
            using luma::from_hex_digit;
            using luma::to_hex;

            // ── Never-crash: arbitrary bytes are checksummed and hex-encoded. ──
            luma::fuzz::do_not_optimize(crc32_hash(input));

            const auto hex = to_hex(std::string_view{input});
            luma::fuzz::do_not_optimize(hex);

            // ── Oracle 2: output shape and overload agreement. ──
            if (hex.size() != input.size() * 2) {
                luma::fuzz::trap(); // hex output is not twice the input length.
            }

            const auto hex_ptr =
                to_hex(reinterpret_cast<const unsigned char*>(input.data()), input.size());
            if (hex_ptr != hex) {
                luma::fuzz::trap(); // the to_hex overloads disagree.
            }

            // ── Oracle 1: hex round-trip reproduces the input exactly. ──
            std::string decoded;
            decoded.reserve(input.size());
            for (std::size_t i{0}; i < hex.size(); i += 2) {
                const int high = from_hex_digit(hex[i]);
                const int low = from_hex_digit(hex[i + 1]);
                if (high < 0 || low < 0) {
                    luma::fuzz::trap(); // to_hex emitted a character from_hex_digit rejects.
                }
                decoded.push_back(static_cast<char>((high << 4) | low));
            }

            if (decoded != input) {
                luma::fuzz::trap(); // hex encode/decode is not a faithful round-trip.
            }

            // Oracle 3 (fixed CRC-32 anchors) is input-independent and verified
            // once in LLVMFuzzerInitialize.
        });
}
