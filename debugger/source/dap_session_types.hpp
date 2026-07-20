#ifndef LUMA_DAP_SESSION_TYPES_HPP
#define LUMA_DAP_SESSION_TYPES_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Shared session types for the Luma DAP debugger.
//
// Extracted from debug_session.hpp so that ThreadStateManager and
// DebugExecutionEngine can include them without creating circular dependencies.
// ─────────────────────────────────────────────────────────────────────────────

#include <atomic>
#include <cassert>
#include <concepts>
#include <condition_variable>
#include <mutex>
#include <string>

#include "dap_types.hpp"

namespace luma {
class VM;
} // namespace luma

namespace luma::dap {

// ─── Thread ID convention ───
// Thread IDs are positive integers. The main execution thread is always ID 1.
// Task (concurrent) threads receive incrementing IDs starting at 2.
// ID 0 is reserved as a sentinel meaning "no thread" (see tl_debug_thread_id).
using ThreadId = int;
constexpr ThreadId k_no_thread_id = 0;
constexpr ThreadId k_main_thread_id = 1;

// ─── Session lifecycle state ───
// Single atomic enum replaces separate running_ / terminated_ bools,
// eliminating the race window where the two flags could be inconsistent.
enum class SessionState : int {
    Idle,       // Not started or fully cleaned up
    Running,    // VM is executing
    Terminated, // VM finished normally or with error
};

// ─── Step tracking state for single-stepping operations ───
struct StepState {
    StepMode mode{StepMode::None};
    std::size_t reference_depth{0};
    int reference_line{-1};
    int reference_file{-1};
};

// ─── Pending events waiting to be processed on the next VM hook ───
struct PendingEvents {
    bool stop_on_entry{false};
    bool pause{false};
    bool data_breakpoint{false};
    std::string data_breakpoint_name;
    std::string exception_message;
    bool exception_caught{false};
    int hit_breakpoint_id{0};
};

// ─── Per-thread debugging state ───
// Each thread (main + spawned tasks) has its own pause/step state.
//
// vm lifecycle:
//   - Set to non-null when the thread's VM is created:
//       • Main thread: in setup_vm_hooks() after VM construction.
//       • Task threads: in the task_spawn_hook callback.
//   - Remains non-null while the thread is actively executing.
//   - Set to nullptr in two situations:
//       • ThreadStateManager::remove_thread() — when a task exits.
//       • ThreadStateManager::null_all_vms() — during session teardown,
//         before the VM is destroyed, to prevent dangling pointers from
//         late DAP requests that still hold ThreadState handles.
//   - Callers on the protocol thread must always null-check vm before use.
//   - Callers on the execution thread (debug hooks) may assert non-null
//     via LUMA_ASSERT_VM when the hook is only invoked while the VM runs.
struct ThreadState {
    int thread_id{0};
    std::string name;

    // Non-owning VM pointer.  Only valid while the thread is alive
    // and the ThreadState is held in thread_states_.
    // See lifecycle documentation above.
    VM* vm{nullptr}; // GUARDED_BY(mutex)

