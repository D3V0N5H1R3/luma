#ifndef LUMA_LSP_ANALYSIS_CACHE_HPP
#define LUMA_LSP_ANALYSIS_CACHE_HPP

#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "lsp_analysis_result.hpp"
#include "lsp_optional_ref.hpp"

namespace luma::lsp {

// Forward declaration — CacheTransaction is defined after LspAnalysisCache.
class CacheTransaction;

// Manages cached analysis results with LRU eviction and include dependency tracking.
//
// NOT thread-safe — the caller (LspServer) must hold appropriate locks before
// calling any method.  This class encapsulates the data structures and eviction
// logic without adding synchronisation overhead.
//
// Transactional updates
// ─────────────────────
// Use `begin_update(uri)` to obtain a CacheTransaction that collects an
// AnalysisResult and include-dependency edges.  Calling `commit()` on the
// transaction atomically applies all staged changes; if the transaction is
// destroyed without committing (e.g. due to an exception), all staged
// changes are silently discarded, leaving the cache unchanged.
class LspAnalysisCache {
public:
    explicit LspAnalysisCache(std::size_t max_entries = 128);

    // ─── Lookup ───

    /// Read-only access to the analysis result for `uri`.
    [[nodiscard("Result contains cached analysis — ignoring it loses the lookup")]]
    optional_ref<const AnalysisResult> find(const std::string& uri) const;

    /// Mutable access to the analysis result for `uri`.
    [[nodiscard("Result contains cached analysis — ignoring it loses the lookup")]]
    optional_ref<AnalysisResult> find(const std::string& uri);

    // ─── Transactional update ───

    /// Begin a transactional update for `uri`.
    /// The returned CacheTransaction collects an AnalysisResult and
    /// include-dependency edges; calling commit() applies them atomically.
    /// If the transaction is destroyed without committing (e.g. due to an
    /// exception), all staged changes are discarded (implicit rollback).
    [[nodiscard("CacheTransaction must be committed or explicitly discarded")]]
    CacheTransaction begin_update(const std::string& uri);

    // ─── Insertion / removal ───

    /// Insert or update the analysis result for `uri`.
    /// Updates LRU order (moves `uri` to most-recently-used).
    void insert(const std::string& uri, AnalysisResult result);

    /// Direct mutable access to the stored result for `uri`.
    /// Precondition: `uri` must already exist in the cache (use insert() first).
    /// Throws std::out_of_range if `uri` is not present.
    /// Useful for in-place metadata updates after insert.
    [[nodiscard]] AnalysisResult& at(const std::string& uri);

    /// Remove the analysis result for `uri`.
    void remove(const std::string& uri);

    /// Whether the cache contains an entry for `uri`.
    [[nodiscard]] bool contains(const std::string& uri) const;

    /// Number of cached entries.
    [[nodiscard]] std::size_t size() const;

    // ─── LRU management ───

    /// Move `uri` to the most-recently-used position.
    void touch(const std::string& uri);

    /// Evict the least-recently-used entry that satisfies `can_evict`.
    /// Returns true if an entry was evicted.
    /// `can_evict` receives the URI and returns true if it may be evicted.
    [[nodiscard("Eviction result determines whether the cache shrank — ignoring may mask "
                "full-cache errors")]]
    bool evict_one(const std::function<bool(const std::string&)>& can_evict);

    /// Evict entries until size <= max_entries, skipping those that fail `can_evict`.
    void evict_to_limit(const std::function<bool(const std::string&)>& can_evict);

    // ─── Include dependency tracking ───

    /// Record that `dependent_uri` depends on `include_uri`.
    void add_include_dependent(const std::string& include_uri, const std::string& dependent_uri);

    /// Get all URIs that depend on a given include path.
    [[nodiscard]] optional_ref<const std::unordered_set<std::string>>
    get_dependents(const std::string& include_uri) const;

    /// Remove a URI from all dependency sets.
    void remove_dependent(const std::string& uri);

    // ─── Iteration ───

    /// Iterate over all cached entries (const).
    template <typename Fn> void for_each(Fn&& fn) const {
        for (const auto& [uri, result] : cache_) {
            fn(uri, result);
        }
    }

    /// Direct access to the underlying map (for code that needs iterators).
    [[nodiscard]] const std::unordered_map<std::string, AnalysisResult>& entries() const {
        return cache_;
    }

    // ─── Cross-file symbol reverse index ───

