#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_set>

#include "common/path_utils.hpp"
#include "json/json.hpp"
#include "lsp_analysis_pipeline.hpp"
#include "lsp_analysis_service.hpp"
#include "lsp_constants.hpp"
#include "lsp_params.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_workspace_handler.hpp"
#include "lsp_workspace_indexer.hpp"
#include "protocol/uri_utils.hpp"

namespace luma::lsp {

using luma::protocol::canonicalize_uri;
using luma::protocol::path_to_uri;
using luma::protocol::uri_to_path;

// ═══════════════════════════════════════════════════════════
// Workspace-wide file indexing
// ═══════════════════════════════════════════════════════════

void LspWorkspaceHandler::scan_workspace_files() {
    const std::string progress_token = "luma/indexing";

    // Validate the persisted index here, on the background scan thread, so the
    // O(files) filesystem stats do not block the main message-loop thread
    // during initialization. This runs before any file is indexed, preserving
    // the original validate-before-scan ordering.
    //
    // validate() mutates the (unsynchronized) persisted index, which the
    // analysis worker also upserts into under the exclusive write state lock.
    // Hold that same lock here to serialize with the worker; log the result
    // afterwards so transport I/O stays off the lock.
    std::size_t invalidated_entries = 0;
    {
        auto state = ctx_.acquire_write_lock();
        invalidated_entries = ctx_.workspace.validate_persisted_index();
    }
    if (invalidated_entries > 0) {
        ctx_.log_message(std::format("Validated persisted index ({} stale entries removed)",
                                     invalidated_entries));
    }

    ctx_.workspace.set_indexing(true);
    const auto start_time = std::chrono::steady_clock::now();
    ctx_.log_message("Workspace indexing started");

    // Request the progress token from the client.
    ctx_.send_notification("window/workDoneProgress/create",
                           JsonValue(JsonValue::ObjectType{{"token", JsonValue(progress_token)}}));
    ctx_.send_progress_begin(progress_token, "Indexing workspace");

    // Observer that routes events to the LspServer context.
    class ScanObserver : public WorkspaceScanObserver {
    public:
        ScanObserver(LspWorkspaceHandler& handler, const std::string& progress_token)
            : handler_(handler), progress_token_(progress_token) {}

        void on_file_found(const std::string& path) override {
            handler_.load_background_file(path);
        }

        void on_progress(std::size_t n) override {
            handler_.ctx_.send_progress_report(progress_token_,
                                               std::format("Indexed {} files...", n));
        }

        void on_log(const std::string& msg) override {
            handler_.ctx_.log_message(msg);
        }

    private:
        LspWorkspaceHandler& handler_;
        const std::string& progress_token_;
    };

    ScanObserver observer(*this, progress_token);
    WorkspaceIndexer indexer(running_);
    const auto count = indexer.scan(ctx_.workspace.roots(), observer);

    ctx_.send_progress_end(progress_token);
    ctx_.workspace.set_indexing(false);

    const auto end_time = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    if (count > 0) {
        ctx_.log_message(std::format("Workspace indexing complete: {} file(s) in {}.{:03d}s", count,
                                     elapsed_ms / 1000, elapsed_ms % 1000));
    } else {
        ctx_.log_message(std::format("Workspace indexing complete: 0 files ({} ms)", elapsed_ms));
    }

    // Look for luma.json in each workspace root.
    ctx_.workspace.discover_project_config(
        ctx_.configuration.config(), [this](const std::string& msg) { ctx_.log_message(msg); });
}

void LspWorkspaceHandler::load_background_file(const std::string& path) {
    namespace fs = std::filesystem;

    const auto uri = canonicalize_uri(path_to_uri(path));

    {
        auto state = ctx_.acquire_read_lock();
        // Skip if already tracked (opened by the editor or previously loaded).
        if (state.documents().contains(state.token(), uri)) {
            return;
        }
        // Limit background files to prevent unbounded memory growth.
        if (state.documents().background_count(state.token()) >=
            constants::limits::max_background_files) {
            return;
        }
    }

    // Read the file with a single open. Opening in "ate" mode positions the
    // stream at the end so the size can be checked (OOM protection) via tellg,
    // avoiding a separate fs::file_size stat that redundantly re-resolves the
    // path.
    std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return;
    }
    const auto file_size = static_cast<std::streamoff>(file.tellg());
    if (file_size < 0 ||
        static_cast<std::uintmax_t>(file_size) > constants::limits::max_file_bytes) {
        return;
    }
    file.seekg(0);

