#include "lsp_token_utils.hpp"

#include <algorithm>
#include <ranges>
#include <string_view>
#include <vector>

#include "analysis/lexer/token.hpp"
#include "analysis/source/source_location.hpp"
#include "lsp_token_index.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

Range find_identifier_range(const std::vector<Token>& tokens, const SourceLocation& decl_loc,
                            std::string_view name) {
    const auto it = std::ranges::find_if(tokens, [&](const Token& tok) {
        return tok.location.line >= decl_loc.line && tok.location.line <= decl_loc.line + 3 &&
               matches_identifier(tok, name);
    });

    if (it != tokens.end()) {
        return token_range(*it);
    }

    // Fallback: best-effort from the declaration keyword location.
    const int line0 = decl_loc.line - 1;
    const int col0 = std::max(0, decl_loc.column - 1);
    return Range{.start = Position{.line = line0, .character = col0},
                 .end = Position{.line = line0, .character = col0 + lexeme_column_width(name)}};
}

Range find_identifier_range_bounded(const std::vector<Token>& tokens, std::string_view name,
                                    int start_line_0based, int end_line_0based, Range fallback) {
    for (const auto& tok : tokens) {
        if (tok.location.line - 1 < start_line_0based) {
            continue;
        }
        if (tok.location.line - 1 > end_line_0based) {
            break;
        }
        if (matches_identifier(tok, name)) {
            return token_range(tok);
        }
    }

    return fallback;
}

Range find_identifier_range_bounded(const TokenIndex& line_index, std::string_view name,
                                    int start_line_0based, int end_line_0based, Range fallback) {
    for (int ln = start_line_0based + 1; ln <= end_line_0based + 1; ++ln) {
        for (const auto& tok : line_index.tokens_on_line(static_cast<std::size_t>(ln))) {
            if (matches_identifier(tok, name)) {
                return token_range(tok);
            }
        }
    }

    return fallback;
}

} // namespace luma::lsp
