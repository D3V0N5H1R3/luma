// UTF-8 utility function unit tests.

#include <cstdint>
#include <string>

#include "common/utf8.hpp"
#include "common/utf8_iterator.hpp"
#include "test_framework.hpp"

using namespace luma;

// ═══════════════════════════════════════════════════════════
// utf8_codepoint_len
// ═══════════════════════════════════════════════════════════

static void test_codepoint_len_ascii() {
    ASSERT_EQ(utf8_codepoint_len('A'), 1);
    ASSERT_EQ(utf8_codepoint_len(0x00), 1);
    ASSERT_EQ(utf8_codepoint_len(0x7F), 1);
}

static void test_codepoint_len_two_byte() {
    ASSERT_EQ(utf8_codepoint_len(0xC2), 2); // Start of 2-byte sequence.
    ASSERT_EQ(utf8_codepoint_len(0xDF), 2); // End of 2-byte range.
}

static void test_codepoint_len_three_byte() {
    ASSERT_EQ(utf8_codepoint_len(0xE0), 3);
    ASSERT_EQ(utf8_codepoint_len(0xEF), 3);
}

static void test_codepoint_len_four_byte() {
    ASSERT_EQ(utf8_codepoint_len(0xF0), 4);
    ASSERT_EQ(utf8_codepoint_len(0xF4), 4);
}

static void test_codepoint_len_invalid() {
    // Invalid continuation byte treated as single byte.
    ASSERT_EQ(utf8_codepoint_len(0x80), 1);
    ASSERT_EQ(utf8_codepoint_len(0xFE), 1);
}

// ═══════════════════════════════════════════════════════════
// utf8_codepoint_count — constexpr codepoint counter over a
// string_view (distinct from the iterator-based utf8_count,
// which is runtime-only and takes a std::string).
// ═══════════════════════════════════════════════════════════

static void test_codepoint_count_ascii() {
    ASSERT_EQ(utf8_codepoint_count("hello"), 5);
    ASSERT_EQ(utf8_codepoint_count(""), 0);
}

static void test_codepoint_count_multibyte() {
    // "café" — 5 bytes, 4 codepoints (é = U+00E9, 2 bytes).
    ASSERT_EQ(utf8_codepoint_count("caf\xC3\xA9"), 4);
    // "Aä€😀" — 10 bytes, 4 codepoints.
    ASSERT_EQ(utf8_codepoint_count("A\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80"), 4);
    // Single emoji U+1F600 — 4 bytes, 1 codepoint.
    ASSERT_EQ(utf8_codepoint_count("\xF0\x9F\x98\x80"), 1);
}

static void test_codepoint_count_bare_continuation() {
    // Bare continuation bytes advance one each (no infinite loop), matching
    // utf8_codepoint_len's single-byte fallback.
    const std::string s{static_cast<char>(0x80), static_cast<char>(0xBF)};
    ASSERT_EQ(utf8_codepoint_count(s), 2);
}

static void test_codepoint_count_is_constexpr() {
    // Must be usable in a constant expression — token_extents() in the
    // language server is constexpr and relies on this.
    static_assert(utf8_codepoint_count("caf\xC3\xA9") == 4);
    static_assert(utf8_codepoint_count("") == 0);
}

// ═══════════════════════════════════════════════════════════
// utf8_count
// ═══════════════════════════════════════════════════════════

static void test_count_ascii() {
    ASSERT_EQ(utf8_count("hello"), 5);
}

static void test_count_empty() {
    ASSERT_EQ(utf8_count(""), 0);
}

static void test_count_multibyte() {
    // "Ä" = 2 bytes (U+00C4), "€" = 3 bytes (U+20AC).
    std::string s = "\xC3\x84"; // Ä
    ASSERT_EQ(utf8_count(s), 1);

    std::string euro = "\xE2\x82\xAC"; // €
    ASSERT_EQ(utf8_count(euro), 1);
}

