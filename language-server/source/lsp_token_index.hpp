#ifndef LUMA_LSP_TOKEN_INDEX_HPP
#define LUMA_LSP_TOKEN_INDEX_HPP

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "analysis/lexer/token.hpp"
#include "common/utf8.hpp"

namespace luma::lsp {

// Per-line index into a token vector for O(1) line lookup.
//
// Built once after lexing and reused for all position-based queries
// within the same analysis result (completion resolve, hover, brace
// matching, etc.).
//
// Tokens are assumed to be in source order (non-decreasing line numbers),
// which is guaranteed by the lexer.  The index stores [start, end) byte
// offsets into the token vector for each 1-based line number.
class TokenIndex {
public:
    // Build the per-line index from a token vector.
    // The token vector must outlive this index.
    void build(const std::vector<Token>& tokens) {
        tokens_ = &tokens;
        line_ranges_.clear();

        if (tokens.empty()) {
            return;
        }

        // Find the maximum line number.
        int max_line{0};
        for (const auto& tok : tokens) {
            if (tok.location.line > max_line) {
                max_line = tok.location.line;
            }
        }

        // Resize to max_line + 1 (index 0 unused; lines are 1-based).
        line_ranges_.resize(static_cast<std::size_t>(max_line) + 1, {0, 0});

        // Single pass: record [start, end) for each line.
        // Tokens are in source order, so line numbers are non-decreasing.
        int current_line{-1};
        for (std::size_t i{0}; i < tokens.size(); ++i) {
            const int line = tokens[i].location.line;
            if (line != current_line) {
                // Close the previous line's range (if any).
                if (current_line > 0) {
                    line_ranges_[static_cast<std::size_t>(current_line)].second = i;
                }
                // Open the new line's range.
                if (line > 0 && static_cast<std::size_t>(line) < line_ranges_.size()) {
                    line_ranges_[static_cast<std::size_t>(line)].first = i;
                }
                current_line = line;
            }
        }

        // Close the last line.
        if (current_line > 0) {
            line_ranges_[static_cast<std::size_t>(current_line)].second = tokens.size();
        }
    }

    // Return all tokens on the given 1-based line as a contiguous span.
    // Returns an empty span if the line has no tokens or is out of range.
    [[nodiscard]] std::span<const Token> tokens_on_line(std::size_t line_1based) const {
        if (!tokens_ || line_1based >= line_ranges_.size()) {
            return {};
        }

        const auto [start, end] = line_ranges_[line_1based];
        if (start >= end) {
            return {};
        }

        return std::span<const Token>(tokens_->data() + start, end - start);
    }

    // Find the token at a specific 1-based (line, column) position.
    // column_1based is compared against token start (inclusive) and end (exclusive).
    // Returns nullptr if no token contains the position.
    [[nodiscard]] const Token* token_at_position(std::size_t line_1based,
                                                 std::size_t column_1based) const {
        const auto line_tokens = tokens_on_line(line_1based);
        const int col = static_cast<int>(column_1based);

        for (const auto& tok : line_tokens) {
            const int col_end = tok.location.column;
            // Prefer the true source start recorded at lex time.  String tokens
            // carry a PROCESSED lexeme (quotes stripped, escapes resolved,
            // triple-quoted bodies dedented), so their start cannot be
            // reconstructed from the lexeme width.  Only trust the recorded start
            // when it lies on this same line.
            const int col_start =
                (tok.start_location.line == tok.location.line && tok.start_location.line != 0)
                    ? tok.start_location.column
                    : col_end - utf8_codepoint_count(tok.lexeme);
            if (col >= col_start && col < col_end) {
                return &tok;
            }
        }

        return nullptr;
    }

    // Return the index range [start, end) into the token vector for a 1-based line.
    // Useful when callers need token indices rather than token references.
    [[nodiscard]] std::pair<std::size_t, std::size_t> index_range(std::size_t line_1based) const {
        if (line_1based >= line_ranges_.size()) {
            return {0, 0};
        }
        return line_ranges_[line_1based];
    }

    // Return the first token index for a given 1-based line.
    // Useful for binary-search-style lookups that need a starting offset.
    [[nodiscard]] std::size_t first_index_on_line(std::size_t line_1based) const {
        if (line_1based >= line_ranges_.size()) {
            return tokens_ ? tokens_->size() : 0;
        }
        return line_ranges_[line_1based].first;
    }

    // Whether the index has been built.
    [[nodiscard]] bool empty() const {
        return tokens_ == nullptr || line_ranges_.empty();
    }

    // The maximum 1-based line number in the index.
    [[nodiscard]] std::size_t max_line() const {
        return line_ranges_.empty() ? 0 : line_ranges_.size() - 1;
    }

private:
    // line_ranges_[line] = {start, end} indices into *tokens_ (1-based line, index 0 unused).
    std::vector<std::pair<std::size_t, std::size_t>> line_ranges_;
    const std::vector<Token>* tokens_ = nullptr;
};

} // namespace luma::lsp

#endif // LUMA_LSP_TOKEN_INDEX_HPP