    std::ostringstream oss;
    oss << file.rdbuf();
    const auto content = oss.str();

    {
        auto state = ctx_.acquire_write_lock();

        // Re-check under exclusive lock to prevent TOCTOU: between the
        // shared_lock check above and here, handle_did_open may have
        // inserted the document with editor content we must not overwrite.
        if (state.documents().contains(state.token(), uri)) {
            return;
        }

        state.documents().set_content(state.token(), uri, content);
        state.documents().mark_background(state.token(), uri);

        // If the persisted index has a valid cached entry for this file,
        // the content hasn't changed since the last session — skip analysis.
        const auto content_hash = std::hash<std::string>{}(content);
        if (ctx_.workspace.persisted_index().is_valid(path, content_hash)) {
            state.documents().set_content_hash(state.token(), uri, content_hash);
            return;
        }
    }

    schedule_analysis(uri);
}

// ═══════════════════════════════════════════════════════════
// Configuration
// ═══════════════════════════════════════════════════════════

void LspWorkspaceHandler::handle_did_change_configuration(const JsonValue& params) {
    ctx_.configuration.apply_settings(params,
                                      [this](const std::string& msg) { ctx_.log_message(msg); });
}

// ═══════════════════════════════════════════════════════════
// Watched file changes
// ═══════════════════════════════════════════════════════════

namespace {

// A file-change event whose URI has been resolved to a canonical form and
// whose path has been validated to lie within the workspace.
struct ResolvedFileEvent {
    std::string uri;  // canonical URI for document store lookups
    std::string path; // filesystem path
    int type;
};

// Convert a file-change event to a resolved URI/path pair.
// Returns nullopt when the URI cannot be resolved or the path lies outside
// every workspace root (guards against reads from a rogue client).
[[nodiscard]] std::optional<ResolvedFileEvent>
resolve_file_event(const params::FileEvent& event, const WorkspaceManager& workspace) {
    const auto path_opt = uri_to_path(event.uri);
    if (!path_opt || !workspace.is_in_workspace(*path_opt)) {
        return std::nullopt;
    }
    return ResolvedFileEvent{.uri = event.uri, .path = *path_opt, .type = event.type};
}

struct PartitionedFileEvents {
    std::vector<ResolvedFileEvent> deletions;
    std::vector<ResolvedFileEvent> creates_changes;
};

// Split a flat list of resolved events into deletions and creates/changes.
[[nodiscard]] PartitionedFileEvents
partition_file_events(const std::vector<ResolvedFileEvent>& events) {
    PartitionedFileEvents result;
    for (const auto& ev : events) {
        if (ev.type == constants::file_change::deleted) {
            result.deletions.push_back(ev);
        } else {
            result.creates_changes.push_back(ev);
        }
    }
    return result;
}

// Handle a deleted file event: remove background files from the document
// store and analysis cache.
void handle_file_deleted(WriteStateLock& state, const std::string& uri) {
    if (state.documents().is_background(state.token(), uri)) {
        state.documents().remove(state.token(), uri);
        state.cache().remove(uri);
    }
}

// Handle a created or changed file event: queue background files for
// reload and schedule new .luma files for indexing.
void handle_file_changed(WriteStateLock& state, const std::string& uri,
                         const std::string& changed_path, std::vector<std::string>& files_to_load) {
    if (state.documents().is_background(state.token(), uri)) {
        state.documents().remove(state.token(), uri);
        files_to_load.push_back(changed_path);
    } else if (!state.documents().contains(state.token(), uri) &&
               has_luma_extension(changed_path)) {
        // New .luma file created — index it as background.
        files_to_load.push_back(changed_path);
    }
}

// Check if any resolved event corresponds to "luma.json".
// Loads the project configuration and schedules re-analysis for all open
// documents if a matching file is found.
void check_luma_json_changes(const std::vector<ResolvedFileEvent>& resolved, LspHandlerContext& ctx,
                             const std::function<void(const std::string&)>& schedule_fn) {
    for (const auto& ev : resolved) {
        if (std::filesystem::path(ev.path).filename().string() != "luma.json") {
            continue;
        }
        ctx.workspace.load_project_config(ev.path, ctx.configuration.config(),
                                          [&ctx](const std::string& msg) { ctx.log_message(msg); });
        // Collect URIs under shared lock, then release before scheduling
        // (schedule_analysis acquires an exclusive lock on the same mutex).
        std::vector<std::string> uris_to_schedule;
        {
            auto state = ctx.acquire_read_lock();
            for (const auto& [doc_uri, _] : state.documents().all(state.token())) {
                if (!state.documents().is_background(state.token(), doc_uri)) {
                    uris_to_schedule.push_back(doc_uri);
                }
            }
        }
        for (const auto& doc_uri : uris_to_schedule) {
            schedule_fn(doc_uri);
        }
        break;
    }
}

} // namespace

