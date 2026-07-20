#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <fuzzer/FuzzedDataProvider.h>

#include "common/utf8.hpp"
#include "common/utf8_iterator.hpp"
#include "fuzz_harness.hpp"

// LibFuzzer entry point for the UTF-8 string codec
// (core/common/utf8.hpp and core/common/utf8_iterator.hpp).
//
// These header-only helpers — utf8_count, utf8_advance, utf8_char_at_byte,
// utf8_decode_at, utf8_byte_offset, utf8_codepoint_index, utf8_codepoint_len,
// utf8_encode and decode_surrogate_pair, plus the UTF8Iterator they back — are
// the hand-written byte-walking core behind nearly every String.* function
// (length, byte_length, reverse, characters, character_at, substring, chunk,
// to_codepoints, from_codepoints, common_prefix/suffix, levenshtein_distance,
// truncate, pad/center, …) and the VM's for-in iteration over a string. They
// also decode the \uXXXX surrogate pairs in the stdlib JSON parser. Because the
// text they walk reaches a Luma program as string literals, file contents or
// network payloads, arbitrary — including malformed, non-UTF-8 — bytes must
// never crash them, read out of bounds, or loop forever. fuzz_structured and
// fuzz_vm only reach these helpers shallowly through sanitised string literals,
// so this direct target exercises the codec far more deeply, mirroring how
// fuzz_csv, fuzz_datetime and fuzz_encoder drive their codecs.
//
// Oracles run on top of the never-crash contract:
//   1. Partition completeness: walking the text one codepoint at a time with
//      utf8_advance and gluing the utf8_char_at_byte chunks back together must
//      reproduce the original bytes exactly, and the chunk count must equal
//      utf8_count — so the walk covers every byte once, never overshooting the
//      end or dropping a trailing fragment.
//   2. Offset/index inverse: utf8_byte_offset and utf8_codepoint_index are
//      mutual inverses (codepoint_index(byte_offset(i)) == i), byte_offset
//      reaches the end once the codepoint count is consumed — overshooting a
//      truncated trailing sequence rather than falling short — and then
//      saturates, and every per-codepoint advance is 1–4 bytes.
//   3. Encode/decode round-trip: utf8_encode rejects surrogate halves and
//      out-of-range scalars (empty result) and otherwise emits a sequence whose
//      length matches utf8_codepoint_len, decodes back to the same scalar, and
//      counts as exactly one codepoint. A fixed known-answer table pins the
//      1/2/3/4-byte boundaries and the rejected ranges against drift.
//   4. Surrogate pair: decode_surrogate_pair maps any valid high/low half pair
//      into a supplementary scalar in [U+10000, U+10FFFF] that utf8_encode then
//      round-trips.
namespace {

using luma::decode_surrogate_pair;
using luma::utf8_advance;
using luma::utf8_byte_offset;
using luma::utf8_char_at_byte;
using luma::utf8_codepoint_index;
using luma::utf8_codepoint_len;
using luma::utf8_count;
using luma::utf8_decode_at;
using luma::utf8_encode;

[[nodiscard]] bool is_surrogate(std::uint32_t cp) noexcept {
    return cp >= 0xD800 && cp <= 0xDFFF;
}

[[nodiscard]] bool is_valid_scalar(std::uint32_t cp) noexcept {
    return cp <= 0x10FFFF && !is_surrogate(cp);
}

// Verify utf8_encode / utf8_decode_at / utf8_codepoint_len agree for one scalar.
void check_codepoint(std::uint32_t cp) {
    const std::string encoded = utf8_encode(cp);

    if (!is_valid_scalar(cp)) {
        if (!encoded.empty()) {
            luma::fuzz::trap(); // surrogate or out-of-range scalar must not encode.
        }

        return;
    }

    if (encoded.empty()) {
        luma::fuzz::trap(); // a valid scalar must produce bytes.
    }

    const auto lead = static_cast<std::uint8_t>(encoded[0]);

    if (static_cast<std::size_t>(utf8_codepoint_len(lead)) != encoded.size()) {
        luma::fuzz::trap(); // leading byte disagrees with the emitted length.
    }

    if (utf8_decode_at(encoded, 0) != cp) {
        luma::fuzz::trap(); // encode → decode lost or changed the scalar.
    }

    if (utf8_count(encoded) != 1) {
        luma::fuzz::trap(); // a single scalar must encode to a single codepoint.
    }
}

// Fixed encoder boundaries: the 1/2/3/4-byte thresholds and the rejected ranges.
struct LengthAnswer {
    std::uint32_t cp;
    std::size_t bytes; // 0 means "must be rejected (empty)".
};

constexpr std::array<LengthAnswer, 12> k_length_answers{{
    {0x000000, 1},
    {0x00007F, 1},
    {0x000080, 2},
    {0x0007FF, 2},
    {0x000800, 3},
    {0x00FFFF, 3},
    {0x010000, 4},
    {0x10FFFF, 4},
    {0x00D800, 0},
    {0x00DFFF, 0},
    {0x110000, 0},
    {0xFFFFFFFF, 0},
}};

void check_length_answers() {
    for (const auto& [cp, bytes] : k_length_answers) {
        const std::string encoded = utf8_encode(cp);

        if (encoded.size() != bytes) {
            luma::fuzz::trap(); // encoder length contract drifted from the anchors.
        }
    }
}

} // namespace

extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/) {
    // The fixed length-boundary anchors do not depend on the fuzz input, so
    // verify them once at startup rather than on every iteration.
    check_length_answers();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // The whole input is the untrusted text, giving the fuzzer maximal direct
    // control over the bytes the codepoint walkers traverse.
    return luma::fuzz::run_text(data, size, luma::fuzz::max_input_size, [&](const std::string& s) {
        // ── Never-crash + Oracle 1: walk every codepoint and reassemble. ──
        const auto count = utf8_count(s);

        std::string rebuilt;
        rebuilt.reserve(s.size());

        std::int64_t steps{0};
        std::size_t pos{0};

        while (pos < s.size()) {
            const auto len = utf8_advance(s, pos);

            if (len < 1 || len > 4) {
                luma::fuzz::trap(); // a codepoint advance must be 1–4 bytes.
            }

            rebuilt += utf8_char_at_byte(s, pos);
            luma::fuzz::do_not_optimize(utf8_decode_at(s, pos));

            pos += len;
            ++steps;
        }

        if (rebuilt != s) {
            luma::fuzz::trap(); // the codepoint partition lost or reordered bytes.
        }

        if (steps != count) {
            luma::fuzz::trap(); // walking disagrees with utf8_count.
        }

        // ── Oracle 2: byte_offset / codepoint_index are mutual inverses. ──
        // Consuming every codepoint reaches the end of the string. For a
        // truncated trailing multibyte sequence the final advance steps past
        // the end (utf8_advance adds the full lead-byte length), so the terminal
        // offset may exceed s.size(); it must never fall short of it.
        const auto terminal = utf8_byte_offset(s, count);

        if (terminal < s.size()) {
            luma::fuzz::trap(); // walking every codepoint must reach the end.
        }

        if (utf8_byte_offset(s, count + 5) != terminal) {
            luma::fuzz::trap(); // byte_offset must saturate once every codepoint is consumed.
        }

        // Sample a bounded set of indices so the whole target stays O(n).
        const auto stride = (count > 16) ? (count / 16) : std::int64_t{1};

        for (std::int64_t i = 0; i <= count; i += stride) {
            const auto byte_pos = utf8_byte_offset(s, i);

            if (utf8_codepoint_index(s, byte_pos) != i) {
                luma::fuzz::trap(); // codepoint_index is not the inverse of byte_offset.
            }
        }

        // ── Oracle 3: the encoder/decoder round-trips over the scalar domain. ──
        // (The fixed length anchors are input-independent — verified once in
        // LLVMFuzzerInitialize.)
        FuzzedDataProvider fdp{data, size};

        for (int n = 0; n < 8 && fdp.remaining_bytes() >= sizeof(std::uint32_t); ++n) {
            check_codepoint(fdp.ConsumeIntegral<std::uint32_t>());
        }

        // ── Oracle 4: every surrogate pair decodes to a supplementary scalar. ──
        for (int n = 0; n < 4 && fdp.remaining_bytes() >= 2 * sizeof(std::uint16_t); ++n) {
            const auto high =
                static_cast<char32_t>(0xD800 + (fdp.ConsumeIntegral<std::uint16_t>() & 0x3FF));
            const auto low =
                static_cast<char32_t>(0xDC00 + (fdp.ConsumeIntegral<std::uint16_t>() & 0x3FF));

            const auto cp = static_cast<std::uint32_t>(decode_surrogate_pair(high, low));

            if (cp < 0x10000 || cp > 0x10FFFF) {
                luma::fuzz::trap(); // a surrogate pair must yield a supplementary scalar.
            }

            check_codepoint(cp);
        }
    });
}
