#ifndef LUMA_LSP_HIERARCHY_HANDLER_HPP
#define LUMA_LSP_HIERARCHY_HANDLER_HPP

#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

class LspHierarchyHandler {
public:
    explicit LspHierarchyHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_call_hierarchy_prepare(const JsonValue& params);
    [[nodiscard]] JsonValue handle_call_hierarchy_incoming(const JsonValue& params);
    [[nodiscard]] JsonValue handle_call_hierarchy_outgoing(const JsonValue& params);
    [[nodiscard]] JsonValue handle_type_hierarchy_prepare(const JsonValue& params);
    [[nodiscard]] JsonValue handle_type_hierarchy_supertypes(const JsonValue& params);
    [[nodiscard]] JsonValue handle_type_hierarchy_subtypes(const JsonValue& params);

private:
    LspHandlerContext& ctx_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_HIERARCHY_HANDLER_HPP