void LspWorkspaceHandler::handle_did_change_watched_files(const JsonValue& params) {
    auto parsed = params::DidChangeWatchedFilesParams::from_json(params);
    if (!parsed) {
        return;
    }

    // Resolve and validate all events in a single pre-pass, normalising URIs
    // and filtering out anything outside the workspace.  This single conversion
    // removes duplicated uri_to_path + is_in_workspace calls in the write-lock
    // loop and the luma.json check below.
    std::vector<ResolvedFileEvent> resolved;
    resolved.reserve(parsed->changes.size());
    for (const auto& event : parsed->changes) {
        if (auto ev = resolve_file_event(event, ctx_.workspace)) {
            resolved.push_back(std::move(*ev));
        }
    }

    const auto [deletions, creates_changes] = partition_file_events(resolved);

    std::unordered_set<std::string> uris_to_reanalyze;
    std::vector<std::string> files_to_load;

    {
        auto state = ctx_.acquire_write_lock();

        for (const auto& ev : deletions) {
            handle_file_deleted(state, ev.uri);
            analysis_service_.erase_cached_file(ev.path);
            if (const auto deps = state.cache().get_dependents(ev.path)) {
                for (const auto& dep_uri : *deps) {
                    uris_to_reanalyze.insert(dep_uri);
                }
            }
        }

        for (const auto& ev : creates_changes) {
            handle_file_changed(state, ev.uri, ev.path, files_to_load);
            analysis_service_.erase_cached_file(ev.path);
            if (const auto deps = state.cache().get_dependents(ev.path)) {
                for (const auto& dep_uri : *deps) {
                    uris_to_reanalyze.insert(dep_uri);
                }
            }
        }
    }

    // Load new/changed background files.
    for (const auto& path : files_to_load) {
        load_background_file(path);
    }

    // Re-analyze affected documents.
    for (const auto& uri : uris_to_reanalyze) {
        schedule_analysis(uri, true);
    }

    // Check if luma.json was changed — reload project configuration.
    check_luma_json_changes(resolved, ctx_,
                            [this](const std::string& uri) { schedule_analysis(uri, true); });
}

void LspWorkspaceHandler::handle_did_save(const JsonValue& params) {
    // Refresh the dependents of a saved file. Includes are read from disk, and
    // didChange only re-analyzes the edited document itself, so a file A that
    // does `include "b.luma"` keeps showing stale cross-file diagnostics, hover,
    // and completion after B is edited — until B is saved (updating disk) and its
    // dependents are re-analyzed. Mirror the watched-files path: drop B's include
    // cache and re-schedule every dependent of B.
    if (!params.is_object() || !params.has("textDocument")) {
        return;
    }
    const auto uri_opt = luma::json::try_extract_field<std::string>(params["textDocument"], "uri");
    if (!uri_opt) {
        return;
    }
    const auto path_opt = uri_to_path(*uri_opt);
    if (!path_opt) {
        return;
    }

    // When diagnostics_on_save is enabled, re-schedule the saved file itself
    // with force_diagnostics so its diagnostics are published now.
    if (ctx_.configuration.config().get()->diagnostics_on_save) {
        schedule_analysis(*uri_opt, true);
    }

    std::unordered_set<std::string> uris_to_reanalyze;
    {
        auto state = ctx_.acquire_write_lock();
        analysis_service_.erase_cached_file(*path_opt);
        if (const auto deps = state.cache().get_dependents(*path_opt)) {
            for (const auto& dep_uri : *deps) {
                uris_to_reanalyze.insert(dep_uri);
            }
        }
    }

    for (const auto& dep_uri : uris_to_reanalyze) {
        schedule_analysis(dep_uri);
    }
}

void LspWorkspaceHandler::schedule_analysis(const std::string& uri, bool force_diagnostics) {
    analysis_pipeline_.schedule_analysis(uri, force_diagnostics);
}

} // namespace luma::lsp
