#ifndef LUMA_LSP_SYMBOL_HANDLER_HPP
#define LUMA_LSP_SYMBOL_HANDLER_HPP

#include <memory>
#include <vector>

#include "analysis/lexer/token.hpp"
#include "json/json.hpp"
#include "lsp_handler_context.hpp"
#include "lsp_types.hpp"

namespace luma {
struct SourceLocation;
struct Declaration;
using DeclarationPtr = std::unique_ptr<Declaration>;
} // namespace luma

namespace luma::lsp {

// Find the range of a block declaration by scanning for the opening '{' and
// the matching closing '}' in the token stream.  Falls back to a single-line
// range if no braces are found.
//
// Free function defined in lsp_server_symbols.cpp.  Shared between the symbol
// handler and the navigation handler.
[[nodiscard]] Range find_block_range(const std::vector<Token>& tokens,
                                     const SourceLocation& decl_loc);

class LspSymbolHandler {
public:
    explicit LspSymbolHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_document_symbol(const JsonValue& params);
    [[nodiscard]] JsonValue handle_workspace_symbol(const JsonValue& params);

    // Shared helpers used by other handlers (e.g., navigation).
    [[nodiscard]] Range find_block_range(const std::vector<Token>& tokens,
                                         const SourceLocation& decl_loc) const;
    [[nodiscard]] std::vector<DocumentSymbol>
    build_document_symbols(const std::vector<DeclarationPtr>& decls,
                           const std::vector<Token>& tokens) const;

private:
    LspHandlerContext& ctx_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_SYMBOL_HANDLER_HPP