static void test_count_mixed() {
    // "Aä" = 1 ASCII + 1 two-byte = 3 bytes, 2 codepoints.
    std::string s = "A\xC3\xA4";
    ASSERT_EQ(utf8_count(s), 2);
}

// ═══════════════════════════════════════════════════════════
// utf8_byte_offset
// ═══════════════════════════════════════════════════════════

static void test_byte_offset_ascii() {
    ASSERT_EQ(utf8_byte_offset("hello", 0), 0U);
    ASSERT_EQ(utf8_byte_offset("hello", 3), 3U);
    ASSERT_EQ(utf8_byte_offset("hello", 5), 5U);
}

static void test_byte_offset_multibyte() {
    // "äb" = 2-byte + 1-byte = byte offsets: ä at 0, b at 2.
    std::string s = "\xC3\xA4"
                    "b";
    ASSERT_EQ(utf8_byte_offset(s, 0), 0U);
    ASSERT_EQ(utf8_byte_offset(s, 1), 2U); // 'b' starts at byte 2.
}

// ═══════════════════════════════════════════════════════════
// utf8_codepoint_index
// ═══════════════════════════════════════════════════════════

static void test_codepoint_index_ascii() {
    ASSERT_EQ(utf8_codepoint_index("hello", 0), 0);
    ASSERT_EQ(utf8_codepoint_index("hello", 3), 3);
}

static void test_codepoint_index_multibyte() {
    // "äb" = ä(2 bytes) + b(1 byte).
    std::string s = "\xC3\xA4"
                    "b";
    ASSERT_EQ(utf8_codepoint_index(s, 0), 0); // byte 0 = codepoint 0.
    ASSERT_EQ(utf8_codepoint_index(s, 2), 1); // byte 2 = codepoint 1 ('b').
}

// ═══════════════════════════════════════════════════════════
// utf8_char_at_byte
// ═══════════════════════════════════════════════════════════

static void test_char_at_byte_ascii() {
    ASSERT_EQ(utf8_char_at_byte("hello", 0), "h");
    ASSERT_EQ(utf8_char_at_byte("hello", 4), "o");
}

static void test_char_at_byte_multibyte() {
    std::string s = "\xC3\xA4"
                    "b";                            // äb
    ASSERT_EQ(utf8_char_at_byte(s, 0), "\xC3\xA4"); // ä (2 bytes).
    ASSERT_EQ(utf8_char_at_byte(s, 2), "b");
}

static void test_char_at_byte_out_of_range() {
    ASSERT_EQ(utf8_char_at_byte("abc", 10), "");
}

// ═══════════════════════════════════════════════════════════
// utf8_decode_at
// ═══════════════════════════════════════════════════════════

static void test_decode_ascii() {
    ASSERT_EQ(utf8_decode_at(std::string_view("A"), 0), static_cast<std::uint32_t>('A'));
}

static void test_decode_two_byte() {
    std::string s = "\xC3\xA4"; // ä = U+00E4
    ASSERT_EQ(utf8_decode_at(s, 0), 0x00E4U);
}

static void test_decode_three_byte() {
    std::string s = "\xE2\x82\xAC"; // € = U+20AC
    ASSERT_EQ(utf8_decode_at(s, 0), 0x20ACU);
}

static void test_decode_four_byte() {
    std::string s = "\xF0\x9F\x98\x80"; // 😀 = U+1F600
    ASSERT_EQ(utf8_decode_at(s, 0), 0x1F600U);
}

// ═══════════════════════════════════════════════════════════
// utf8_encode
// ═══════════════════════════════════════════════════════════

static void test_encode_ascii() {
    ASSERT_EQ(utf8_encode('A'), "A");
}

static void test_encode_two_byte() {
    std::string expected = "\xC3\xA4"; // ä = U+00E4
    ASSERT_EQ(utf8_encode(0x00E4), expected);
}

static void test_encode_three_byte() {
    std::string expected = "\xE2\x82\xAC"; // € = U+20AC
    ASSERT_EQ(utf8_encode(0x20AC), expected);
}

