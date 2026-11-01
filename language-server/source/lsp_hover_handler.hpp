#ifndef LUMA_LSP_HOVER_HANDLER_HPP
#define LUMA_LSP_HOVER_HANDLER_HPP

#include <cstddef>
#include <string>

#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

struct AnalysisResult;
class StdlibRegistry;

// The hover markdown for the token at `idx` of `result` (empty when there is
// nothing to show). Defined in lsp_server_hover.cpp; declared here so it can
// be unit tested without the asynchronous server.
[[nodiscard]] std::string resolve_hover_text(const AnalysisResult& result, std::size_t idx,
                                             const StdlibRegistry& stdlib_registry);

class LspHoverHandler {
public:
    explicit LspHoverHandler(LspHandlerContext& ctx) : ctx_(ctx) {}

    [[nodiscard]] JsonValue handle_hover(const JsonValue& params);

private:
    LspHandlerContext& ctx_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_HOVER_HANDLER_HPP
