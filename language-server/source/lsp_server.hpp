#ifndef LUMA_LSP_SERVER_HPP
#define LUMA_LSP_SERVER_HPP

#include <atomic>
#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Forward declarations — types used only behind unique_ptr in this header.
namespace luma::lsp {
class AnalysisPipeline;
class AnalysisService;
class DocumentSynchronizer;
class LspCodeActionHandler;
class LspCompletionHandler;
class LspFoldingHandler;
class LspFormattingHandler;
class LspHierarchyHandler;
class LspHoverHandler;
class LspInlayHintHandler;
class LspNavigationHandler;
class LspRenameHandler;
class LspSemanticTokensHandler;
class LspSymbolHandler;
class LspSyncHandler;
class LspWorkspaceHandler;
} // namespace luma::lsp

#include "analysis/diagnostics/diagnostic.hpp"
#include "json/json.hpp"
#include "lsp_analysis_cache.hpp"
#include "lsp_cancellation_manager.hpp"
#include "lsp_configuration_manager.hpp"
#include "lsp_document_store.hpp"
#include "lsp_handler_context.hpp"
#include "lsp_handler_registry.hpp"
#include "lsp_pending_uri_set.hpp"
#include "lsp_semantic_token_cache.hpp"
#include "lsp_stdlib_registry.hpp"
#include "lsp_transport_wrapper.hpp"
#include "lsp_types.hpp"
#include "lsp_workspace_manager.hpp"
#include "protocol/transport.hpp"

namespace luma::lsp {

using luma::protocol::Transport;

// ─── Server creation config ───
// Passed to LspServer::create() to supply all resources needed at
// construction time.  Currently contains only the transport; future
// parameters (e.g., log sink) can be added here without changing the
// factory signature.

struct LspServerConfig {
    std::unique_ptr<Transport> transport;
};

// ═══════════════════════════════════════════════════════════════════════
// LspServer — Language Server Protocol implementation
// ═══════════════════════════════════════════════════════════════════════
//
// The server owns shared state and delegates feature handling to
// specialised handler classes (LspHoverHandler, LspCompletionHandler,
// etc.).  Each handler receives an LspHandlerContext reference that
// provides access to shared state and communication helpers.
//
// Lifecycle methods (initialize, shutdown) and message dispatch remain
// on LspServer.  Feature .cpp files define the handler class methods.
//
// Handler classes:
//   LspHoverHandler           – textDocument/hover
//   LspCompletionHandler      – textDocument/completion, resolve, signatureHelp
//   LspNavigationHandler      – definition, references, highlight, typeDefinition,
//                                implementation, documentLink, selectionRange
//   LspSymbolHandler          – documentSymbol, workspaceSymbol
//   LspFormattingHandler      – formatting, rangeFormatting
//   LspSemanticTokensHandler  – semantic tokens (full, delta, range)
//   LspRenameHandler          – rename, prepareRename, linkedEditingRange
//   LspHierarchyHandler       – call/type hierarchy
//   LspFoldingHandler         – foldingRange
//   LspCodeActionHandler      – codeAction, codeLens, executeCommand
//   LspInlayHintHandler       – inlayHint
//   LspWorkspaceHandler       – workspace features, watched files, configuration
//   LspSyncHandler            – didOpen, didChange, didClose
//
// Extracted infrastructure:
//   LspTransportWrapper       – owns transport and write mutex
//   DocumentSynchronizer      – document sync logic
//   AnalysisPipeline          – background analysis thread
//   LspHandlerContext         – shared state references for handlers
// ═══════════════════════════════════════════════════════════════════════

class LspServer {
public:
    // ═══ Configurable Limits ═══
    static constexpr std::size_t max_cached_documents_{128};

    // Creates a fully-initialised LspServer.
    // Returns nullptr if config.transport is null or any other
    // pre-condition for safe operation is not met.
    [[nodiscard]] static std::unique_ptr<LspServer> create(LspServerConfig config);

    ~LspServer() noexcept;

    LspServer(const LspServer&) = delete;
    LspServer& operator=(const LspServer&) = delete;

    // Run the server message loop until shutdown/exit.
    // Returns the process exit code (0 after clean shutdown, 1 otherwise).
    [[nodiscard]] int run();

private:
    // Constructor called only from create() — all validation happens there.
    explicit LspServer(std::unique_ptr<Transport> transport);

    // ═══ Dispatch (lsp_server_dispatch.cpp) ═══
    void register_handlers();
    void dispatch_request(const JsonValue& id, const std::string& method, const JsonValue& params);
    [[nodiscard]] bool dispatch_notification(const std::string& method, const JsonValue& params);

