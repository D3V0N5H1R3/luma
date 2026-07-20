#ifndef LUMA_LSP_SEMANTIC_TOKEN_CACHE_HPP
#define LUMA_LSP_SEMANTIC_TOKEN_CACHE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace luma::lsp {

// Per-document semantic token data stored in the cache.
struct SemanticTokenEntry {
    std::vector<int64_t> data;
    std::string result_id;
    std::size_t source_hash{0};
    int64_t document_version{-1};
};

// ═══════════════════════════════════════════════════════════════════════
// SemanticTokenCache — thread-safe cache for LSP semantic token data.
//
// Stores pre-computed semantic token arrays and result IDs per document
// URI.  All public methods acquire an internal mutex, so callers do not
// need to hold any external lock for semantic-token metadata access.
//
// Result IDs are monotonically increasing integers (stringified) that
// enable the LSP delta protocol: the client sends the previous result
// ID with textDocument/semanticTokens/full/delta, and the server can
// return a compact edit list when the tokens have only partially
// changed.
// ═══════════════════════════════════════════════════════════════════════
class SemanticTokenCache {
public:
    // Retrieve a snapshot of the cached entry for `uri`.
    // Returns std::nullopt if no entry exists.
    [[nodiscard]] std::optional<SemanticTokenEntry> get(const std::string& uri) const {
        const std::lock_guard lock(mutex_);
        const auto it = entries_.find(uri);
        if (it == entries_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    // Insert or replace the cached entry for `uri`.
    // Automatically assigns a new monotonically increasing result ID.
    // Returns the assigned result ID.
    [[nodiscard]] std::string update(const std::string& uri, std::vector<int64_t> data,
                                     std::size_t source_hash, int64_t document_version = -1) {
        const std::lock_guard lock(mutex_);
        const auto id = std::to_string(counter_.fetch_add(1));
        entries_.insert_or_assign(
            uri, SemanticTokenEntry{std::move(data), id, source_hash, document_version});
        return id;
    }

    // Update only the result ID for an existing entry (e.g. after a
    // no-change delta response).  Returns the new result ID, or
    // std::nullopt if the URI is not cached.
    [[nodiscard]] std::optional<std::string> refresh_result_id(const std::string& uri) {
        const std::lock_guard lock(mutex_);
        const auto id = std::to_string(counter_.fetch_add(1));
        const auto it = entries_.find(uri);
        if (it == entries_.end()) {
            return std::nullopt;
        }
        it->second.result_id = id;
        return id;
    }

    // Remove the cached entry for a single document.
    void invalidate(const std::string& uri) {
        const std::lock_guard lock(mutex_);
        entries_.erase(uri);
    }

    // Remove all cached entries.
    void invalidate_all() {
        const std::lock_guard lock(mutex_);
        entries_.clear();
    }

    // Number of cached entries.
    [[nodiscard]] std::size_t size() const {
        const std::lock_guard lock(mutex_);
        return entries_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SemanticTokenEntry> entries_;
    std::atomic<int64_t> counter_{0};
};

} // namespace luma::lsp

#endif // LUMA_LSP_SEMANTIC_TOKEN_CACHE_HPP
