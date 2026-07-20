#include "lsp_analysis_pipeline.hpp"

#include <chrono>
#include <format>
#include <optional>

#include "lsp_analysis_result.hpp"
#include "lsp_analysis_service_impl.hpp"
#include "lsp_constants.hpp"
#include "lsp_diagnostic_builder.hpp"
#include "lsp_exception_utils.hpp"
#include "lsp_lock_utils.hpp"
#include "lsp_persisted_index.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_types.hpp"
#include "protocol/uri_utils.hpp"

namespace luma::lsp {

using luma::protocol::uri_to_path;

AnalysisPipeline::AnalysisPipeline(SharedState state, Callbacks callbacks, AnalysisService* service)
    : state_(state), callbacks_(std::move(callbacks)), analysis_service_(service) {}

AnalysisPipeline::~AnalysisPipeline() {
    stop();
}

void AnalysisPipeline::start() {
    analysis_thread_ = std::thread([this] { analysis_worker(); });
}

void AnalysisPipeline::stop() {
    analysis_cv_.notify_one();

    if (analysis_thread_.joinable()) {
        analysis_thread_.join();
    }

    // Nullify the raw pointer after the worker thread has been joined
    // to prevent any accidental use-after-free.
    analysis_service_ = nullptr;
}

void AnalysisPipeline::schedule_analysis(const std::string& uri) {
    with_unique_lock(state_.state_mutex, [&] { state_.pending_uris.insert(uri); });

    // Signal the in-flight analysis (if any) to abort early so the
    // worker picks up the new content without waiting for the stale
    // analysis to finish.
    request_cancellation();
    analysis_cv_.notify_one();
}

// ═══════════════════════════════════════════════════════════════════════
// Analysis Pipeline — background worker
// ═══════════════════════════════════════════════════════════════════════
//
// Architecture
// ────────────
// The analysis pipeline runs on a dedicated background thread
// (analysis_thread_) and processes document URIs that have been marked
// dirty by schedule_analysis().  The pipeline has three phases:
//
//   1. Debounce & collect  – wait for edits to settle, then drain
//      pending_uris_ under state_mutex_ (unique).
//   2. Analyse             – run AnalysisService::analyze() on each
//      URI's source snapshot.  This is the expensive, lock-free phase.
//   3. Commit & publish    – re-acquire state_mutex_ (unique), verify
//      the document hasn't changed, store the result in
//      analysis_cache_, pre-compute semantic tokens, and publish
//      diagnostics to the client.
// ═══════════════════════════════════════════════════════════════════════

namespace {

// Return true if the URI is not open in the editor and not a background
// document — i.e. it is safe to evict from cache.
bool is_evictable(const WriteStateLock& state, const std::string& uri) {
    return !state.documents().contains(state.token(), uri) &&
           !state.documents().is_background(state.token(), uri);
}

// Evict stale cache entries using the LRU policy, then fall back to a
// hard-cap sweep if the cache is still too large.
void evict_stale_cache_entries(WriteStateLock& state) {
    // LRU eviction with the cache's own configurable limit.
    state.cache().evict_to_limit([&state](const std::string& evict_uri) {
        if (is_evictable(state, evict_uri)) {
            state.documents().erase_content_hash(state.token(), evict_uri);
            return true;
        }
        return false;
    });

    // Hard cap: remove all non-open entries if the cache is still too large.
    if (state.cache().size() > constants::limits::max_cache_size) {
        std::vector<std::string> to_evict;
        state.cache().for_each([&](const std::string& cached_uri, const AnalysisResult&) {
            if (is_evictable(state, cached_uri)) {
                to_evict.push_back(cached_uri);
            }
        });
        for (const auto& evict_uri : to_evict) {
            state.cache().remove(evict_uri);
            state.documents().erase_content_hash(state.token(), evict_uri);
        }
    }
}

} // anonymous namespace

// ─── Main worker loop ────────────────────────────────────────────────

void AnalysisPipeline::analysis_worker() {
    callbacks_.log_message("Analysis worker started", constants::message_type::info);

    while (state_.running.load()) {
        auto uris = debounce_and_collect();
        if (uris.empty()) {
            continue;
        }

        state_.analysis_cancel_flag.store(false, std::memory_order_release);

        for (const auto& uri : uris) {
            if (!state_.running.load()) {
                break;
            }
            if (state_.analysis_cancel_flag.load(std::memory_order_acquire)) {
                // New edits arrived — re-schedule all URIs and restart
                // the loop to pick up fresh content.
                with_unique_lock(state_.state_mutex, [&] { state_.pending_uris.insert_all(uris); });
                break;
            }

            try {
                analyze_single_uri(uri);
            } catch (const std::exception& e) {
                handle_analysis_error(uri, e);
            } catch (...) {
                handle_unknown_analysis_error(uri);
            }
        }
    }

    callbacks_.log_message("Analysis worker stopped", constants::message_type::info);
}

// ─── Phase 1: Debounce & collect ─────────────────────────────────────

std::vector<std::string> AnalysisPipeline::debounce_and_collect() {
    std::unique_lock lock(state_.state_mutex);
    analysis_cv_.wait(lock,
                      [this] { return !state_.pending_uris.empty() || !state_.running.load(); });

    if (!state_.running.load()) {
        return {};
    }

    // Wait briefly for additional changes to coalesce.
    analysis_cv_.wait_for(
        lock, std::chrono::milliseconds(state_.configuration.config().get()->analysis_debounce_ms),
        [this] { return !state_.running.load(); });

    if (!state_.running.load()) {
        return {};
    }

    return state_.pending_uris.drain_all();
}

// ─── Phase 2: Analyse a single URI ──────────────────────────────────

void AnalysisPipeline::analyze_single_uri(const std::string& uri) {
    // Snapshot the source text and compute content hash under a shared lock.
    std::string source;
    std::size_t content_hash = 0;
    {
        const ReadStateLock state(state_.state_mutex, state_.doc_store, state_.analysis_cache,
                                  state_.pending_uris);
        const auto* doc_ptr = state.documents().get_content(state.token(), uri);
        if (doc_ptr == nullptr) {
            return;
        }
        source = *doc_ptr;
        content_hash = std::hash<std::string>{}(source);

        auto cached_hash = state.documents().get_content_hash(state.token(), uri);
        if (cached_hash != 0 && cached_hash == content_hash) {
            return;
        }
    }

    const auto start = std::chrono::steady_clock::now();
    const auto deadline =
        start + std::chrono::milliseconds(state_.configuration.config().get()->analysis_timeout_ms);

    AnalysisResult result = analysis_service_->analyze(uri, source, deadline);

    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

    callbacks_.log_message(std::format("Analysis: {} ms, {} diagnostics", ms.count(),
                                       result.semantic.diagnostics.size()),
                           3);

    commit_and_publish(uri, std::move(result), content_hash);
}

// ─── Phase 3: Commit & publish ──────────────────────────────────────

namespace {

// Prepare the index entry and semantic token data from analysis result.
// Returns the index entry needed for committing to the persisted index.
[[nodiscard]] std::optional<IndexedFileEntry> prepare_index_entry(const AnalysisResult& result,
                                                                  const std::string& uri,
                                                                  std::size_t content_hash) {
    const auto file_path = uri_to_path(uri);
    if (!file_path.has_value()) {
        return std::nullopt;
    }

    IndexedFileEntry entry;
    entry.path = *file_path;
    entry.content_hash = content_hash;
    for (const auto& [name, _] : result.semantic.symbols.user_functions) {
        entry.function_names.push_back(name);
    }
    for (const auto& [name, _] : result.semantic.symbols.record_definitions) {
        entry.record_names.push_back(name);
    }
    for (const auto& [name, _] : result.semantic.symbols.choice_variants) {
        entry.choice_names.push_back(name);
    }
    for (const auto& [name, _] : result.semantic.symbols.definitions) {
        entry.exported_symbols.push_back(name);
    }
    entry.has_main = result.semantic.symbols.user_functions.contains("main");
    return entry;
}

} // anonymous namespace

// Acquire write lock, verify document is unchanged, store analysis result
// in cache, and update the persisted index. Returns the document version
// and whether the commit succeeded.
AnalysisPipeline::CommitOutcome
AnalysisPipeline::commit_to_cache(const std::string& uri, AnalysisResult result,
                                  std::size_t content_hash,
                                  std::optional<IndexedFileEntry> idx_entry) {
    WriteStateLock state(state_.state_mutex, state_.doc_store, state_.analysis_cache,
                         state_.pending_uris);

    const auto* doc_ptr = state.documents().get_content(state.token(), uri);
    if (doc_ptr == nullptr) {
        return {};
    }
    if (std::hash<std::string>{}(*doc_ptr) != content_hash) {
        state.pending_uris().insert(uri);
        return {};
    }

    state.documents().set_content_hash(state.token(), uri, content_hash);

    if (idx_entry.has_value()) {
        state_.workspace.persisted_index().upsert(std::move(*idx_entry));
    }

    auto txn = state.cache().begin_update(uri);
    for (const auto& path : result.semantic.includes.included_paths) {
        txn.add_include_dependent(path);
    }
    txn.set_result(std::move(result));
    txn.commit();

    evict_stale_cache_entries(state);

    const int stored_version = state.documents().get_version(state.token(), uri);
    int doc_version = 0;
    if (stored_version >= 0) {
        doc_version = stored_version;
    } else {
        doc_version = 1;
        state.documents().set_version(state.token(), uri, doc_version);
    }

    return {.doc_version = doc_version, .committed = true};
}

bool AnalysisPipeline::publish_committed_diagnostics(const std::string& uri,
                                                     const std::vector<Diagnostic>& diagnostics,
                                                     int doc_version) {
    const bool is_background = with_shared_lock(state_.state_mutex, [&] {
        const LockToken lock_token;
        return state_.doc_store.is_background(lock_token, uri);
    });

    if (is_background) {
        return false;
    }

    try {
        callbacks_.publish_diagnostics(uri, diagnostics, doc_version);
    } catch (const std::exception& e) {
        callbacks_.log_message(std::format("Failed to publish diagnostics: {}", e.what()),
                               constants::message_type::info);
    } catch (...) {
        callbacks_.log_message(
            std::format("Failed to publish diagnostics: {}", format_current_exception()),
            constants::message_type::info);
    }
    return true;
}

void AnalysisPipeline::commit_and_publish(const std::string& uri, AnalysisResult result,
                                          std::size_t content_hash) {
    const std::vector<Diagnostic> diags_copy = result.semantic.diagnostics;

    // Phase 3a: Prepare data outside the lock.
    auto idx_entry = prepare_index_entry(result, uri, content_hash);
    auto token_data = callbacks_.compute_semantic_token_data(result);

    // Phase 3b: Commit to cache under write lock.
    const auto outcome =
        commit_to_cache(uri, std::move(result), content_hash, std::move(idx_entry));
    if (!outcome.committed) {
        return;
    }

    // Update semantic token cache outside the lock — SemanticTokenCache
    // has its own internal mutex and does not need state_mutex_.
    (void)state_.semantic_token_cache.update(uri, std::move(token_data), content_hash,
                                             static_cast<int64_t>(outcome.doc_version));

    // Phase 3c: Publish diagnostics for foreground documents.
    const bool foreground = publish_committed_diagnostics(uri, diags_copy, outcome.doc_version);

    // Phase 3d: For foreground edits, ask the client to re-pull semantic tokens
    // now that fresh tokens have been cached, so highlighting held from the
    // debounce window is replaced (B09). The refresh is global and param-less,
    // so it is only fired for foreground commits to avoid spamming it during
    // background workspace indexing.
    if (foreground && callbacks_.refresh_semantic_tokens) {
        callbacks_.refresh_semantic_tokens();
    }
}

// ─── Error handling ─────────────────────────────────────────────────

void AnalysisPipeline::handle_analysis_error(const std::string& uri, const std::exception& e) {
    const std::string msg = e.what();
    if (msg.find("cancelled") != std::string::npos) {
        with_unique_lock(state_.state_mutex, [&] { state_.pending_uris.insert(uri); });
    } else {
        callbacks_.log_message(std::format("Analysis failed for {}: {}", uri, msg),
                               constants::message_type::info);
        publish_error_diagnostic(uri, std::format("Analysis failed: {}", msg));
    }
}

void AnalysisPipeline::handle_unknown_analysis_error(const std::string& uri) {
    const std::string detail = format_current_exception();
    callbacks_.log_message(std::format("Analysis failed for {}: {}", uri, detail),
                           constants::message_type::info);
    publish_error_diagnostic(uri, std::format("Analysis failed: {}", detail));
}

void AnalysisPipeline::publish_error_diagnostic(const std::string& uri,
                                                const std::string& message) {
    try {
        callbacks_.publish_diagnostics(
            uri,
            {diagnostic_builder::make_whole_file_diagnostic(message, constants::severity::error)},
            0);
    } catch (...) {
        callbacks_.log_message(
            std::format("Failed to publish error diagnostic after analysis failure: {}",
                        format_current_exception()),
            constants::message_type::info);
    }
}

} // namespace luma::lsp
