#ifndef LUMA_COMMON_UTF8_ITERATOR_HPP
#define LUMA_COMMON_UTF8_ITERATOR_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "common/utf8.hpp"

namespace luma {

// ─── UTF8Iterator ───
// Stateful forward iterator over UTF-8 codepoints.  Advancing costs O(1) per
// codepoint rather than restarting from position 0, making sequential scans
// O(N) instead of O(N²).
class UTF8Iterator {
public:
    explicit UTF8Iterator(std::string_view text) noexcept
        : text_{text}, byte_offset_{0}, codepoint_index_{0} {}

    // Decode and return the codepoint at the current position, then advance.
    // An ill-formed or truncated sequence yields U+FFFD and advances by one
    // byte (see utf8_decode), so the value returned and the distance moved
    // always agree.  At the end of the text this returns U+FFFD and does not
    // move, so the byte offset never passes the end of the text.
    [[nodiscard]] char32_t next() noexcept {
        const auto decoded = utf8_decode(text_, byte_offset_);
        step(decoded.length);
        return static_cast<char32_t>(decoded.codepoint);
    }

    // Advance past the current codepoint without returning it.  Moves by the
    // same distance as next(); a no-op at the end of the text.
    void advance() noexcept {
        step(utf8_decode(text_, byte_offset_).length);
    }

    // Current byte offset into the source string.
    [[nodiscard]] std::size_t byte_offset() const noexcept {
        return byte_offset_;
    }

    // Number of codepoints consumed so far.
    [[nodiscard]] std::size_t codepoint_index() const noexcept {
        return codepoint_index_;
    }

    // True when all codepoints have been consumed.
    [[nodiscard]] bool at_end() const noexcept {
        return byte_offset_ >= text_.size();
    }

private:
    // utf8_decode never reports a length that runs past the end of the text
    // and reports 0 at the end, so the offset stays within [0, size()].
    void step(std::size_t length) noexcept {
        if (length == 0) {
            return;
        }
        byte_offset_ += length;
        ++codepoint_index_;
    }

    std::string_view text_;
    std::size_t byte_offset_;
    std::size_t codepoint_index_;
};

// Count the number of UTF-8 codepoints in `s`.  Delegates to
// utf8_codepoint_count (utf8.hpp) so the counting loop lives in a single place,
// widening the result to int64_t for callers that store codepoint counts as
// 64-bit values.  The utf8_byte_offset / utf8_codepoint_index /
// utf8_seek_to_codepoint helpers keep their own scans because their termination
// conditions (index/byte comparison vs. at_end) differ.
[[nodiscard]] inline std::int64_t utf8_count(const std::string& s) noexcept {
    return static_cast<std::int64_t>(utf8_codepoint_count(s));
}

// Convenience wrapper returning the codepoint count as std::size_t for callers
// that use it to size or index buffers.  A codepoint count is never negative,
// so narrowing utf8_count's int64_t result is value-preserving; this factors
// out the static_cast<std::size_t>(utf8_count(s)) idiom that recurs across the
// String module and VM string indexing.
[[nodiscard]] inline std::size_t utf8_count_size(const std::string& s) noexcept {
    return static_cast<std::size_t>(utf8_count(s));
}

// Return the byte offset of the `n`-th codepoint in `s`.
// Iterates until the codepoint index reaches `n`; distinct from utf8_count
// (which iterates to at_end) and utf8_codepoint_index (which iterates to
// a byte position).  The result never exceeds s.size(): an `n` past the last
// codepoint (or a truncated trailing sequence) yields s.size().
[[nodiscard]] inline std::size_t utf8_byte_offset(const std::string& s, std::int64_t n) noexcept {
    UTF8Iterator it{s};
    while (!it.at_end() && static_cast<std::int64_t>(it.codepoint_index()) < n) {
        it.advance();
    }
    return it.byte_offset();
}

// Return the codepoint index corresponding to byte position `byte_pos`.
// Iterates until the byte offset reaches `byte_pos`; distinct from
// utf8_count (which iterates to at_end) and utf8_byte_offset (which
// iterates to a codepoint index).
[[nodiscard]] inline std::int64_t utf8_codepoint_index(const std::string& s,
                                                       std::size_t byte_pos) noexcept {
    UTF8Iterator it{s};
    while (!it.at_end() && it.byte_offset() < byte_pos) {
        it.advance();
    }
    return static_cast<std::int64_t>(it.codepoint_index());
}

// Return the byte offset of the `codepoint_idx`-th codepoint in `text` using a
// single forward scan (O(N)).  Prefer this over repeated calls to
// utf8_byte_offset() which would be O(N²) across a sequence of indices.
[[nodiscard]] inline std::size_t utf8_seek_to_codepoint(std::string_view text,
                                                        std::size_t codepoint_idx) noexcept {
    UTF8Iterator it{text};
    while (!it.at_end() && it.codepoint_index() < codepoint_idx) {
        it.advance();
    }
    return it.byte_offset();
}

} // namespace luma

#endif // LUMA_COMMON_UTF8_ITERATOR_HPP