static void test_encode_four_byte() {
    std::string expected = "\xF0\x9F\x98\x80"; // 😀 = U+1F600
    ASSERT_EQ(utf8_encode(0x1F600), expected);
}

static void test_encode_invalid_codepoint() {
    // Surrogate range is invalid.
    ASSERT_EQ(utf8_encode(0xD800), "");
    // Above U+10FFFF.
    ASSERT_EQ(utf8_encode(0x110000), "");
}

// ═══════════════════════════════════════════════════════════
// Round-trip: encode → decode
// ═══════════════════════════════════════════════════════════

static void test_round_trip() {
    const std::uint32_t codepoints[] = {0x41, 0xE4, 0x20AC, 0x1F600};

    for (auto cp : codepoints) {
        auto encoded = utf8_encode(cp);
        auto decoded = utf8_decode_at(encoded, 0);
        ASSERT_EQ(decoded, cp);
    }
}

// ═══════════════════════════════════════════════════════════
// Invalid UTF-8 sequences
// ═══════════════════════════════════════════════════════════

static void test_codepoint_len_continuation_without_start() {
    // Bare continuation bytes (0x80..0xBF) are invalid leading bytes.
    ASSERT_EQ(utf8_codepoint_len(0x80), 1);
    ASSERT_EQ(utf8_codepoint_len(0xBF), 1);
    ASSERT_EQ(utf8_codepoint_len(0xA0), 1);
}

static void test_codepoint_len_invalid_bytes() {
    // 0xFE and 0xFF are never valid in UTF-8.
    ASSERT_EQ(utf8_codepoint_len(0xFE), 1);
    ASSERT_EQ(utf8_codepoint_len(0xFF), 1);
    // 0xF5..0xF7 still match the 4-byte leading pattern (11110xxx) so the
    // function reports 4 (it checks byte patterns, not Unicode validity).
    ASSERT_EQ(utf8_codepoint_len(0xF5), 4);
    ASSERT_EQ(utf8_codepoint_len(0xFD), 1);
}

static void test_decode_truncated_two_byte() {
    // Two-byte start byte with no continuation — falls back to raw byte.
    std::string s{static_cast<char>(0xC3)};
    ASSERT_EQ(utf8_decode_at(s, 0), 0xC3U);
}

static void test_decode_truncated_three_byte() {
    // Three-byte start byte with only one continuation.
    std::string s{static_cast<char>(0xE2), static_cast<char>(0x82)};
    ASSERT_EQ(utf8_decode_at(s, 0), 0xE2U);
}

static void test_decode_truncated_four_byte() {
    // Four-byte start byte with only two continuations.
    std::string s{static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x98)};
    ASSERT_EQ(utf8_decode_at(s, 0), 0xF0U);
}

static void test_count_bare_continuation_bytes() {
    // Each bare continuation byte is treated as a single-byte codepoint.
    std::string s{static_cast<char>(0x80), static_cast<char>(0xBF)};
    ASSERT_EQ(utf8_count(s), 2);
}

static void test_char_at_byte_truncated() {
    // Two-byte leader at end of string — should return the partial sequence.
    std::string s{static_cast<char>(0xC3)};
    ASSERT_EQ(utf8_char_at_byte(s, 0), std::string{static_cast<char>(0xC3)});
}

// ═══════════════════════════════════════════════════════════
// Surrogate pairs (must be rejected by utf8_encode)
// ═══════════════════════════════════════════════════════════

static void test_encode_surrogates_rejected() {
    // Low surrogate start (U+D800).
    ASSERT_EQ(utf8_encode(0xD800), "");
    // High surrogate end (U+DBFF).
    ASSERT_EQ(utf8_encode(0xDBFF), "");
    // Low surrogate start (U+DC00).
    ASSERT_EQ(utf8_encode(0xDC00), "");
    // Low surrogate end (U+DFFF).
    ASSERT_EQ(utf8_encode(0xDFFF), "");
    // Mid-range surrogate.
    ASSERT_EQ(utf8_encode(0xDABC), "");
}

