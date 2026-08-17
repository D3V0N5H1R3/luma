#ifndef LUMA_LSP_DOCUMENT_SYNCHRONIZER_HPP
#define LUMA_LSP_DOCUMENT_SYNCHRONIZER_HPP

#include <condition_variable>
#include <functional>
#include <shared_mutex>
#include <string>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "json/json.hpp"
#include "lsp_analysis_cache.hpp"
#include "lsp_document_store.hpp"
#include "lsp_pending_uri_set.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_workspace_manager.hpp"

namespace luma::lsp {

using luma::json::JsonValue;

// ═══════════════════════════════════════════════════════════════════════
// DocumentSynchronizer — handles textDocument/didOpen, didChange, didClose.
//
// Extracted from LspServer to isolate document synchronisation logic
// from the rest of the server.  Operates on shared state via explicit
// references and communicates with the server through callbacks.
//
// Thread safety: each handler acquires WriteStateLock internally.
// Callbacks (log, publish, schedule) may be called outside the lock.
// ═══════════════════════════════════════════════════════════════════════

class DocumentSynchronizer {
public:
    // References to LspServer shared state.
    //
    // Intentionally a smaller subset than AnalysisPipeline::SharedState or
    // LspHandlerContext — the synchronizer only needs document storage,
    // the analysis cache, the pending-URI queue, and the workspace manager.
    // Keeping this minimal avoids coupling the synchronizer to pipeline or
    // handler concerns (e.g., configuration, stdlib, transport).
    struct SharedState {
        std::shared_mutex& state_mutex;
        DocumentStore& doc_store;
        LspAnalysisCache& analysis_cache;
        PendingUriSet& pending_uris;
        WorkspaceManager& workspace;
    };

    // Callbacks for operations that remain on LspServer.
    struct Callbacks {
        std::function<void(const std::string& uri, bool force_diagnostics)> schedule_analysis;
        std::function<void(const std::string& text, int type)> log_message;
        std::function<void(const std::string& uri, const std::vector<Diagnostic>& diagnostics,
                           int version)>
            publish_diagnostics;
        std::function<void(const std::string& path)> load_background_file;
    };

    DocumentSynchronizer(SharedState state, Callbacks callbacks);

    void handle_did_open(const JsonValue& params);
    void handle_did_change(const JsonValue& params);
    void handle_did_close(const JsonValue& params);

private:
    void apply_incremental_change(WriteStateLock& state, const std::string& uri, std::string& doc,
                                  const JsonValue& change);
    void apply_full_sync(WriteStateLock& state, const std::string& uri, std::string& doc,
                         const JsonValue& change);

    SharedState state_;
    Callbacks callbacks_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_DOCUMENT_SYNCHRONIZER_HPP
