#include "lsp_document_synchronizer.hpp"

#include <format>
#include <string>
#include <utility>

#include "json/json.hpp"
#include "lsp_constants.hpp"
#include "lsp_params.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_string_utils.hpp"
#include "protocol/uri_utils.hpp"

namespace luma::lsp {

using luma::json::try_extract_field;
using luma::protocol::canonicalize_uri;
using luma::protocol::uri_to_path;
using util::clamp_to_int;

namespace {

// Resolves an LSP start/end position pair to a [start_byte, end_byte) byte
// range in the document, using the document store's pre-computed line-start
// index.  Must be called after rebuild_line_starts() for the document.
[[nodiscard]] std::pair<std::size_t, std::size_t>
compute_byte_range(WriteStateLock& state, const std::string& uri, const std::string& doc,
                   int start_line, int start_char, int end_line, int end_char) {
    const auto start_off =
        state.documents().position_to_offset(state.token(), uri, doc, start_line, start_char);
    const auto end_off =
        state.documents().position_to_offset(state.token(), uri, doc, end_line, end_char);
    return {start_off, end_off};
}

} // namespace

DocumentSynchronizer::DocumentSynchronizer(SharedState state, Callbacks callbacks)
    : state_(state), callbacks_(std::move(callbacks)) {}

// ═══════════════════════════════════════════════════════════
// Document synchronisation
// ═══════════════════════════════════════════════════════════

void DocumentSynchronizer::handle_did_open(const JsonValue& params) {
    auto parsed = params::DidOpenTextDocumentParams::from_json(params);
    if (!parsed) {
        callbacks_.log_message("didOpen: missing or malformed textDocument params",
                               constants::message_type::info);
        return;
    }
    const auto uri = std::move(parsed->uri);
    const auto text = std::move(parsed->text);

    callbacks_.log_message(std::format("Opened: {}", uri), constants::message_type::info);

    // Captured from the locked scope for use after the lock is released.
    bool content_unchanged = false;
    bool has_cached_diags = false;
    std::vector<Diagnostic> diags_copy;

    {
        const WriteStateLock state(state_.state_mutex, state_.doc_store, state_.analysis_cache,
                                   state_.pending_uris);

        // Promote background file to foreground (editor-opened).
        state.documents().unmark_background(state.token(), uri);

        // If the document is already open, treat as a content refresh
        // rather than a fresh open — skip re-analysis if content is
        // unchanged.
        if (state.documents().contains(state.token(), uri) &&
            *state.documents().get_content(state.token(), uri) == text) {
            content_unchanged = true;
            // Content unchanged but was background — publish cached
            // diagnostics that were suppressed during background indexing.
            auto cached = state_.analysis_cache.find(uri);
            if (cached.has_value()) {
                has_cached_diags = true;
                diags_copy = cached->semantic.diagnostics;
            }
        } else {
            state.documents().set_content(state.token(), uri, text);
        }
    } // lock released here — safe to invoke callbacks

    if (content_unchanged) {
        if (has_cached_diags) {
            callbacks_.publish_diagnostics(uri, diags_copy, 0);
        }
        return;
    }

    callbacks_.schedule_analysis(uri);
}

void DocumentSynchronizer::handle_did_change(const JsonValue& params) {
    const auto& td = params.get("textDocument");
    const auto& changes_val = params.get("contentChanges");
    if (td.is_null() || !changes_val.is_array()) {
        callbacks_.log_message("didChange: missing textDocument or contentChanges field",
                               constants::message_type::info);
        return;
    }

    auto uri_opt = try_extract_field<std::string>(td, "uri");
    if (!uri_opt) {
        callbacks_.log_message("didChange: missing uri field in textDocument",
                               constants::message_type::info);
        return;
    }
    const auto uri = canonicalize_uri(*uri_opt);

    const auto& changes = changes_val.as_array();

    {
        WriteStateLock state(state_.state_mutex, state_.doc_store, state_.analysis_cache,
                             state_.pending_uris);

        // Ignore changes for documents not opened via didOpen.  This guard must
        // run before set_version below: set_version mutates through operator[]
        // and would default-insert a phantom DocumentState for an untracked
        // URI, defeating the check and letting an unopened document be analyzed
        // (and its document map grow unbounded from untrusted didChange input).
        if (!state.documents().contains(state.token(), uri)) {
            return;
        }

        // Use client-supplied document version when available.
        if (auto version = try_extract_field<int>(td, "version")) {
            state.documents().set_version(state.token(), uri, *version);
        }

        auto& doc = *state.documents().get_content(state.token(), uri);

        for (const auto& change : changes) {
            if (change.has("range")) {
                apply_incremental_change(state, uri, doc, change);
            } else {
                apply_full_sync(state, uri, doc, change);
            }
            // Rebuild after each change so the next change's range (which
            // refers to the post-edit document) resolves against fresh
            // offsets, and downstream consumers see up-to-date offsets after
            // the final change. On entry line_starts is already valid (set by
            // set_content / the previous didChange), so no pre-loop rebuild is
            // needed.
            state.documents().rebuild_line_starts(uri, doc);
        }

        // Keep stored_hash consistent with the now-edited content so a later
        // set_content (e.g. a refresh on re-open) does not wrongly dedup and
        // skip the update.
        state.documents().refresh_stored_hash(state.token(), uri);
    }

    callbacks_.schedule_analysis(uri);
}

void DocumentSynchronizer::apply_incremental_change(WriteStateLock& state, const std::string& uri,
                                                    std::string& doc, const JsonValue& change) {
    // line_starts is valid on entry: the caller keeps it current after every
    // change, and set_content establishes it on didOpen.
    const auto& range = change["range"];
    const int start_line = clamp_to_int(range["start"]["line"].as_integer());
    const int start_char = clamp_to_int(range["start"]["character"].as_integer());
    const int end_line = clamp_to_int(range["end"]["line"].as_integer());
    const int end_char = clamp_to_int(range["end"]["character"].as_integer());

    const auto [start_off, end_off] =
        compute_byte_range(state, uri, doc, start_line, start_char, end_line, end_char);

    if (end_off >= start_off) {
        doc.replace(start_off, end_off - start_off, change["text"].as_string());
    }
}

void DocumentSynchronizer::apply_full_sync(WriteStateLock& /*state*/, const std::string& /*uri*/,
                                           std::string& doc, const JsonValue& change) {
    doc = change["text"].as_string();
}

void DocumentSynchronizer::handle_did_close(const JsonValue& params) {
    auto parsed = params::DidCloseTextDocumentParams::from_json(params);
    if (!parsed) {
        callbacks_.log_message("didClose: missing or malformed textDocument params",
                               constants::message_type::info);
        return;
    }
    const auto uri = std::move(parsed->uri);

    callbacks_.log_message(std::format("Closed: {}", uri), constants::message_type::info);

    // Check if the file belongs to a workspace root — if so, reload from
    // disk as a background document so cross-file references keep working.
    const auto file_path = uri_to_path(uri);
    bool reload_as_background = false;
    if (file_path.has_value()) {
        reload_as_background = state_.workspace.is_in_workspace(*file_path);
    }

    {
        const WriteStateLock state(state_.state_mutex, state_.doc_store, state_.analysis_cache,
                                   state_.pending_uris);
        state.documents().remove(state.token(), uri);
        state.cache().remove(uri);

        // Remove any pending analysis request for this document.
        state.pending_uris().remove(uri);

        // Clean up include dependency graph entries.
        state.cache().remove_dependent(uri);
    }

    // Clear diagnostics for closed document.
    callbacks_.publish_diagnostics(uri, {}, 0);

    // Reload from disk as background document for cross-file indexing.
    if (reload_as_background) {
        callbacks_.load_background_file(*file_path);
    }
}

} // namespace luma::lsp
