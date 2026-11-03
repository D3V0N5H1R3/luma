#include "lsp_token_utils.hpp"

#include <algorithm>
#include <ranges>
#include <string_view>
#include <vector>

#include "analysis/lexer/token.hpp"
#include "analysis/lexer/token_type.hpp"
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

namespace {

// Whether `type` can end a type annotation (`integer`, `Point`, `array<T>`,
// `(A, B)`, `T?`).
[[nodiscard]] bool ends_type_annotation(TokenType type) {
    return type == TokenType::Identifier || is_type_keyword(type) || type == TokenType::Greater ||
           type == TokenType::GreaterGreater || type == TokenType::RightParen ||
           type == TokenType::QuestionMark;
}

// Whether the parenthesised name list holding `token_index` — `(a, b, …)`,
// containing only identifiers and commas — belongs to a binding construct:
// `for (a, b) in`, `catch (e)`, or a `case` pattern such as `Circle(r)`.
[[nodiscard]] bool in_binding_name_list(const std::vector<Token>& tokens, std::size_t token_index) {
    std::size_t open = token_index;
    while (open > 0 && (tokens[open - 1].type == TokenType::Identifier ||
                        tokens[open - 1].type == TokenType::Comma)) {
        --open;
    }
    if (open == 0 || tokens[open - 1].type != TokenType::LeftParen || open < 2) {
        return false;
    }
    const auto before_paren = tokens[open - 2].type;
    if (before_paren == TokenType::For || before_paren == TokenType::Catch) {
        return true;
    }
    // A case pattern: a `case` earlier on the same line, before any `{` (which
    // would start the arm's body, where `success(x)` constructs a value).
    for (std::size_t i = open - 1; i > 0; --i) {
        const auto& tok = tokens[i - 1];
        if (tok.type == TokenType::LeftBrace || tok.type == TokenType::RightBrace) {
            return false;
        }
        if (tok.type == TokenType::Case) {
            return true;
        }
        if (tok.preceded_by_newline) {
            return false;
        }
    }
    return false;
}

} // namespace

bool is_binding_occurrence(const std::vector<Token>& tokens, std::size_t token_index) {
    const auto& tok = tokens[token_index];
    if (tok.type != TokenType::Identifier) {
        return false;
    }
    const Token* prev = token_index > 0 ? &tokens[token_index - 1] : nullptr;
    const Token* next = token_index + 1 < tokens.size() ? &tokens[token_index + 1] : nullptr;
    if (prev == nullptr || prev->type == TokenType::Dot) {
        return false;
    }

    // `<type> name =` declarations and `(<type> name, …)` parameters: a type
    // annotation directly before the name on the same line.
    if (!tok.preceded_by_newline && ends_type_annotation(prev->type) && next != nullptr &&
        (next->type == TokenType::Equals || next->type == TokenType::Comma ||
         next->type == TokenType::RightParen)) {
        return true;
    }

    // `for name in` and `for index, name in`.
    if (prev->type == TokenType::For) {
        return true;
    }
    if (prev->type == TokenType::Comma && token_index >= 3 &&
        tokens[token_index - 2].type == TokenType::Identifier &&
        tokens[token_index - 3].type == TokenType::For) {
        return true;
    }

    return in_binding_name_list(tokens, token_index);
}

} // namespace luma::lsp