    // ═══ Lifecycle (lsp_server_lifecycle.cpp) ═══
    [[nodiscard]] JsonValue handle_initialize(const JsonValue& params);
    void handle_initialized();
    [[nodiscard]] JsonValue handle_shutdown();

    // ═══ Threading & Synchronization ═══
    //
    // LOCK ORDERING (always acquire in this order to prevent deadlock):
    //   1. state_mutex_          (shared or unique)
    //   2. write_mutex_
    //
    // CancellationManager has its own internal mutex, separate from the
    // lock ordering above.  It must NOT be held while acquiring
    // state_mutex_ (but the reverse is fine).
    //
    // Semantic token metadata is managed by semantic_token_cache_, which
    // has its own internal mutex and does NOT participate in the lock order
    // above.  Callers must NOT hold semantic_token_cache_'s internal lock
    // while acquiring state_mutex_ (but the reverse is fine).

    mutable std::shared_mutex state_mutex_;
    PendingUriSet pending_uris_;
    std::atomic<bool> running_{false};

    // Abort signal for in-flight analysis. Owned here — rather than by the
    // pipeline — so both the analysis service (reader) and the pipeline
    // (writer) can reference it without a circular construction dependency.
    // Declared before analysis_service_/analysis_pipeline_ so it outlives them.
    std::atomic<bool> analysis_cancel_flag_{false};

    std::thread scan_thread_;
    CancellationManager cancellation_manager_;

    // ═══ Analysis Cache ═══
    LspAnalysisCache analysis_cache_;

    // ═══ Semantic Token Cache ═══
    SemanticTokenCache semantic_token_cache_;

    // ═══ Stdlib ═══
    StdlibRegistry stdlib_registry_;

    // ═══ Workspace ═══
    WorkspaceManager workspace_;

    // ═══ Configuration State ═══
    ConfigurationManager configuration_;
    bool shutdown_requested_{false};
    std::atomic<bool> initialized_{false};
    int exit_code_{0};

    // ═══ Analysis Service ═══
    // IMPORTANT: analysis_service_ must be declared before analysis_pipeline_
    // so that the pipeline (which holds a raw pointer to the service) is
    // destroyed first in reverse-declaration-order.  Do not reorder.
    std::unique_ptr<AnalysisService> analysis_service_;

    // ═══ Document Management ═══
    DocumentStore doc_store_;

    // ═══ Extracted Components ═══
    LspTransportWrapper transport_wrapper_;
    std::unique_ptr<DocumentSynchronizer> doc_sync_;
    // Destroyed before analysis_service_ — see comment above.
    std::unique_ptr<AnalysisPipeline> analysis_pipeline_;

    // ═══ Handler Context & Instances ═══
    //
    // handler_ctx_ binds references to the shared state above and is
    // passed to every handler.  Most handlers only need the context;
    // workspace_handler_ and sync_handler_ also need runtime-constructed
    // objects (pipeline, service, doc_sync) so they are heap-allocated
    // and created in the constructor body after those objects exist.
    //
    // The explicit member-initializer list in the constructor is
    // intentional: each handler is a distinct type, so a loop or
    // macro cannot construct them without losing type safety.
    // Aggregate initialization is not possible because C++ requires
    // each aggregate element to share the same type.  The
    // one-handler-per-line style also makes it trivial to add, remove,
    // or reorder handlers without merge conflicts.
    LspHandlerContext handler_ctx_;

    std::unique_ptr<LspHoverHandler> hover_handler_;
    std::unique_ptr<LspCompletionHandler> completion_handler_;
    std::unique_ptr<LspNavigationHandler> navigation_handler_;
    std::unique_ptr<LspSymbolHandler> symbol_handler_;
    std::unique_ptr<LspFormattingHandler> formatting_handler_;
    std::unique_ptr<LspSemanticTokensHandler> semantic_tokens_handler_;
    std::unique_ptr<LspRenameHandler> rename_handler_;
    std::unique_ptr<LspHierarchyHandler> hierarchy_handler_;
    std::unique_ptr<LspFoldingHandler> folding_handler_;
    std::unique_ptr<LspCodeActionHandler> code_action_handler_;
    std::unique_ptr<LspInlayHintHandler> inlay_hint_handler_;
    std::unique_ptr<LspWorkspaceHandler> workspace_handler_;
    std::unique_ptr<LspSyncHandler> sync_handler_;

    // ═══ Handler Dispatch ═══
    LspHandlerRegistry handlers_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_SERVER_HPP
