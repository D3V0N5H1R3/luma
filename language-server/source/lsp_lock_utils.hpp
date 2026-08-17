#ifndef LUMA_LSP_LOCK_UTILS_HPP
#define LUMA_LSP_LOCK_UTILS_HPP

// Consolidated lock utilities for the LSP server.
//
// Contains:
//   - with_shared_lock / with_unique_lock   — RAII lambda wrappers for bare mutexes
//   - with_shared_state / with_unique_state — RAII lambda wrappers for state locks

#include <concepts>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <variant>

#include "lsp_pending_uri_set.hpp"
#include "lsp_server_state_lock.hpp"

namespace luma::lsp {

// ─── Lock adapter functions ─────────────────────────────────

// Execute `fn` while holding a shared (read) lock on `mtx`.
template <typename Fn>
    requires std::invocable<Fn>
auto with_shared_lock(std::shared_mutex& mtx, Fn&& fn) {
    const std::shared_lock lock(mtx);
    return std::forward<Fn>(fn)();
}

// Execute `fn` while holding a unique (write) lock on `mtx`.
template <typename Fn>
    requires std::invocable<Fn>
auto with_unique_lock(std::shared_mutex& mtx, Fn&& fn) {
    const std::unique_lock lock(mtx);
    return std::forward<Fn>(fn)();
}

// Acquire a shared (read) lock on `mutex` and call fn(state).
// fn receives a reference to the ReadStateLock, giving access to
// documents(), cache(), pending_uris() and token().
template <typename Fn>
    requires std::invocable<Fn, ReadStateLock&>
auto with_shared_state(std::shared_mutex& mutex, const DocumentStore& doc_store,
                       const LspAnalysisCache& cache, PendingUriSet& pending_uris, Fn&& fn) {
    ReadStateLock state(mutex, doc_store, cache, pending_uris);
    return std::forward<Fn>(fn)(state);
}

// Acquire a unique (write) lock on `mutex` and call fn(state).
// fn receives a reference to the WriteStateLock, giving mutable access
// to documents(), cache(), pending_uris() and token().
template <typename Fn>
    requires std::invocable<Fn, WriteStateLock&>
auto with_unique_state(std::shared_mutex& mutex, DocumentStore& doc_store, LspAnalysisCache& cache,
                       PendingUriSet& pending_uris, Fn&& fn) {
    WriteStateLock state(mutex, doc_store, cache, pending_uris);
    return std::forward<Fn>(fn)(state);
}

} // namespace luma::lsp

#endif // LUMA_LSP_LOCK_UTILS_HPP
