#ifndef LUMA_LSP_POSITION_UTILS_HPP
#define LUMA_LSP_POSITION_UTILS_HPP

// ═══════════════════════════════════════════════════════════════════════════
// Convenience include for shared/protocol position utilities.
//
// The actual implementation lives in shared/protocol/position_utils.hpp.
// This header re-exports those utilities into the luma::lsp namespace
// so that existing LSP code continues to compile without a direct
// dependency on the shared protocol layer path.
// ═══════════════════════════════════════════════════════════════════════════
#include <string>
#include <string_view>
#include <vector>

#include "../../shared/protocol/position_utils.hpp"
#include "analysis/source/source_location.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

// Re-export shared protocol utilities for backward compatibility.
using luma::protocol::byte_offset_to_codepoint_column;
using luma::protocol::byte_offset_to_utf16_column;
using luma::protocol::codepoint_column_to_byte_offset;
using luma::protocol::is_supplementary_sequence;
using luma::protocol::is_valid_utf8_run;
using luma::protocol::utf16_column_to_byte_offset;

// ═══════════════════════════════════════════════════════════
// Codepoint ↔ UTF-16 column encoder
//
// The Luma lexer records token columns as 1-based CODEPOINT indices, but the
// LSP wire protocol (with the default utf-16 position encoding) expects 0-based
// UTF-16 code-unit indices. The two agree for every BMP character but diverge
// on supplementary-plane characters (4-byte UTF-8 = 2 UTF-16 units), such as
// emoji or astral-plane text inside string literals and comments.
//
// The LSP keeps all token/location geometry in codepoint space internally and
// uses this encoder to translate ONLY at the protocol boundary: UTF-16 →
// codepoint for incoming request positions, and codepoint → UTF-16 for outgoing
// response Positions/Ranges. Lines are identical in both encodings, so only the
// `character` field is ever converted.
//
// Non-owning: the referenced source text and line-start offsets must outlive
// the encoder. In practice they live in AnalysisResult.metadata and are read
// under the server's read lock, so a locally-constructed encoder is safe.
// A default-constructed (null) encoder performs identity conversions, so code
// paths without source available degrade to the previous behaviour rather than
// crashing.
// ═══════════════════════════════════════════════════════════
class PositionEncoder {
public:
    PositionEncoder() = default;

    PositionEncoder(const std::string* source, const std::vector<std::size_t>* line_starts) noexcept
        : source_(source), line_starts_(line_starts) {}

    [[nodiscard]] bool valid() const noexcept {
        return source_ != nullptr && line_starts_ != nullptr;
    }

    // 0-based codepoint column on a 0-based line → 0-based UTF-16 column.
    [[nodiscard]] int to_utf16(int line0, int codepoint_col) const {
        if (codepoint_col <= 0 || !valid()) {
            return codepoint_col > 0 ? codepoint_col : 0;
        }
        const std::string_view line = line_text(line0);
        if (line.empty()) {
            return codepoint_col;
        }
        const std::size_t byte_off = codepoint_column_to_byte_offset(line, codepoint_col);
        return byte_offset_to_utf16_column(line, byte_off);
    }

    // 0-based UTF-16 column on a 0-based line → 0-based codepoint column.
    [[nodiscard]] int to_codepoint(int line0, int utf16_col) const {
        if (utf16_col <= 0 || !valid()) {
            return utf16_col > 0 ? utf16_col : 0;
        }
        const std::string_view line = line_text(line0);
        if (line.empty()) {
            return utf16_col;
        }
        const std::size_t byte_off = utf16_column_to_byte_offset(line, utf16_col);
        return byte_offset_to_codepoint_column(line, byte_off);
    }

    [[nodiscard]] Position to_utf16(const Position& p) const {
        return Position{p.line, to_utf16(p.line, p.character)};
    }

    [[nodiscard]] Position to_codepoint(const Position& p) const {
        return Position{p.line, to_codepoint(p.line, p.character)};
    }

    [[nodiscard]] Range to_utf16(const Range& r) const {
        return Range{to_utf16(r.start), to_utf16(r.end)};
    }

    [[nodiscard]] Range to_codepoint(const Range& r) const {
        return Range{to_codepoint(r.start), to_codepoint(r.end)};
    }

private:
    // Returns the text of the 0-based line (newline stripped) as a view into the
    // source. Caches the most recently requested line so the semantic-tokens hot
    // path — which converts many token columns on the same line — stays O(1)
    // after the first lookup instead of re-slicing per token.
    [[nodiscard]] std::string_view line_text(int line0) const {
        if (line0 < 0 || !valid()) {
            return {};
        }
        if (line0 == cached_line0_) {
            return cached_line_;
        }
        cached_line0_ = line0;
        cached_line_ = {};

        const auto& starts = *line_starts_;
        const auto idx = static_cast<std::size_t>(line0);
        if (idx >= starts.size()) {
            return cached_line_;
        }
        const std::string& src = *source_;
        const std::size_t start = starts[idx];
        std::size_t end = src.size();
        if (idx + 1 < starts.size()) {
            end = starts[idx + 1];
        }
        if (end > start && src[end - 1] == '\n') {
            --end;
        }
        if (end > start && src[end - 1] == '\r') {
            --end;
        }
        cached_line_ = std::string_view{src}.substr(start, end - start);
        return cached_line_;
    }

    const std::string* source_ = nullptr;
    const std::vector<std::size_t>* line_starts_ = nullptr;
    mutable int cached_line0_ = -1;
    mutable std::string_view cached_line_{};
};

// ═══════════════════════════════════════════════════════════
// LSP-specific position notes
//
// LSP positions use 0-based line and 0-based character (UTF-16 offset).
// Luma SourceLocation uses 1-based line and 1-based column (byte offset).
// All functions re-exported above operate on 0-based values (LSP convention)
// unless otherwise noted.
// ═══════════════════════════════════════════════════════════

// Convert a 1-based Luma line number to a 0-based LSP line.
// Clamps to 0 for safety.
[[nodiscard]] inline constexpr int to_0based_line(int line_1based) {
    return line_1based > 0 ? line_1based - 1 : 0;
}

// Converts a 1-based source Location to a 0-based LSP Position.
[[nodiscard]] inline constexpr Position location_to_position(const SourceLocation& loc) {
    return Position{to_0based_line(loc.line), loc.column > 0 ? loc.column - 1 : 0};
}

} // namespace luma::lsp

#endif // LUMA_LSP_POSITION_UTILS_HPP
