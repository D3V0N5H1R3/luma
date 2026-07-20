#include "lsp_server.hpp"

#include <format>
#include <optional>

#include "lsp_analysis_pipeline.hpp"
#include "lsp_analysis_service.hpp"
#include "lsp_analysis_service_impl.hpp"
#include "lsp_code_action_handler.hpp"
#include "lsp_completion_handler.hpp"
#include "lsp_document_synchronizer.hpp"
#include "lsp_folding_handler.hpp"
#include "lsp_formatting_handler.hpp"
#include "lsp_hierarchy_handler.hpp"
#include "lsp_hover_handler.hpp"
#include "lsp_inlay_hint_handler.hpp"
#include "lsp_navigation_handler.hpp"
#include "lsp_rename_handler.hpp"
#include "lsp_semantic_tokens_handler.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_symbol_handler.hpp"
#include "lsp_sync_handler.hpp"
#include "lsp_workspace_handler.hpp"
#include "protocol/error_recovery.hpp"
#include "protocol/transport_exceptions.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// Factory
// ═══════════════════════════════════════════════════════════

std::unique_ptr<LspServer> LspServer::create(LspServerConfig config) {
    if (!config.transport) {
        return nullptr;
    }
    // Constructor is private; use raw new + wrap in unique_ptr.
    return std::unique_ptr<LspServer>(new LspServer(std::move(config.transport)));
}

// ═══════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════

LspServer::LspServer(std::unique_ptr<Transport> transport)
    : transport_wrapper_(std::move(transport), initialized_),
      handler_ctx_{.state_mutex = state_mutex_,
                   .doc_store = doc_store_,
                   .analysis_cache = analysis_cache_,
                   .pending_uris = pending_uris_,
                   .stdlib_registry = stdlib_registry_,
                   .semantic_token_cache = semantic_token_cache_,
                   .configuration = configuration_,
                   .workspace = workspace_,
                   .transport_wrapper = transport_wrapper_},
      hover_handler_(std::make_unique<LspHoverHandler>(handler_ctx_)),
      completion_handler_(std::make_unique<LspCompletionHandler>(handler_ctx_)),
      navigation_handler_(std::make_unique<LspNavigationHandler>(handler_ctx_)),
      symbol_handler_(std::make_unique<LspSymbolHandler>(handler_ctx_)),
      formatting_handler_(std::make_unique<LspFormattingHandler>(handler_ctx_)),
      semantic_tokens_handler_(std::make_unique<LspSemanticTokensHandler>(handler_ctx_)),
      rename_handler_(std::make_unique<LspRenameHandler>(handler_ctx_)),
      hierarchy_handler_(std::make_unique<LspHierarchyHandler>(handler_ctx_)),
      folding_handler_(std::make_unique<LspFoldingHandler>(handler_ctx_)),
      code_action_handler_(std::make_unique<LspCodeActionHandler>(handler_ctx_)),
      inlay_hint_handler_(std::make_unique<LspInlayHintHandler>(handler_ctx_)) {
    // ── Analysis subsystem construction ──
    // The cancel flag (analysis_cancel_flag_) is owned by LspServer and shared
    // by reference with both the analysis service (reader) and the pipeline
    // (writer). Because neither subsystem owns the flag, there is no longer a
    // circular construction dependency: the service is created first, then the
    // pipeline is created with the already-built service.

    // Step 1: create the analysis service, referencing the shared cancel flag.
    analysis_service_ = std::make_unique<LspAnalysisService>(
        configuration_.config(), analysis_cancel_flag_,
        AnalysisCallbacks{
            .log = [this](const std::string& msg) { transport_wrapper_.log_message(msg); },
            .notify =
                [this](std::string_view method, const JsonValue& params) {
                    transport_wrapper_.send_notification(method, params);
                }});

    // Step 2: create the pipeline, sharing the same cancel flag and the service.
    analysis_pipeline_ = std::make_unique<AnalysisPipeline>(
        AnalysisPipeline::SharedState{.state_mutex = state_mutex_,
                                      .doc_store = doc_store_,
                                      .analysis_cache = analysis_cache_,
                                      .pending_uris = pending_uris_,
                                      .running = running_,
                                      .configuration = configuration_,
                                      .workspace = workspace_,
                                      .semantic_token_cache = semantic_token_cache_,
                                      .analysis_cancel_flag = analysis_cancel_flag_},
        AnalysisPipeline::Callbacks{
            .log_message = [this](const std::string& msg,
                                  int type) { transport_wrapper_.log_message(msg, type); },
            .publish_diagnostics =
                [this](const std::string& uri, const std::vector<Diagnostic>& diags, int ver) {
                    transport_wrapper_.publish_diagnostics(uri, diags, ver);
                },
            .compute_semantic_token_data =
                [this](const AnalysisResult& r) {
                    return semantic_tokens_handler_->compute_semantic_token_data(r);
                },
            .refresh_semantic_tokens =
                [this] {
                    transport_wrapper_.send_notification(constants::method::refresh_semantic_tokens,
                                                         JsonValue());
                }},
        analysis_service_.get());

    // Build the workspace handler (needs pipeline, service, running flag).
    workspace_handler_ = std::make_unique<LspWorkspaceHandler>(handler_ctx_, *analysis_pipeline_,
                                                               *analysis_service_, running_);

    // Build the document synchronizer.
    doc_sync_ = std::make_unique<DocumentSynchronizer>(
        DocumentSynchronizer::SharedState{.state_mutex = state_mutex_,
                                          .doc_store = doc_store_,
                                          .analysis_cache = analysis_cache_,
                                          .pending_uris = pending_uris_,
                                          .workspace = workspace_},
        DocumentSynchronizer::Callbacks{
            .schedule_analysis =
                [this](const std::string& uri) { workspace_handler_->schedule_analysis(uri); },
            .log_message = [this](const std::string& msg,
                                  int type) { transport_wrapper_.log_message(msg, type); },
            .publish_diagnostics =
                [this](const std::string& uri, const std::vector<Diagnostic>& diags, int ver) {
                    transport_wrapper_.publish_diagnostics(uri, diags, ver);
                },
            .load_background_file =
                [this](const std::string& path) {
                    workspace_handler_->load_background_file(path);
                }});

    // Build the sync handler (needs doc_sync).
    sync_handler_ = std::make_unique<LspSyncHandler>(handler_ctx_, *doc_sync_);
}

