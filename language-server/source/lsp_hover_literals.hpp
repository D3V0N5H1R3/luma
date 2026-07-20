#ifndef LUMA_LSP_HOVER_LITERALS_HPP
#define LUMA_LSP_HOVER_LITERALS_HPP

#include <cstddef>
#include <functional>
#include <string_view>
#include <unordered_map>

#include "analysis/lexer/token_type.hpp"

namespace luma::lsp {

// Hash functor for TokenType (an enum class) so it can be used as an
// unordered_map key without a manual static_cast at every call site.
struct TokenTypeHash {
    std::size_t operator()(TokenType t) const noexcept {
        return std::hash<int>{}(static_cast<int>(t));
    }
};

// Static map of literal and type-keyword token types to hover documentation.
// Extracted from lsp_server_hover.cpp so the data is declared in a single
// place and can be tested or extended independently. The definition lives in
// lsp_hover_literals.cpp to keep the data table out of every translation unit
// that includes this header.
[[nodiscard]] const std::unordered_map<TokenType, std::string_view, TokenTypeHash>&
get_literal_hover_map();

} // namespace luma::lsp

#endif // LUMA_LSP_HOVER_LITERALS_HPP