    mutable std::mutex mutex;
    std::condition_variable cv;
    bool is_paused{false};               // GUARDED_BY(mutex)
    bool is_exception_terminated{false}; // GUARDED_BY(mutex)
    StepState step;                      // GUARDED_BY(mutex)
    PendingEvents pending;               // GUARDED_BY(mutex)
};

// ─── Lock ordering enforcement (debug builds only) ───
//
// DAP session lock ordering (always acquire in this order):
//   1. ThreadStateManager::states_mutex_  (thread_states_mutex_, L1)
//   2. ThreadState::mutex          (per-thread pause/step, L2)
//   3. config_mutex_               (startup only, L3)
//
// Leaf locks (never held simultaneously with the above):
//   - exception_mutex_  — guards last_exception_message/is_caught
//
// The RAII guards below enforce this ordering in debug builds via a
// thread-local bitmask that tracks which session locks are held and
// asserts the invariants on every acquisition.
//
// Design note: ordering assertions are intentionally compiled out in
// release builds (#ifndef NDEBUG).  Lock ordering violations are
// programming errors (not runtime faults) and are deterministically
// reproducible in debug/CI builds.  Enabling the checks in release
// would add a thread-local read + write on every lock acquisition —
// a measurable cost on the hot debug-hook path where ThreadState::mutex
// is acquired per source line.  See dap_lock_ordering.hpp for the
// full mutex inventory and ordering rules.

enum class DapLockId : unsigned {
    ThreadStates = 1 << 0, // thread_states_mutex_ (level 1)
    PerThread = 1 << 1,    // ThreadState::mutex   (level 2)
    Config = 1 << 2,       // config_mutex_        (level 3, never nested in practice)
    Exception = 1 << 3,    // exception_mutex_     (leaf — never held with others)
};

// ─── LockId concept ───
// Constrains the lock-ID type used with OrderedLockGuard / OrderedUniqueLock.
// The type must be a scoped enum (std::is_enum_v) and must be totally
// ordered (required for any future ordered-acquisition analysis).
template <typename T>
concept LockId = std::is_enum_v<T> && std::totally_ordered<T>;

static_assert(LockId<DapLockId>, "DapLockId must satisfy the LockId concept");

#ifndef NDEBUG
inline thread_local unsigned tl_dap_held_locks{0};

namespace detail {

inline void validate_dap_lock_order(DapLockId id) {
    const unsigned held = tl_dap_held_locks;

    switch (id) {
        case DapLockId::ThreadStates:
            assert(!(held & static_cast<unsigned>(DapLockId::PerThread)) &&
                   "Lock ordering violation: thread_states_mutex_ acquired "
                   "while holding ThreadState::mutex");
            break;
        case DapLockId::PerThread:
            assert(!(held & static_cast<unsigned>(DapLockId::Config)) &&
                   "Lock ordering violation: ThreadState::mutex acquired "
                   "while holding config_mutex_");
            assert(!(held & static_cast<unsigned>(DapLockId::Exception)) &&
                   "Lock ordering violation: ThreadState::mutex acquired "
                   "while holding exception_mutex_");
            break;
        case DapLockId::Config:
            assert(held == 0 && "Lock ordering violation: config_mutex_ must not be "
                                "nested with other session locks");
            break;
        case DapLockId::Exception:
            assert(held == 0 && "Lock ordering violation: exception_mutex_ must not be "
                                "nested with other session locks");
            break;
    }
}

inline void track_dap_lock_acquired(DapLockId id) {
    tl_dap_held_locks |= static_cast<unsigned>(id);
}

inline void track_dap_lock_released(DapLockId id) {
    tl_dap_held_locks &= ~static_cast<unsigned>(id);
}

[[nodiscard]] inline bool dap_lock_is_held(DapLockId id) {
    return (tl_dap_held_locks & static_cast<unsigned>(id)) != 0;
}

} // namespace detail
#endif // NDEBUG

// RAII lock guard with ordering enforcement in debug builds.
// LockIdType must satisfy the LockId concept (a totally-ordered enum).
// Ordering checks are applied when LockIdType is DapLockId; other
// conforming enum types are accepted and lock/unlock without ordering
// assertions.
template <LockId LockIdType> class OrderedLockGuard {
public:
    OrderedLockGuard(std::mutex& m, LockIdType id) : mutex_(m), id_(id) {
#ifndef NDEBUG
        if constexpr (std::same_as<LockIdType, DapLockId>) {
            detail::validate_dap_lock_order(id_);
        }
#endif
        mutex_.lock();
#ifndef NDEBUG
        if constexpr (std::same_as<LockIdType, DapLockId>) {
            detail::track_dap_lock_acquired(id_);
        }
#endif
    }

    ~OrderedLockGuard() {
#ifndef NDEBUG
        if constexpr (std::same_as<LockIdType, DapLockId>) {
            detail::track_dap_lock_released(id_);
        }
#endif
        mutex_.unlock();
    }

    OrderedLockGuard(const OrderedLockGuard&) = delete;
    OrderedLockGuard& operator=(const OrderedLockGuard&) = delete;
    OrderedLockGuard(OrderedLockGuard&&) = delete;
    OrderedLockGuard& operator=(OrderedLockGuard&&) = delete;

private:
    std::mutex& mutex_;
    LockIdType id_;
};

// RAII unique lock with ordering enforcement in debug builds.
// Exposes underlying() for condition_variable::wait().
template <LockId LockIdType> class OrderedUniqueLock {
public:
    OrderedUniqueLock(std::mutex& m, LockIdType id) : lock_(m, std::defer_lock), id_(id) {
#ifndef NDEBUG
        if constexpr (std::same_as<LockIdType, DapLockId>) {
            detail::validate_dap_lock_order(id_);
        }
#endif
        lock_.lock();
#ifndef NDEBUG
        if constexpr (std::same_as<LockIdType, DapLockId>) {
            detail::track_dap_lock_acquired(id_);
        }
#endif
    }

    ~OrderedUniqueLock() {
        if (lock_.owns_lock()) {
#ifndef NDEBUG
            if constexpr (std::same_as<LockIdType, DapLockId>) {
                detail::track_dap_lock_released(id_);
            }
#endif
        }
    }

    // Access the underlying unique_lock for condition_variable::wait().
    [[nodiscard]] std::unique_lock<std::mutex>& underlying() noexcept {
        return lock_;
    }

    OrderedUniqueLock(const OrderedUniqueLock&) = delete;
    OrderedUniqueLock& operator=(const OrderedUniqueLock&) = delete;
    // Move construction is defaulted so the lock can be returned by value and
    // stored in an owner (e.g. std::optional / a returned struct) while still
    // being held. The defaulted move transfers the underlying unique_lock
    // (moved-from becomes empty, so its destructor skips the release) and copies
    // the id, so the debug lock-tracking bit is acquired once and released once.
    // Move assignment stays deleted — only construction is needed.
    OrderedUniqueLock(OrderedUniqueLock&&) = default;
    OrderedUniqueLock& operator=(OrderedUniqueLock&&) = delete;

private:
    std::unique_lock<std::mutex> lock_;
    LockIdType id_;
};

// ─── Thread-local state ───
// Variables prefixed with tl_ are thread-local: each OS thread has its own
// independent copy.  They are used on the hot debug-hook path to avoid
// lock acquisition for per-thread identity (tl_debug_thread_id) and
// debug-build lock-order checking (tl_dap_held_locks).

// Thread-local debug thread ID.
inline thread_local int tl_debug_thread_id{0};

} // namespace luma::dap

#endif // LUMA_DAP_SESSION_TYPES_HPP