LspServer::~LspServer() noexcept {
    running_.store(false);
    analysis_pipeline_->notify();

    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }

    // Explicit destruction order: pipeline must stop before service is destroyed,
    // because the pipeline holds a raw pointer to the service.
    analysis_pipeline_->stop();
    analysis_pipeline_.reset();
    analysis_service_.reset();
}

// ═══════════════════════════════════════════════════════════
// Message loop
// ═══════════════════════════════════════════════════════════

int LspServer::run() {
    transport_wrapper_.log_message("Server starting");

    stdlib_registry_.init([this](const std::string& msg) { transport_wrapper_.log_message(msg); });
    register_handlers();

    // Start the analysis worker thread.
    running_.store(true);
    analysis_pipeline_->start();

    bool exit_requested = false;
    protocol::ErrorRecoveryState recovery;

    while (!exit_requested) {
        std::optional<JsonValue> message;

        try {
            message = transport_wrapper_.read_message();
        } catch (const protocol::ConnectionClosed&) {
            transport_wrapper_.log_message("Transport connection closed");
            break;
        } catch (const std::exception& e) {
            transport_wrapper_.log_message(std::format("Error reading message: {}", e.what()));
            auto action = recovery.on_error(protocol::classify_read_error(e));
            if (action == protocol::RecoveryAction::shutdown) {
                transport_wrapper_.log_message(
                    std::format("Shutting down after {} consecutive read errors",
                                recovery.consecutive_errors()));
                break;
            }
            continue;
        }

        if (!message.has_value()) {
            transport_wrapper_.log_message("EOF on stdin, exiting");
            break;
        }

        recovery.on_success();

        try {
            const auto& msg = *message;

            if (!msg.is_object() || !msg.has("method")) {
                continue;
            }

            const auto method = msg["method"].as_string();
            const auto params = msg.has("params") ? msg["params"] : JsonValue();

            if (msg.has("id")) {
                dispatch_request(msg["id"], method, params);
            } else {
                exit_requested = dispatch_notification(method, params);
            }
        } catch (const std::exception& e) {
            transport_wrapper_.log_message(std::format("Error handling message: {}", e.what()));
        }
    }

    // Stop the analysis worker.
    running_.store(false);
    analysis_pipeline_->stop();

    return exit_code_;
}

} // namespace luma::lsp
