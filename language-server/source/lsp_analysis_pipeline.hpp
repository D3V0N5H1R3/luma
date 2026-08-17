#ifndef LUMA_LSP_ANALYSIS_PIPELINE_HPP
#define LUMA_LSP_ANALYSIS_PIPELINE_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "analysis/diagnostics/diagnostic.hpp"
#include "json/json.hpp"
#include "lsp_analysis_cache.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_analysis_service.hpp"
#include "lsp_configuration_manager.hpp"
#include "lsp_document_store.hpp"
#include "lsp_pending_uri_set.hpp"
#include "lsp_persisted_index.hpp"
#include "lsp_semantic_token_cache.hpp"
#include "lsp_workspace_manager.hpp"

namespace luma::lsp {

using luma::json::JsonValue;

// ═══════════════════════════════════════════════════════════════════════
// AnalysisPipeline — background analysis scheduling and execution.
//
// Extracted from LspServer to isolate the background analysis thread,
// debounce logic, and the three-phase analysis cycle (collect → analyse
// → commit & publish) from the rest of the server.
//
// Owns:
//   - analysis_thread_        (background worker)
//   - analysis_cv_            (condition variable for wake-up)
//
// Operates on shared state via explicit references:
//   - analysis_cancel_flag   (abort signal for in-flight analysis; owned by
//                             LspServer and shared with the analysis service)
//   - pending_uris_          (drained under state_mutex_)
//   - state_mutex_            (phases 1 & 3, unique lock)
//   - doc_store_, analysis_cache_, workspace_, etc.
//
// Thread safety: schedule_analysis() may be called from any thread.
// The analysis worker runs on its own thread and acquires locks internally.
// ═══════════════════════════════════════════════════════════════════════

class AnalysisPipeline {
public:
    // References to LspServer shared state needed by the pipeline.
    //
    // This struct is intentionally separate from DocumentSynchronizer::SharedState
    // and LspHandlerContext.  Each component references only the subset of server
    // state it actually uses:
    //   - AnalysisPipeline needs `running`, `configuration`, `semantic_token_cache`,
    //     and the shared `analysis_cancel_flag` for the background worker — these
    //     are not needed by the synchronizer.
    //   - DocumentSynchronizer uses a smaller subset (no running flag, no config).
    //   - LspHandlerContext adds stdlib_registry and transport_wrapper for handler
    //     communication, which neither the pipeline nor synchronizer require.
    //
    // Merging these into a single struct would couple unrelated components and
    // widen the interface each component depends on.
    struct SharedState {
        std::shared_mutex& state_mutex;
        DocumentStore& doc_store;
        LspAnalysisCache& analysis_cache;
        PendingUriSet& pending_uris;
        std::atomic<bool>& running;
        ConfigurationManager& configuration;
        WorkspaceManager& workspace;
        SemanticTokenCache& semantic_token_cache;
        // Abort signal for in-flight analysis, owned by LspServer and also
        // referenced by the analysis service (the reader). See the note on
        // the constructor for why this breaks the former circular dependency.
        std::atomic<bool>& analysis_cancel_flag;
    };

    // Callbacks for operations that remain on LspServer.
    struct Callbacks {
        std::function<void(const std::string& text, int type)> log_message;
        std::function<void(const std::string& uri, const std::vector<Diagnostic>& diagnostics,
                           int version)>
            publish_diagnostics;
        std::function<std::vector<int64_t>(const AnalysisResult& result)>
            compute_semantic_token_data;
        // Ask the client to re-pull semantic tokens (workspace/semanticTokens/refresh)
        // once a re-analysis has landed. The request is global and param-less, so it
        // is fired once per foreground commit to replace the stale highlighting a
        // client may hold from the debounce window.
        std::function<void()> refresh_semantic_tokens;
    };

    // Construct with shared state, callbacks, and the analysis service.
    //
    // The service pointer must remain valid for the pipeline's lifetime.
    // It is passed as a raw pointer (rather than a reference) so the
    // destructor can null it once the worker thread is joined, as a
    // defensive guard against use-after-free. The cancel flag is shared via
    // SharedState (owned by LspServer), so the service and pipeline no longer
    // have a circular construction dependency and can be created in order.
    AnalysisPipeline(SharedState state, Callbacks callbacks, AnalysisService* service);

    ~AnalysisPipeline();

    AnalysisPipeline(const AnalysisPipeline&) = delete;
    AnalysisPipeline& operator=(const AnalysisPipeline&) = delete;

    // Add a URI to the pending set and signal the worker to wake up.
    // Thread-safe — may be called from any thread.
    // When force_diagnostics is true, diagnostics are published even if the
    // diagnostics_on_save setting is enabled (used for didOpen and didSave).
    void schedule_analysis(const std::string& uri, bool force_diagnostics = false);

    // Start the background worker thread.
    void start();

    // Stop the background worker and join its thread.
    void stop();

    // Signal the analysis worker to abort current in-flight analysis.
    void request_cancellation() {
        state_.analysis_cancel_flag.store(true, std::memory_order_release);
    }

    // Notify the condition variable (used during shutdown).
    void notify() {
        analysis_cv_.notify_one();
    }

private:
    void analysis_worker();

    // Phase 1: Wait for edits to settle, then drain pending URIs.
    [[nodiscard]] std::vector<std::string> debounce_and_collect();

    // Phase 2: Run analysis on a single URI (snapshot, analyse, commit, publish).
    void analyze_single_uri(const std::string& uri);

    // Phase 3: Acquire write lock, store result, publish diagnostics.
    void commit_and_publish(const std::string& uri, AnalysisResult result,
                            std::size_t content_hash);

    // Phase 3 helpers:
    struct CommitOutcome {
        int doc_version{0};
        bool committed{false};
    };

    // Acquire write lock, verify document unchanged, store result in cache.
    CommitOutcome commit_to_cache(const std::string& uri, AnalysisResult result,
                                  std::size_t content_hash,
                                  std::optional<IndexedFileEntry> idx_entry);

    // Publish diagnostics to client for foreground documents. Returns true when
    // the document was foreground (diagnostics were published), false when the
    // document is background and publication was skipped.
    bool publish_committed_diagnostics(const std::string& uri,
                                       const std::vector<Diagnostic>& diagnostics, int doc_version);

    // Error handling for analysis failures.
    void handle_analysis_error(const std::string& uri, const std::exception& e);
    void handle_unknown_analysis_error(const std::string& uri);
    void publish_error_diagnostic(const std::string& uri, const std::string& message);

    SharedState state_;
    Callbacks callbacks_;
    AnalysisService* analysis_service_{nullptr};

    // URIs that should publish diagnostics regardless of diagnostics_on_save.
    // Populated by schedule_analysis(uri, true) and consumed by publish_committed_diagnostics.
    std::mutex force_diag_mutex_;
    std::unordered_set<std::string> force_diagnostics_uris_;

    std::condition_variable_any analysis_cv_;
    std::thread analysis_thread_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_ANALYSIS_PIPELINE_HPP
