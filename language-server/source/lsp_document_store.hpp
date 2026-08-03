#ifndef LUMA_LSP_DOCUMENT_STORE_HPP
#define LUMA_LSP_DOCUMENT_STORE_HPP

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "common/string_hash.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// DocumentStore — manages open document state.
//
// Stores document text, version numbers, line-start offset
// caches, content hashes, and background-file tracking.
//
// ── Thread safety ──────────────────────────────────────────
//
// DocumentStore is NOT internally synchronised.  The caller must hold
// the appropriate lock on the owning server's `state_mutex_`
// (a std::shared_mutex) before calling any method:
//
//   • **Read-only methods** (marked `const`, in the "shared_lock"
//     section below): callers must hold at least a
//     `std::shared_lock<std::shared_mutex>` on `state_mutex_`.
//     Multiple readers may proceed concurrently.
//
//   • **Mutating methods** (in the "unique_lock" section below):
//     callers must hold an exclusive
//     `std::unique_lock<std::shared_mutex>` on `state_mutex_`.
//     No other readers or writers may be active.
//
// The helper wrappers `ReadStateLock` (acquires shared) and
// `WriteStateLock` (acquires exclusive) in `lsp_server_state_lock.hpp`
// handle acquisition and construct a `LockToken` automatically.
// Prefer those wrappers over manual lock management.
//
// ── LockToken ──────────────────────────────────────────────
//
// Every DocumentStore method accepts a `const LockToken&` parameter.
// `LockToken` is an empty marker type whose sole purpose is to make
// the locking requirement visible at each call site.  A `LockToken`
// is constructed by the state-lock wrappers when the mutex is
// acquired; passing it proves (by convention) that the caller holds
// the lock.  `LockToken` does NOT enforce any compile-time
// guarantee — it is a documentation aid.  A future improvement
// could derive `LockToken` from `std::shared_lock` /
// `std::unique_lock` or use `[[clang::require_capability]]`
// annotations to enforce the contract statically.
// ═══════════════════════════════════════════════════════════

// A token proving that the caller holds an appropriate lock.
// Created by the state-lock wrappers (ReadStateLock / WriteStateLock)
// when acquiring state_mutex_.
//
// See class-level documentation above for full threading policy.
struct LockToken {
    explicit LockToken() = default;
};

// Per-document state grouped into a single struct so that adding a new
// per-document field requires touching only one place.
struct DocumentState {
    std::string content;
    int version{-1};
    std::size_t content_hash{0};
    std::size_t stored_hash{0};
    bool dirty{true};
    std::vector<std::size_t> line_starts;
    bool is_background{false};
};

class DocumentStore {
public:
    // ─── Read-only accessors (shared_lock) ───

    // Get the text of a document, or nullptr if not tracked.
    [[nodiscard]] const std::string* get_content(const LockToken&, const std::string& uri) const {
        const auto* doc = find_document(uri);
        return doc ? &doc->content : nullptr;
    }

    // Check if a document is tracked (open or background).
    [[nodiscard]] bool contains(const LockToken&, const std::string& uri) const {
        return documents_.contains(uri);
    }

    // Check if a document is a background file (not opened in editor).
    [[nodiscard]] bool is_background(const LockToken&, const std::string& uri) const {
        const auto* doc = find_document(uri);
        return doc != nullptr && doc->is_background;
    }

    // Get the version of a document, or -1 if not tracked.
    [[nodiscard]] int get_version(const LockToken&, const std::string& uri) const {
        const auto* doc = find_document(uri);
        return doc ? doc->version : -1;
    }

    // Get the content hash of a document, or 0 if not tracked.
    [[nodiscard]] std::size_t get_content_hash(const LockToken&, const std::string& uri) const {
        const auto* doc = find_document(uri);
        return doc ? doc->content_hash : 0;
    }

    // Get the number of background URIs.
    [[nodiscard]] std::size_t background_count(const LockToken&) const {
        std::size_t count{0};
        for (const auto& [_, doc] : documents_) {
            if (doc.is_background) {
                ++count;
            }
        }
        return count;
    }

    // Iterate over all tracked documents (uri → state).
    [[nodiscard]] const StringMap<DocumentState>& all(const LockToken&) const {
        return documents_;
    }

    // Get cached line-start offsets for a document, or nullptr if not tracked.
    [[nodiscard]] const std::vector<std::size_t>* get_line_starts(const LockToken&,
                                                                  const std::string& uri) const {
        const auto* doc = find_document(uri);
        return doc ? &doc->line_starts : nullptr;
    }

