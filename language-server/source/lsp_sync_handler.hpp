#ifndef LUMA_LSP_SYNC_HANDLER_HPP
#define LUMA_LSP_SYNC_HANDLER_HPP

#include "json/json.hpp"
#include "lsp_handler_context.hpp"

namespace luma::lsp {

class DocumentSynchronizer;

class LspSyncHandler {
public:
    LspSyncHandler(LspHandlerContext& ctx, DocumentSynchronizer& doc_sync)
        : ctx_(ctx), doc_sync_(doc_sync) {}

    void handle_did_open(const JsonValue& params);
    void handle_did_change(const JsonValue& params);
    void handle_did_close(const JsonValue& params);

private:
    LspHandlerContext& ctx_;
    DocumentSynchronizer& doc_sync_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_SYNC_HANDLER_HPP
