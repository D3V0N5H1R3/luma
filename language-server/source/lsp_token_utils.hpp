#ifndef LUMA_LSP_TOKEN_UTILS_HPP
#define LUMA_LSP_TOKEN_UTILS_HPP

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/lexer/token.hpp"
#include "analysis/source/source_location.hpp"
#include "common/utf8.hpp"
#include "lsp_token_index.hpp"
#include "lsp_types.hpp"
#include "symbols/qualified_name.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// Token ↔ Position utilities (aggregate)
//
// TokenExtents bundles all four 0-based coordinates for a
// token.  Use token_extents() when multiple coordinates are
// needed together; use the scalar helpers below when only one
// coordinate is required.
// ═══════════════════════════════════════════════════════════

struct TokenExtents {
    int start_line_0based;
    int end_line_0based;
    int start_col_0based;
    int end_col_0based;
};

// Number of source columns a lexeme spans.  The lexer advances columns one per
// Unicode codepoint (UTF-8 continuation bytes are not counted), so a token's
// column width is its CODEPOINT count — not its byte length.  Using
// `lexeme.size()` (bytes) shifts the start column left for any lexeme that
// contains multi-byte UTF-8 (e.g. a string literal or non-ASCII identifier),
// producing wrong hover/definition/rename ranges and edit corruption.
[[nodiscard]] inline constexpr int lexeme_column_width(std::string_view lexeme) noexcept {
    return luma::utf8_codepoint_count(lexeme);
}

// Returns the 0-based LSP extents (line/col) for a token.
[[nodiscard]] inline constexpr TokenExtents token_extents(const Token& token) {
    const int end_line0 = token.location.line - 1;
    const int end_col = token.location.column - 1;
    // Prefer the true source span recorded at lex time.  String literals store a
    // PROCESSED lexeme (quotes stripped, escapes resolved, triple-quoted bodies
    // dedented), so reconstructing the start from the lexeme width is wrong for
    // them and, for multi-line strings, yields a large NEGATIVE start column.
    if (token.start_location.line != 0) {
        return {token.start_location.line - 1, end_line0, token.start_location.column - 1, end_col};
    }
    const int start_col = end_col - lexeme_column_width(token.lexeme);
    return {end_line0, end_line0, start_col, end_col};
}

// ═══════════════════════════════════════════════════════════
// Token ↔ Position utilities (scalar)
//
// Lightweight helpers returning individual 0-based coordinates.
// Use these when only one coordinate is needed; prefer
// token_range() when both start and end are required.
// ═══════════════════════════════════════════════════════════

[[nodiscard]] inline constexpr int token_line_0based(const Token& tok) {
    return token_extents(tok).start_line_0based;
}

[[nodiscard]] inline constexpr int token_col_end_0based(const Token& tok) {
    return token_extents(tok).end_col_0based;
}

[[nodiscard]] inline constexpr int token_col_start_0based(const Token& tok) {
    return token_extents(tok).start_col_0based;
}

// ═══════════════════════════════════════════════════════════
// Token ↔ Range utilities
//
// Convert between Luma's 1-based source locations and
// LSP's 0-based Position/Range types.
// ═══════════════════════════════════════════════════════════

// Compute a 0-based Range from a Token, spanning its true source extent.
// token.location.column is 1-based and points one past the last char; string
// tokens additionally carry a recorded start (see token_extents), so this
// correctly covers the surrounding quotes and multi-line bodies.
[[nodiscard]] inline Range token_range(const Token& tok) {
    const auto ext = token_extents(tok);
    return Range{Position{ext.start_line_0based, std::max(0, ext.start_col_0based)},
                 Position{ext.end_line_0based, std::max(0, ext.end_col_0based)}};
}

// 1-based source column of a token's first character on a single line.  Prefers
// the true start recorded at lex time (correct for string literals, whose
// processed lexeme cannot be used to reconstruct it); otherwise derives it from
// the end column and lexeme width.  For a token whose source span crosses lines
// (e.g. a triple-quoted string) the recorded start is on a different line, so
// the lexeme-width fallback is used instead — such tokens are handled per line
// by their callers.
[[nodiscard]] inline constexpr int token_start_column_1based(const Token& tok) {
    if (tok.start_location.line == tok.location.line && tok.start_location.line != 0) {
        return tok.start_location.column;
    }
    return tok.location.column - lexeme_column_width(tok.lexeme);
}

// Compute a 0-based Range for a named symbol at a SourceLocation.
// location.column is 1-based and points one past the name.
[[nodiscard]] inline Range name_range(const SourceLocation& loc, std::size_t name_length) {
    const int line0 = loc.line - 1;
    const int end_col0 = loc.column - 1;
    const int start_col0 = end_col0 - static_cast<int>(name_length);
    return Range{Position{line0, std::max(0, start_col0)}, Position{line0, std::max(0, end_col0)}};
}

