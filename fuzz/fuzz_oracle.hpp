#pragma once

#include <string>

#include "fuzz_harness.hpp"

// Shared oracles for the std::optional-returning codec fuzz targets
// (fuzz_encoder, fuzz_compression).
//
// Both helpers are parameterised on the codec's encode/decode callables so a
// single implementation serves every optional-shaped codec pair: `encode`
// returns the encoded std::string and `decode` returns a std::optional<...>
// that is empty when the input is rejected.  This header deliberately depends on
// nothing beyond fuzz_harness.hpp so analysis-only targets can include it too.
//
// The never-crash contract (arbitrary bytes must be decoded or rejected without
// crashing) is exercised by the callers' own do_not_optimize probes; these
// helpers layer the two round-trip oracles on top of it.

namespace luma::fuzz {

// Oracle: encode → decode is the identity.  A freshly encoded blob must always
// decode, and to the exact input bytes.
template <typename Encode, typename Decode>
void check_roundtrip(const std::string& input, Encode&& encode, Decode&& decode) {
    const auto encoded = encode(input);
    const auto decoded = decode(encoded);
    if (!decoded || *decoded != input) {
        trap(); // encode → decode is not a faithful round-trip.
    }
}

// Oracle: idempotence on the decoder's own output.  When the decoder accepts a
// value, re-encoding that value and decoding again must reproduce it.  Inputs
// the decoder rejects are skipped.
template <typename Encode, typename Decode>
void check_decoder_stable(const std::string& input, Encode&& encode, Decode&& decode) {
    const auto accepted = decode(input);
    if (!accepted) {
        return;
    }

    const auto reencoded = encode(*accepted);
    const auto second = decode(reencoded);
    if (!second || *second != *accepted) {
        trap(); // decoder is not idempotent on its own output.
    }
}

} // namespace luma::fuzz