static void test_decode_surrogate_pair_helper() {
    // decode_surrogate_pair combines a high+low pair into a codepoint.
    // U+D800 + U+DC00 => U+10000
    ASSERT_EQ(static_cast<uint32_t>(decode_surrogate_pair(0xD800, 0xDC00)), uint32_t{0x10000});
    // U+DBFF + U+DFFF => U+10FFFF
    ASSERT_EQ(static_cast<uint32_t>(decode_surrogate_pair(0xDBFF, 0xDFFF)), uint32_t{0x10FFFF});
    // U+D83D + U+DE00 => U+1F600 (😀)
    ASSERT_EQ(static_cast<uint32_t>(decode_surrogate_pair(0xD83D, 0xDE00)), uint32_t{0x1F600});
}

// ═══════════════════════════════════════════════════════════
// Boundary conditions
// ═══════════════════════════════════════════════════════════

static void test_encode_boundary_one_two_byte() {
    // U+007F is the last 1-byte codepoint.
    auto s = utf8_encode(0x007F);
    ASSERT_EQ(s.size(), 1U);
    ASSERT_EQ(utf8_decode_at(s, 0), 0x007FU);

    // U+0080 is the first 2-byte codepoint.
    s = utf8_encode(0x0080);
    ASSERT_EQ(s.size(), 2U);
    ASSERT_EQ(utf8_decode_at(s, 0), 0x0080U);
}

static void test_encode_boundary_two_three_byte() {
    // U+07FF is the last 2-byte codepoint.
    auto s = utf8_encode(0x07FF);
    ASSERT_EQ(s.size(), 2U);
    ASSERT_EQ(utf8_decode_at(s, 0), 0x07FFU);

    // U+0800 is the first 3-byte codepoint.
    s = utf8_encode(0x0800);
    ASSERT_EQ(s.size(), 3U);
    ASSERT_EQ(utf8_decode_at(s, 0), 0x0800U);
}

static void test_encode_boundary_three_four_byte() {
    // U+FFFF is the last 3-byte codepoint.
    auto s = utf8_encode(0xFFFF);
    ASSERT_EQ(s.size(), 3U);
    ASSERT_EQ(utf8_decode_at(s, 0), 0xFFFFU);

    // U+10000 is the first 4-byte codepoint.
    s = utf8_encode(0x10000);
    ASSERT_EQ(s.size(), 4U);
    ASSERT_EQ(utf8_decode_at(s, 0), 0x10000U);
}

static void test_encode_maximum_codepoint() {
    // U+10FFFF is the maximum valid codepoint.
    auto s = utf8_encode(0x10FFFF);
    ASSERT_EQ(s.size(), 4U);
    ASSERT_EQ(utf8_decode_at(s, 0), 0x10FFFFU);

    // U+110000 is above the maximum — must be rejected.
    ASSERT_EQ(utf8_encode(0x110000), "");
    ASSERT_EQ(utf8_encode(0x1FFFFF), "");
}

// ═══════════════════════════════════════════════════════════
// Empty strings and single-byte ASCII
// ═══════════════════════════════════════════════════════════

static void test_count_empty_string() {
    ASSERT_EQ(utf8_count(std::string{}), 0);
}

static void test_single_byte_ascii() {
    // NUL byte.
    ASSERT_EQ(utf8_encode(0x00), std::string(1, '\0'));

    // Printable ASCII.
    ASSERT_EQ(utf8_encode('A'), "A");
    ASSERT_EQ(utf8_encode('z'), "z");
    ASSERT_EQ(utf8_encode('0'), "0");
    ASSERT_EQ(utf8_encode(' '), " ");

    // DEL (0x7F) — last single-byte value.
    auto s = utf8_encode(0x7F);
    ASSERT_EQ(s.size(), 1U);
    ASSERT_EQ(utf8_decode_at(s, 0), 0x7FU);
}

