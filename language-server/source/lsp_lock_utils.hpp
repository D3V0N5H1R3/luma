#ifndef LUMA_LSP_LOCK_UTILS_HPP
#define LUMA_LSP_LOCK_UTILS_HPP

// Consolidated lock utilities for the LSP server.
//
// Contains:
//   - with_shared_lock / with_unique_lock   — RAII lambda wrappers for bare mutexes
//   - with_shared_state / with_unique_state — RAII lambda wrappers for state locks
//   - SharedOrderedLockGuard / PlainOrderedLockGuard — acquire two mutexes in documented order
//
// Previously split across lsp_lock_adapters.hpp and lsp_ordered_lock_guard.hpp.

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

// ─── Ordered lock guards ────────────────────────────────────
//
// Acquire two mutexes in a fixed order to prevent deadlock.
//
// LspServer documents the following lock ordering:
//
//   1. state_mutex_    (std::shared_mutex — shared or unique)
//   2. write_mutex_    (std::mutex in LspTransportWrapper)
//
// CancellationManager has its own internal mutex and does NOT
// participate in the ordering above.
//
// SharedOrderedLockGuard acquires a shared_mutex (shared read) then a
// plain mutex.  Use when reading state while holding the write lock.
//
// PlainOrderedLockGuard acquires two plain mutexes in enforced order.
//
// Usage:
//   SharedOrderedLockGuard guard(state_mutex_, write_mutex_);
//   PlainOrderedLockGuard  guard(first_mutex_, second_mutex_);

// Acquire state_mutex (shared read) + a plain mutex in enforced order.
class SharedOrderedLockGuard {
public:
    SharedOrderedLockGuard(std::shared_mutex& first, std::mutex& second)
        : first_lock_(first), second_lock_(second) {}

    ~SharedOrderedLockGuard() = default;

    SharedOrderedLockGuard(const SharedOrderedLockGuard&) = delete;
    SharedOrderedLockGuard& operator=(const SharedOrderedLockGuard&) = delete;
    SharedOrderedLockGuard(SharedOrderedLockGuard&&) = delete;
    SharedOrderedLockGuard& operator=(SharedOrderedLockGuard&&) = delete;

private:
    std::shared_lock<std::shared_mutex> first_lock_;
    std::unique_lock<std::mutex> second_lock_;
};

// Acquire two plain mutexes in enforced order (first before second).
class PlainOrderedLockGuard {
public:
    PlainOrderedLockGuard(std::mutex& first, std::mutex& second)
        : first_lock_(first), second_lock_(second) {}

    ~PlainOrderedLockGuard() = default;

    PlainOrderedLockGuard(const PlainOrderedLockGuard&) = delete;
    PlainOrderedLockGuard& operator=(const PlainOrderedLockGuard&) = delete;
    PlainOrderedLockGuard(PlainOrderedLockGuard&&) = delete;
    PlainOrderedLockGuard& operator=(PlainOrderedLockGuard&&) = delete;

private:
    std::unique_lock<std::mutex> first_lock_;
    std::unique_lock<std::mutex> second_lock_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_LOCK_UTILS_HPP
