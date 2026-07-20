#ifndef LUMA_LSP_HOVER_HANDLER_HPP
#define LUMA_LSP_HOVER_HANDLER_HPP

#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

class LspHoverHandler {
public:
    explicit LspHoverHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_hover(const JsonValue& params);

private:
    LspHandlerContext& ctx_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_HOVER_HANDLER_HPP
