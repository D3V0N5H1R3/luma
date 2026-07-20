#ifndef LUMA_LSP_INLAY_HINT_HANDLER_HPP
#define LUMA_LSP_INLAY_HINT_HANDLER_HPP

#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

class LspInlayHintHandler {
public:
    explicit LspInlayHintHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_inlay_hint(const JsonValue& params);

private:
    LspHandlerContext& ctx_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_INLAY_HINT_HANDLER_HPP
