#ifndef LUMA_DAP_THREAD_STATE_MANAGER_HPP
#define LUMA_DAP_THREAD_STATE_MANAGER_HPP

// ─────────────────────────────────────────────────────────────────────────────
// ThreadStateManager — owns the per-thread debugging state registry.
//
// Centralises all thread list operations (add, remove, lookup, iteration)
// together with the paused-thread counter and VM signalling, behind a single
// lock domain.  Callers receive typed RAII guards rather than raw mutexes.
// ─────────────────────────────────────────────────────────────────────────────

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dap_session_types.hpp"

namespace luma::dap {

// ═══════════════════════════════════════════════════════════
// ThreadStateManager — owns the thread registry and pause counter.
//
// All public methods are thread-safe unless documented otherwise.
// ═══════════════════════════════════════════════════════════

class ThreadStateManager {
public:
    ThreadStateManager() = default;

    // ─── Thread registration ───

    // Remove all registered threads (called before re-launching).
    void clear();

    // Add a thread to the registry.  The thread_id must be unique.
    void add_thread(std::shared_ptr<ThreadState> ts);

    // Remove a thread: nulls its VM pointer, adjusts the paused count if the
    // thread was paused, then erases it from the registry.
    void remove_thread(int thread_id);

    // Null all VM pointers without removing threads.  Call before the VM
    // is destroyed to prevent dangling-pointer dereferences from late DAP
    // requests that still hold thread-state handles.
    void null_all_vms();

    // Unpause every thread, notify its CV, and request a pause check.
    // Used by terminate() to wake threads blocked in wait_for_resume().
    void force_unpause_all();

    // ─── Thread lookup ───

    [[nodiscard]] std::shared_ptr<ThreadState> get_thread(int thread_id) const;

    // Check if a thread ID is valid (exists in the registry).
    [[nodiscard]] bool is_thread_valid(int thread_id) const;

    // Returns the ThreadState for the calling thread (via tl_debug_thread_id).
    [[nodiscard]] std::shared_ptr<ThreadState> current_thread() const;

    [[nodiscard]] std::vector<std::pair<int, std::string>> get_threads() const;

    // ─── Pause tracking ───

    [[nodiscard]] bool all_threads_stopped() const;

    void increment_paused_count();
    void decrement_paused_count();

    [[nodiscard]] int paused_count() const noexcept;

    // Count paused threads while thread_states_mutex_ is already held.
    // Caller MUST hold lock_states() before calling this.
    [[nodiscard]] int count_paused_locked() const;

    // ─── VM signalling ───

    void signal_all_vms_pause_check();

    // ─── Snapshot ───

    // Return a point-in-time snapshot of all thread states.
    // Used by callers that need to iterate without holding the registry lock.
    [[nodiscard]] std::vector<std::shared_ptr<ThreadState>> all_threads_snapshot() const;

    // ─── Lock helpers ───

    [[nodiscard]] OrderedLockGuard<DapLockId> lock_states() const;
    [[nodiscard]] OrderedLockGuard<DapLockId> lock_state(ThreadState& ts) const;
    [[nodiscard]] OrderedUniqueLock<DapLockId> lock_state_unique(ThreadState& ts) const;

private:
    // Lock ordering (enforced in debug builds by OrderedLockGuard):
    //   thread_states_mutex_ (L1) < ThreadState::mutex (L2) < config_mutex_ (L3)
    //   exception_mutex_ is a leaf lock — never nested with any of the above.
    //
    // Callers must not hold a higher-level lock (L2 or L3) when acquiring
    // lock_states(), and must not hold L3 when acquiring lock_state().
    // See DapLockId in dap_session_types.hpp for the full definition.
    mutable std::mutex states_mutex_;
    std::unordered_map<int, std::shared_ptr<ThreadState>>
        thread_state_map_; // GUARDED_BY(states_mutex_)
    std::atomic<int> paused_count_{0};
};

} // namespace luma::dap

#endif // LUMA_DAP_THREAD_STATE_MANAGER_HPP
