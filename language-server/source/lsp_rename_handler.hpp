#ifndef LUMA_LSP_RENAME_HANDLER_HPP
#define LUMA_LSP_RENAME_HANDLER_HPP

#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

class LspRenameHandler {
public:
    explicit LspRenameHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_rename(const JsonValue& params);
    [[nodiscard]] JsonValue handle_prepare_rename(const JsonValue& params);
    [[nodiscard]] JsonValue handle_linked_editing_range(const JsonValue& params);

private:
    LspHandlerContext& ctx_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_RENAME_HANDLER_HPP
