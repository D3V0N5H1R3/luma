#include "lsp_document_synchronizer.hpp"
#include "lsp_sync_handler.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// Document synchronisation — thin wrappers delegating to DocumentSynchronizer.
// ═══════════════════════════════════════════════════════════

void LspSyncHandler::handle_did_open(const JsonValue& params) {
    doc_sync_.handle_did_open(params);
}

void LspSyncHandler::handle_did_change(const JsonValue& params) {
    doc_sync_.handle_did_change(params);
}

void LspSyncHandler::handle_did_close(const JsonValue& params) {
    doc_sync_.handle_did_close(params);
}

} // namespace luma::lsp
