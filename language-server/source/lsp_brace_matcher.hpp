#ifndef LUMA_LSP_BRACE_MATCHER_HPP
#define LUMA_LSP_BRACE_MATCHER_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "analysis/lexer/token.hpp"
#include "lsp_token_index.hpp"

namespace luma::lsp {

// Maximum number of lines to scan backward when searching for enclosing braces.
static constexpr int k_max_backward_scan_lines = 50;

namespace detail {

// Common backward brace-scan implementation.
// Scans from `start_from` down to token index 0, returning the index of the
// first unmatched '{' whose location is in [min_line, luma_line].
[[nodiscard]] inline std::optional<std::size_t>
scan_braces_backward(const std::vector<Token>& tokens, std::size_t start_from, int luma_line,
                     int min_line) {
    int brace_depth = 0;

    for (std::size_t i = start_from; i > 0; --i) {
        const auto& tok = tokens[i - 1];
        if (tok.location.line > luma_line) {
            continue;
        }
        if (tok.location.line < min_line) {
            break;
        }
        if (tok.type == TokenType::RightBrace) {
            ++brace_depth;
        } else if (tok.type == TokenType::LeftBrace) {
            if (brace_depth == 0) {
                return i - 1;
            }
            --brace_depth;
        }
    }

    return std::nullopt;
}

} // namespace detail

// Scans backwards through `tokens` from the cursor at `luma_line` (1-based)
// looking for the nearest unclosed '{'. Stops early if the scan travels more
// than k_max_backward_scan_lines lines above `luma_line`. Returns the index of
// the opening '{' token, or std::nullopt if none is found within the search window.
[[nodiscard]] inline std::optional<std::size_t>
find_enclosing_brace_token_index(const std::vector<Token>& tokens, int luma_line) {
    const int min_line = std::max(1, luma_line - k_max_backward_scan_lines);
    return detail::scan_braces_backward(tokens, tokens.size(), luma_line, min_line);
}

// Overload that uses the per-line index to skip directly to `luma_line`
// instead of scanning backwards from the end of the token vector.
[[nodiscard]] inline std::optional<std::size_t>
find_enclosing_brace_token_index(const std::vector<Token>& tokens, int luma_line,
                                 const TokenIndex& line_index) {
    const int min_line = std::max(1, luma_line - k_max_backward_scan_lines);

    // Start scanning backwards from the last token on luma_line.
    const auto [_, end] = line_index.index_range(static_cast<std::size_t>(luma_line));
    const std::size_t start_from =
        (end > 0) ? end : line_index.first_index_on_line(static_cast<std::size_t>(luma_line));

    return detail::scan_braces_backward(tokens, start_from, luma_line, min_line);
}

// Scans the tokens from `brace_token_idx + 1` up to (and including) `luma_line`
// (1-based) and returns the set of field names that have already been assigned
// (i.e. an Identifier token immediately followed by '=').
[[nodiscard]] inline std::unordered_set<std::string>
collect_assigned_fields(const std::vector<Token>& tokens, std::size_t brace_token_idx,
                        int luma_line) {
    std::unordered_set<std::string> already_set;
    for (std::size_t i = brace_token_idx + 1; i < tokens.size(); ++i) {
        const auto& tok = tokens[i];
        if (tok.location.line > luma_line) {
            break;
        }
        if (tok.type == TokenType::Identifier && i + 1 < tokens.size() &&
            tokens[i + 1].type == TokenType::Equals) {
            already_set.insert(tok.lexeme);
        }
    }
    return already_set;
}

// Scans forward from `open_brace_idx` (which must point to a LeftBrace token)
// and returns the index of the matching RightBrace, accounting for nested
// brace pairs.  Returns std::nullopt if the closing brace is not found
// (e.g. unterminated block).
[[nodiscard]] inline std::optional<std::size_t>
find_matching_close_brace(const std::vector<Token>& tokens, std::size_t open_brace_idx) {
    int depth = 0;

    for (std::size_t i = open_brace_idx; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::LeftBrace) {
            ++depth;
        }
        if (tokens[i].type == TokenType::RightBrace) {
            --depth;
        }
        if (depth == 0) {
            return i;
        }
    }

    return std::nullopt;
}

} // namespace luma::lsp

#endif // LUMA_LSP_BRACE_MATCHER_HPP