// Compute a single-line 0-based Range for a name that begins at `start` and
// spans `name_length` columns. Complements the SourceLocation overload above,
// which anchors on a 1-based end column; this one anchors on the already
// 0-based start Position produced by location_to_position().
[[nodiscard]] inline Range name_range(const Position& start, int name_length) {
    return Range{start, Position{start.line, start.character + name_length}};
}

// Returns true if `token` is an identifier token.
[[nodiscard]] inline constexpr bool is_identifier(const Token& token) {
    return token.type == TokenType::Identifier;
}

// Returns true if `token` is an identifier token matching `name`.
[[nodiscard]] inline bool matches_identifier(const Token& token, std::string_view name) {
    return is_identifier(token) && token.lexeme == name;
}

// Find the first Identifier token matching 'name' near a declaration location
// and return its token_range. Falls back to a heuristic range if not found.
// Defined in lsp_token_utils.cpp to keep this scanning logic out of headers.
[[nodiscard]] Range find_identifier_range(const std::vector<Token>& tokens,
                                          const SourceLocation& decl_loc, std::string_view name);

// Find the range of a declaration's NAME given its keyword-anchored location.
// Declarations record the location of the leading keyword (`function`,
// `record`, `choice`, …), not the name that follows it, so a range built
// directly from that location covers the keyword/return-type region instead of
// the name. This scans forward for the actual name token — reducing a possibly
// qualified map key (e.g. "Ns.func") to the bare member that appears as a token
// — and returns its codepoint-safe range.
[[nodiscard]] inline Range find_declaration_name_range(const std::vector<Token>& tokens,
                                                       const SourceLocation& decl_loc,
                                                       std::string_view name) {
    return find_identifier_range(tokens, decl_loc, luma::qualified_member(name));
}

// Find the first Identifier token matching 'name' within a bounding range
// (0-based line numbers) and return its token_range.
[[nodiscard]] Range find_identifier_range_bounded(const std::vector<Token>& tokens,
                                                  std::string_view name, int start_line_0based,
                                                  int end_line_0based, Range fallback);

// Overload that uses the per-line index for O(1) line lookup instead of
// scanning from the start of the token vector.
[[nodiscard]] Range find_identifier_range_bounded(const TokenIndex& line_index,
                                                  std::string_view name, int start_line_0based,
                                                  int end_line_0based, Range fallback);

// Indexed overload of find_declaration_name_range: resolves a declaration's NAME
// range using the per-line TokenIndex, restricting the scan to the declaration's
// own line window (the keyword line plus the next few) for O(1) lookup.
//
// LIFETIME: TokenIndex holds a raw pointer into the token vector it was built
// from, so this must only be called while that index is still valid for the
// owning result — i.e. immediately after TokenIndex::build, before the result is
// copied or moved (a moved result's index dangles). The sole caller is
// build_token_index, which precomputes UserFunctionInfo::name_range in place;
// request-time code paths use the by-reference std::vector<Token> overload or
// the precomputed by-value range instead. The fallback matches the vector
// overload: a best-effort range at the keyword location when the name token is
// not found.
[[nodiscard]] inline Range find_declaration_name_range(const TokenIndex& line_index,
                                                       const SourceLocation& decl_loc,
                                                       std::string_view name) {
    const std::string_view member = luma::qualified_member(name);
    const int line0 = decl_loc.line - 1;
    const int col0 = std::max(0, decl_loc.column - 1);
    const Range fallback{Position{line0, col0},
                         Position{line0, col0 + lexeme_column_width(member)}};
    // The vector overload matches tokens on 1-based lines [decl_loc.line,
    // decl_loc.line + 3]; reproduce that window in 0-based terms.
    return find_identifier_range_bounded(line_index, member, line0, decl_loc.line + 2, fallback);
}

// ═══════════════════════════════════════════════════════════
// Namespace-qualified name helpers
//
// Look backward from an identifier token to detect a preceding
// "Namespace." prefix (Identifier Dot pattern).
// ═══════════════════════════════════════════════════════════

// Extract the namespace prefix (if any) by looking backward from the
// identifier at `identifier_index`.  Returns the namespace lexeme when the
// pattern  Identifier  Dot  <identifier_index>  is found.
[[nodiscard]] inline std::optional<std::string>
extract_namespace_prefix(const std::vector<Token>& tokens, std::size_t identifier_index) {
    if (identifier_index >= 2 && tokens[identifier_index - 1].type == TokenType::Dot &&
        is_identifier(tokens[identifier_index - 2])) {
        return tokens[identifier_index - 2].lexeme;
    }

    return std::nullopt;
}

// Check whether the token at `token_index` has a specific namespace prefix.
[[nodiscard]] inline bool has_namespace_prefix(const std::vector<Token>& tokens,
                                               std::size_t token_index,
                                               std::string_view expected_ns) {
    if (token_index < 2) {
        return false;
    }

    return tokens[token_index - 1].type == TokenType::Dot &&
           is_identifier(tokens[token_index - 2]) && tokens[token_index - 2].lexeme == expected_ns;
}

} // namespace luma::lsp

#endif // LUMA_LSP_TOKEN_UTILS_HPP