    // Returns true if the document content has changed since the last
    // set_content() call that observed a different hash, or if the document
    // has never been stored.  Analysis workers can use this to skip re-analysis
    // when the source text is unchanged.
    [[nodiscard]] bool is_dirty(const LockToken&, const std::string& uri) const {
        const auto* doc = find_document(uri);
        return doc == nullptr || doc->dirty;
    }

    // Convert a 0-based (line, character) position to a byte offset in the document.
    [[nodiscard]] std::size_t position_to_offset(const LockToken&, const std::string& uri,
                                                 const std::string& text, int line,
                                                 int character) const;

    // ─── Mutating methods (unique_lock) ───

    // Store or replace document content and rebuild line-start offsets.
    // Skips the rebuild and marks the document as clean when the content hash
    // is identical to the previously stored content.
    void set_content(const LockToken&, const std::string& uri, const std::string& text) {
        const auto new_hash = std::hash<std::string>{}(text);
        auto it = documents_.find(uri);
        const bool content_unchanged = it != documents_.end() && it->second.stored_hash == new_hash;

        if (content_unchanged) {
            it->second.dirty = false;
            return;
        }

        auto& doc = documents_[uri];
        doc.content = text;
        doc.stored_hash = new_hash;
        doc.dirty = true;
        rebuild_line_starts(uri, text);
    }

    // Recompute stored_hash from the document's current content.  The didChange
    // path mutates content in place (bypassing set_content), so without this the
    // stored_hash would keep the pre-edit value and set_content's dedup could
    // later skip a legitimate re-open update, leaving stale content in place.
    void refresh_stored_hash(const LockToken&, const std::string& uri) {
        if (auto* doc = find_document(uri)) {
            doc->stored_hash = std::hash<std::string>{}(doc->content);
        }
    }

    // Set the version for a document.
    void set_version(const LockToken&, const std::string& uri, int version) {
        documents_[uri].version = version;
    }

    // Set the content hash for a document.
    void set_content_hash(const LockToken&, const std::string& uri, std::size_t hash) {
        documents_[uri].content_hash = hash;
    }

    // Remove the content hash for a document.
    void erase_content_hash(const LockToken&, const std::string& uri) {
        auto* doc = find_document(uri);
        if (doc) {
            doc->content_hash = 0;
        }
    }

    // Get mutable reference to document content.
    // Returns nullptr if not tracked.
    [[nodiscard]] std::string* get_content(const LockToken&, const std::string& uri) {
        auto* doc = find_document(uri);
        return doc ? &doc->content : nullptr;
    }

    // Mark a document as clean (not dirty) so analysis workers skip re-analysis.
    void mark_clean(const LockToken&, const std::string& uri) {
        auto* doc = find_document(uri);
        if (doc) {
            doc->dirty = false;
        }
    }

    // Mark a document as a background file.
    void mark_background(const LockToken&, const std::string& uri) {
        documents_[uri].is_background = true;
    }

    // Unmark a document as a background file.
    void unmark_background(const LockToken&, const std::string& uri) {
        auto* doc = find_document(uri);
        if (doc) {
            doc->is_background = false;
        }
    }

    // Remove a document and all associated state.
    void remove(const LockToken&, const std::string& uri) {
        documents_.erase(uri);
    }

    // Rebuild the cached line-start offsets for a document.
    void rebuild_line_starts(const std::string& uri, const std::string& text) {
        auto& ls = documents_[uri].line_starts;
        ls.clear();
        ls.push_back(0);
        for (std::size_t i{0}; i < text.size(); ++i) {
            if (text[i] == '\n') {
                ls.push_back(i + 1);
            }
        }
    }

private:
    // Look up a document by URI, returning nullptr if not tracked.
    [[nodiscard]] const DocumentState* find_document(const std::string& uri) const {
        auto it = documents_.find(uri);
        return it != documents_.end() ? &it->second : nullptr;
    }

    // Look up a document by URI (mutable), returning nullptr if not tracked.
    [[nodiscard]] DocumentState* find_document(const std::string& uri) {
        auto it = documents_.find(uri);
        return it != documents_.end() ? &it->second : nullptr;
    }

    // All per-document state keyed by normalized URI.
    StringMap<DocumentState> documents_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_DOCUMENT_STORE_HPP
