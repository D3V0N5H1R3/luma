#ifndef LUMA_LSP_FOLDING_HANDLER_HPP
#define LUMA_LSP_FOLDING_HANDLER_HPP

#include <string>

#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

// Fallback folding used before the document has been analysed: appends a
// region fold for every multi-line `{`…`}` pair in `source`, ignoring braces
// in comments, string literals and ${...} interpolations. Defined in
// lsp_server_folding.cpp; declared here for direct unit testing.
void collect_brace_folds_from_text(const std::string& source, JsonValue::ArrayType& ranges);

class LspFoldingHandler {
public:
    explicit LspFoldingHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_folding_range(const JsonValue& params);

private:
    LspHandlerContext& ctx_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_FOLDING_HANDLER_HPP