// ═══════════════════════════════════════════════════════════
// Multi-byte sequence operations
// ═══════════════════════════════════════════════════════════

static void test_count_four_byte_chars() {
    // Two emoji: 😀😀 = 8 bytes, 2 codepoints.
    std::string s = "\xF0\x9F\x98\x80\xF0\x9F\x98\x80";
    ASSERT_EQ(utf8_count(s), 2);
}

static void test_byte_offset_four_byte() {
    // "😀b" = 4-byte emoji + 1-byte 'b'.
    std::string s = "\xF0\x9F\x98\x80"
                    "b";
    ASSERT_EQ(utf8_byte_offset(s, 0), 0U);
    ASSERT_EQ(utf8_byte_offset(s, 1), 4U); // 'b' starts at byte 4.
    ASSERT_EQ(utf8_byte_offset(s, 2), 5U); // Past the end.
}

static void test_codepoint_index_four_byte() {
    // "😀b" = emoji(4 bytes) + b(1 byte).
    std::string s = "\xF0\x9F\x98\x80"
                    "b";
    ASSERT_EQ(utf8_codepoint_index(s, 0), 0); // Byte 0 = codepoint 0.
    ASSERT_EQ(utf8_codepoint_index(s, 4), 1); // Byte 4 = codepoint 1 ('b').
}

static void test_char_at_byte_four_byte() {
    // "😀b" — first char is 4 bytes.
    std::string s = "\xF0\x9F\x98\x80"
                    "b";
    ASSERT_EQ(utf8_char_at_byte(s, 0), "\xF0\x9F\x98\x80");
    ASSERT_EQ(utf8_char_at_byte(s, 4), "b");
}

static void test_round_trip_boundaries() {
    const std::uint32_t boundary_codepoints[] = {0x00,   0x7F,   0x80,    0x07FF,
                                                 0x0800, 0xFFFF, 0x10000, 0x10FFFF};

    for (auto cp : boundary_codepoints) {
        auto encoded = utf8_encode(cp);
        ASSERT_TRUE(!encoded.empty());
        auto decoded = utf8_decode_at(encoded, 0);
        ASSERT_EQ(decoded, cp);
    }
}

// ═══════════════════════════════════════════════════════════
// is_utf8_continuation
// ═══════════════════════════════════════════════════════════

static void test_is_utf8_continuation() {
    // Continuation bytes: 0x80..0xBF.
    ASSERT_TRUE(is_utf8_continuation(0x80));
    ASSERT_TRUE(is_utf8_continuation(0xBF));
    ASSERT_TRUE(is_utf8_continuation(0xA0));

    // Non-continuation bytes.
    ASSERT_FALSE(is_utf8_continuation(0x00));
    ASSERT_FALSE(is_utf8_continuation(0x7F));
    ASSERT_FALSE(is_utf8_continuation(0xC0));
    ASSERT_FALSE(is_utf8_continuation(0xFF));
}

// ═══════════════════════════════════════════════════════════
// UTF8Iterator
// ═══════════════════════════════════════════════════════════

static void test_iterator_empty() {
    UTF8Iterator it{""};
    ASSERT_TRUE(it.at_end());
    ASSERT_EQ(it.byte_offset(), 0U);
    ASSERT_EQ(it.codepoint_index(), 0U);
}

static void test_iterator_mixed_sequence() {
    // "Aä€😀" = 1 + 2 + 3 + 4 = 10 bytes, 4 codepoints.
    std::string s = "A\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80";

    UTF8Iterator it{s};
    ASSERT_EQ(static_cast<std::uint32_t>(it.next()), static_cast<std::uint32_t>('A'));
    ASSERT_EQ(static_cast<std::uint32_t>(it.next()), 0x00E4U);  // ä
    ASSERT_EQ(static_cast<std::uint32_t>(it.next()), 0x20ACU);  // €
    ASSERT_EQ(static_cast<std::uint32_t>(it.next()), 0x1F600U); // 😀
    ASSERT_TRUE(it.at_end());
    ASSERT_EQ(it.codepoint_index(), 4U);
    ASSERT_EQ(it.byte_offset(), 10U);
}

