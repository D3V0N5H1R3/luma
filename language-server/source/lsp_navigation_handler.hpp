#ifndef LUMA_LSP_NAVIGATION_HANDLER_HPP
#define LUMA_LSP_NAVIGATION_HANDLER_HPP

#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

class LspNavigationHandler {
public:
    explicit LspNavigationHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_definition(const JsonValue& params);
    [[nodiscard]] JsonValue handle_references(const JsonValue& params);
    [[nodiscard]] JsonValue handle_document_highlight(const JsonValue& params);
    [[nodiscard]] JsonValue handle_type_definition(const JsonValue& params);
    [[nodiscard]] JsonValue handle_implementation(const JsonValue& params);
    [[nodiscard]] JsonValue handle_document_link(const JsonValue& params);
    [[nodiscard]] JsonValue handle_selection_range(const JsonValue& params);

private:
    LspHandlerContext& ctx_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_NAVIGATION_HANDLER_HPP
