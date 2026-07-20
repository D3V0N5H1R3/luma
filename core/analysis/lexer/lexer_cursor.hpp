#ifndef LUMA_LEXER_LEXER_CURSOR_HPP
#define LUMA_LEXER_LEXER_CURSOR_HPP

#include <cstddef>
#include <string>
#include <utility>

#include "analysis/source/source_location.hpp"
#include "common/utf8.hpp"

namespace luma {

// ─────────────────────────────────────────────────────────────────────────────
// LexerCursor — source buffer + scanning position
// ─────────────────────────────────────────────────────────────────────────────
// Single responsibility: own the source text and the current scan position
// (byte offset, line, and Unicode-code-point column), and provide the low-level
// character-stream primitives (peeking, advancing, conditional match) that the
// tokenization rules build upon.
//
// Extracted from Lexer so that cursor/position tracking is decoupled from the
// tokenization rules.  The Lexer holds one LexerCursor and exposes the same
// primitive methods (is_at_end/current/peek_next/advance/match/current_location)
// by delegating to it, so scanner code is unaffected.
//
// Mutation happens only through advance() (and match(), which calls advance()),
// keeping line/column accounting in one place.
// ─────────────────────────────────────────────────────────────────────────────
class LexerCursor {
public:
    explicit LexerCursor(std::string source) : source_{std::move(source)} {}

    // ── Position queries ───────────────────────────────────────────────────
    [[nodiscard]] bool is_at_end() const noexcept {
        return position_ >= source_.size();
    }

    [[nodiscard]] std::size_t position() const noexcept {
        return position_;
    }

    [[nodiscard]] const std::string& source() const noexcept {
        return source_;
    }

    [[nodiscard]] int line() const noexcept {
        return line_;
    }

    [[nodiscard]] int column() const noexcept {
        return column_;
    }

    // ── Character peeking ──────────────────────────────────────────────────
    [[nodiscard]] char current() const {
        if (is_at_end()) {
            return '\0';
        }

        return source_[position_];
    }

    [[nodiscard]] char peek_next() const {
        if (position_ + 1 >= source_.size()) {
            return '\0';
        }

        return source_[position_ + 1];
    }

    // ── Advancing ──────────────────────────────────────────────────────────
    // Consume and return the current character, updating line/column.
    char advance() {
        const char character = current();

        ++position_;

        if (character == '\n') {
            ++line_;
            column_ = 1;
        } else if (!is_utf8_continuation(static_cast<unsigned char>(character))) {
            // Count columns in Unicode code points: a code-point start (ASCII
            // or a UTF-8 leading byte) advances the column, while continuation
            // bytes (0x80..0xBF) do not.  For wide characters (e.g. CJK, emoji)
            // this may differ from the display column reported by a terminal or
            // editor.
            ++column_;
        }

        return character;
    }

    // Consume the current character only if it equals `expected`.
    [[nodiscard]] bool match(char expected) {
        if (is_at_end() || current() != expected) {
            return false;
        }

        advance();

        return true;
    }

    // ── Source location ────────────────────────────────────────────────────
    [[nodiscard]] SourceLocation location(FileId file_id) const {
        return {.file_id = file_id, .line = line_, .column = column_};
    }

private:
    std::string source_;
    std::size_t position_{0};
    int line_{1};
    int column_{1};
};

} // namespace luma

#endif // LUMA_LEXER_LEXER_CURSOR_HPP
