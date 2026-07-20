#ifndef LUMA_LSP_FORMATTING_HANDLER_HPP
#define LUMA_LSP_FORMATTING_HANDLER_HPP

#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

class LspFormattingHandler {
public:
    explicit LspFormattingHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_formatting(const JsonValue& params);
    [[nodiscard]] JsonValue handle_range_formatting(const JsonValue& params);

private:
    LspHandlerContext& ctx_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_FORMATTING_HANDLER_HPP