    /// A cross-file symbol definition site: the URI of the file that defines
    /// the symbol and the source location of the definition.
    struct SymbolLocation {
        std::string uri;
        SourceLocation location;
    };

    /// Look up a symbol's definition across all cached files in O(1).
    /// Searches the incrementally-maintained reverse index built from every
    /// cached file's `definitions` and `user_functions` maps.
    /// `exclude_uri`, when non-empty, is skipped (used to exclude the file the
    /// cursor is already in, mirroring the cross-file fallback scan).
    /// Returns the first matching definition, or nullopt when the symbol is not
    /// defined in any cached file.
    [[nodiscard]] std::optional<SymbolLocation>
    lookup_symbol(const std::string& name, const std::string& exclude_uri = {}) const;

    /// Whether the reverse index holds any symbols. When false, callers should
    /// fall back to a linear scan (the index has not been populated yet).
    [[nodiscard]] bool has_symbol_index() const {
        return !symbol_index_.empty();
    }

private:
    std::unordered_map<std::string, AnalysisResult> cache_;
    std::list<std::string> lru_list_;
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_index_;
    std::unordered_map<std::string, std::unordered_set<std::string>> include_dependents_;
    std::size_t max_entries_;

    // Reverse index: symbol name → definition sites across all cached files.
    // Kept in lock-step with cache_ by every mutating method (insert/remove/
    // evict), so a lookup miss authoritatively means "not defined anywhere".
    struct IndexEntry {
        std::string uri;
        SourceLocation location;
    };

    std::unordered_map<std::string, std::vector<IndexEntry>> symbol_index_;
    // Secondary map: file URI → symbol names it contributed, so a file's
    // entries can be removed from symbol_index_ without scanning every bucket.
    std::unordered_map<std::string, std::vector<std::string>> uri_symbols_;

    // Add every definition/user_function in cache_[uri] to the reverse index.
    // Precondition: cache_ already contains an entry for `uri`.
    void index_uri(const std::string& uri);

    // Remove all reverse-index entries contributed by `uri`.
    void unindex_uri(const std::string& uri);

    // CacheTransaction needs access to private mutation methods.
    friend class CacheTransaction;
};

// ═══════════════════════════════════════════════════════════════════════════
// CacheTransaction — RAII transaction for atomic cache updates
// ═══════════════════════════════════════════════════════════════════════════
//
// Stages an AnalysisResult and include-dependency edges, then applies them
// all at once when commit() is called.  If the transaction is destroyed
// without committing (due to an exception or explicit rollback()), all
// staged changes are silently discarded — the cache remains unchanged.
//
// Usage:
//
//   auto txn = cache.begin_update(uri);
//   txn.set_result(std::move(result));
//   for (const auto& path : result.semantic.includes.included_paths) {
//       txn.add_include_dependent(path);
//   }
//   txn.commit();   // atomically inserts result + dependency edges
//
// Move-only; not copyable.
class CacheTransaction {
public:
    ~CacheTransaction();

    CacheTransaction(const CacheTransaction&) = delete;
    CacheTransaction& operator=(const CacheTransaction&) = delete;
    CacheTransaction(CacheTransaction&& other) noexcept;
    CacheTransaction& operator=(CacheTransaction&&) = delete;

    /// Stage the analysis result to be inserted on commit.
    void set_result(AnalysisResult result);

    /// Stage an include dependency: the transaction's URI depends on `include_path`.
    void add_include_dependent(const std::string& include_path);

    /// Atomically apply all staged changes to the cache.
    /// After commit the transaction is consumed and cannot be reused.
    void commit();

    /// Explicitly discard all staged changes without modifying the cache.
    void rollback() noexcept;

    /// Whether this transaction has been committed or rolled back.
    [[nodiscard]] bool is_finished() const noexcept {
        return finished_;
    }

    /// The URI this transaction targets.
    [[nodiscard]] const std::string& uri() const noexcept {
        return uri_;
    }

private:
    friend class LspAnalysisCache;
    CacheTransaction(LspAnalysisCache& cache, std::string uri);

    LspAnalysisCache* cache_;
    std::string uri_;
    std::optional<AnalysisResult> pending_result_;
    std::vector<std::string> pending_include_deps_;
    bool finished_{false};
};

} // namespace luma::lsp

#endif // LUMA_LSP_ANALYSIS_CACHE_HPP
