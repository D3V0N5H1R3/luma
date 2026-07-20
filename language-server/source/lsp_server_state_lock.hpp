#ifndef LUMA_LSP_SERVER_STATE_LOCK_HPP
#define LUMA_LSP_SERVER_STATE_LOCK_HPP

#include <shared_mutex>

#include "lsp_analysis_cache.hpp"
#include "lsp_document_store.hpp"
#include "lsp_pending_uri_set.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════════════════════
// ServerStateLock — RAII wrapper for safe access to LspServer shared state.
//
// Acquires state_mutex_ on construction and releases it on destruction.
// Provides typed accessors to the protected state (document store, analysis
// cache, pending URI queue) so that callers cannot access state without
// holding the lock.  Also produces a LockToken for DocumentStore methods.
//
// Two flavours:
//   ReadStateLock   — shared (read) access   (multiple concurrent readers)
//   WriteStateLock  — unique (write) access   (exclusive)
//
// Usage inside LspServer member functions:
//
//   {
//       ReadStateLock state(state_mutex_, doc_store_, analysis_cache_,
//                           pending_uris_);
//       const auto* content = state.documents().get_content(state.token(), uri);
//       auto cached = state.cache().find(uri);
//   }
//
//   {
//       WriteStateLock state(state_mutex_, doc_store_, analysis_cache_,
//                            pending_uris_);
//       state.documents().set_content(state.token(), uri, text);
//       state.cache().insert(uri, std::move(result));
//   }
// ═══════════════════════════════════════════════════════════════════════════

// Base template — shared by both lock flavours.
// `Lock` is either std::shared_lock or std::unique_lock.
// `DocStoreRef` is either `const DocumentStore&` or `DocumentStore&`.
// `CacheRef` is either `const LspAnalysisCache&` or `LspAnalysisCache&`.
// Both lock flavours pass `PendingUriSet&` since the set is internally
// thread-safe and may be mutated even under a shared (read) lock.
template <typename Lock, typename DocStoreRef, typename CacheRef> class StateLock {
public:
    StateLock(std::shared_mutex& mutex, DocStoreRef doc_store, CacheRef cache,
              PendingUriSet& pending_uris)
        : lock_(mutex), doc_store_(doc_store), cache_(cache), pending_uris_(pending_uris) {}

    ~StateLock() = default;

    StateLock(const StateLock&) = delete;
    StateLock& operator=(const StateLock&) = delete;
    StateLock(StateLock&&) = delete;
    StateLock& operator=(StateLock&&) = delete;

    // Marker token for DocumentStore methods that require proof of locking.
    [[nodiscard]] const LockToken& token() const {
        return lock_token_;
    }

    // Access the document store (const or mutable depending on lock flavour).
    [[nodiscard]] DocStoreRef documents() const {
        return doc_store_;
    }

    // Access the analysis cache (const or mutable depending on lock flavour).
    [[nodiscard]] CacheRef cache() const {
        return cache_;
    }

    // Access the pending URI set.
    [[nodiscard]] PendingUriSet& pending_uris() const {
        return pending_uris_;
    }

    // Release the lock early (before destruction).
    void unlock() {
        lock_.unlock();
    }

private:
    Lock lock_;
    LockToken lock_token_;
    DocStoreRef doc_store_;
    CacheRef cache_;
    PendingUriSet& pending_uris_;
};

// Shared (read) lock — concurrent readers, const access to doc_store and cache.
// The pending URI queue is always passed by mutable reference since it is
// internally thread-safe and may be updated even during read operations.
using ReadStateLock =
    StateLock<std::shared_lock<std::shared_mutex>, const DocumentStore&, const LspAnalysisCache&>;

// Unique (write) lock — exclusive access, mutable state.
using WriteStateLock =
    StateLock<std::unique_lock<std::shared_mutex>, DocumentStore&, LspAnalysisCache&>;

} // namespace luma::lsp

#endif // LUMA_LSP_SERVER_STATE_LOCK_HPP