static void test_utf8_seek_to_codepoint() {
    // "Aä€😀" = 1 + 2 + 3 + 4 = 10 bytes.
    std::string s = "A\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80";
    ASSERT_EQ(utf8_seek_to_codepoint(s, 0), 0U);
    ASSERT_EQ(utf8_seek_to_codepoint(s, 1), 1U);  // After 'A'.
    ASSERT_EQ(utf8_seek_to_codepoint(s, 2), 3U);  // After 'ä'.
    ASSERT_EQ(utf8_seek_to_codepoint(s, 3), 6U);  // After '€'.
    ASSERT_EQ(utf8_seek_to_codepoint(s, 4), 10U); // After '😀'.
}

// ─── main ───

int main() {
    // Codepoint length.
    RUN(test_codepoint_len_ascii);
    RUN(test_codepoint_len_two_byte);
    RUN(test_codepoint_len_three_byte);
    RUN(test_codepoint_len_four_byte);
    RUN(test_codepoint_len_invalid);
    RUN(test_codepoint_len_continuation_without_start);
    RUN(test_codepoint_len_invalid_bytes);

    // Count.
    RUN(test_count_ascii);
    RUN(test_count_empty);
    RUN(test_count_multibyte);
    RUN(test_count_mixed);
    RUN(test_count_empty_string);
    RUN(test_count_bare_continuation_bytes);
    RUN(test_count_four_byte_chars);

    // Codepoint count (constexpr string_view counter).
    RUN(test_codepoint_count_ascii);
    RUN(test_codepoint_count_multibyte);
    RUN(test_codepoint_count_bare_continuation);
    RUN(test_codepoint_count_is_constexpr);

    // Byte offset.
    RUN(test_byte_offset_ascii);
    RUN(test_byte_offset_multibyte);
    RUN(test_byte_offset_four_byte);

    // Codepoint index.
    RUN(test_codepoint_index_ascii);
    RUN(test_codepoint_index_multibyte);
    RUN(test_codepoint_index_four_byte);

    // Char at byte.
    RUN(test_char_at_byte_ascii);
    RUN(test_char_at_byte_multibyte);
    RUN(test_char_at_byte_out_of_range);
    RUN(test_char_at_byte_truncated);
    RUN(test_char_at_byte_four_byte);

    // Decode.
    RUN(test_decode_ascii);
    RUN(test_decode_two_byte);
    RUN(test_decode_three_byte);
    RUN(test_decode_four_byte);
    RUN(test_decode_truncated_two_byte);
    RUN(test_decode_truncated_three_byte);
    RUN(test_decode_truncated_four_byte);

    // Encode.
    RUN(test_encode_ascii);
    RUN(test_encode_two_byte);
    RUN(test_encode_three_byte);
    RUN(test_encode_four_byte);
    RUN(test_encode_invalid_codepoint);
    RUN(test_encode_surrogates_rejected);
    RUN(test_encode_boundary_one_two_byte);
    RUN(test_encode_boundary_two_three_byte);
    RUN(test_encode_boundary_three_four_byte);
    RUN(test_encode_maximum_codepoint);

    // Surrogate pair helper.
    RUN(test_decode_surrogate_pair_helper);

    // Single-byte / empty.
    RUN(test_single_byte_ascii);

    // is_utf8_continuation.
    RUN(test_is_utf8_continuation);

    // UTF8Iterator.
    RUN(test_iterator_empty);
    RUN(test_iterator_mixed_sequence);

    // utf8_seek_to_codepoint.
    RUN(test_utf8_seek_to_codepoint);

    // Round-trip.
    RUN(test_round_trip);
    RUN(test_round_trip_boundaries);
    return SUMMARY();
}
