#ifndef LUMA_DAP_LOCK_ORDERING_HPP
#define LUMA_DAP_LOCK_ORDERING_HPP

// ═══════════════════════════════════════════════════════════════════════════════
// DAP Lock Ordering Reference
// ═══════════════════════════════════════════════════════════════════════════════
//
// This header documents every mutex in the DAP debugger subsystem and the
// ordering rules that prevent deadlocks.  Include it where convenient; it
// pulls in no transitive headers and defines no symbols.
//
// ─── Ordered session locks ──────────────────────────────────────────────────
//
// These four locks participate in a strict acquisition order enforced at
// runtime in debug builds by OrderedLockGuard / OrderedUniqueLock (see
// dap_session_types.hpp).  Always acquire them in the order shown below:
//
//   Level 1  ThreadStateManager::states_mutex_  (DapLockId::ThreadStates)
//            Guards the thread registry map.  Must be acquired before any
//            per-thread lock.
//
//   Level 2  ThreadState::mutex           (DapLockId::PerThread)
//            Guards per-thread pause/step state and condition variable.
//            May be acquired while L1 is held.
//
//   Level 3  DebugExecutionEngine::config_mutex_  (DapLockId::Config)
//            Guards is_config_done_ / config_cv_ during startup.
//            Acquired in isolation (never nested in practice).
//
//   Leaf     DebugExecutionEngine::exception_mutex_ (DapLockId::Exception)
//            Guards last_exception_message_ / last_exception_is_caught_.
//            Must never be held while acquiring or holding any other
//            session lock, and no session lock may be acquired while it
//            is held.
//
// Violation of this order triggers an assertion failure in debug builds.
// See DapLockId and validate_dap_lock_order() in dap_session_types.hpp.
//
// ─── Why debug-only? ────────────────────────────────────────────────────────
//
// Lock ordering assertions use a thread_local bitmask (tl_dap_held_locks)
// that is read and written on every lock acquisition/release.  This is
// intentionally compiled out in release builds (#ifndef NDEBUG) because:
//
//   1. Lock ordering violations are programmer errors, not runtime faults.
//      They are deterministically reproducible in debug and CI builds.
//
//   2. The debug hook path (should_break → evaluate_step_mode) acquires
//      ThreadState::mutex on every source line.  Adding a thread_local
//      read + write per acquisition is measurable overhead at ~100k
//      hook calls/sec in tight loops.
//
//   3. The bitmask approach cannot detect violations caused by
//      non-deterministic scheduling without also adding timing jitter,
//      which would only be appropriate in a dedicated stress-test harness.
//
// If you need release-build lock diagnostics, consider using platform-
// specific tools (Thread Sanitizer, Helgrind) instead.
//
// ─── Leaf-level locks (independent) ─────────────────────────────────────────
//
// Each of the following mutexes protects a self-contained data structure and
// is never held while acquiring any other mutex (nor is any other mutex
// acquired while it is held).  They are therefore deadlock-free by
// construction and do not require ordered guards.
//
//   BreakpointManager::mutex_
//       Guards all breakpoint collections (source, function, data, exception).
//       std::mutex — plain lock_guard.
//
//   CompiledBreakpoint::cache_mutex_
//       Guards compiled condition/log-message caches.
//       std::shared_mutex — unique_lock for writes, shared_lock for reads.
//
//   CustomVisualizer::cache_mutex_
//       Guards the compiled-rule list and the type-name match cache.
//       std::mutex — plain lock_guard.
//
//   DapProtocolHandler::send_mutex_
//       Serialises DAP message sends to guarantee monotonic sequence numbers.
//       std::mutex — plain lock_guard.
//
//   DapTcpTransport::write_mutex_
//       Serialises raw TCP writes so messages are not interleaved.
//       std::mutex — plain lock_guard.
//
//   DebugOutputBuffer::mutex_   (debug_stream_utils.hpp)
//       Guards the line-buffered output buffer and flush callback.
//       std::mutex — plain lock_guard.
//
//   ExpressionEvaluator::cache_mutex_
//       Guards the LRU expression compilation cache.
//       std::mutex — plain lock_guard.
//
//   HotReloader::mutex_
//       Guards watched-file list and last-check timestamp.
//       std::mutex — plain lock_guard.
//
//   TimeTravel::mutex_
//       Guards the VM snapshot deque and instruction counter.
//       std::mutex — plain lock_guard.
//
//   VariableInspector::ref_mutex_
//       Guards variable-reference and frame-mapping registries.
//       std::mutex — plain lock_guard.  Never acquire any other mutex
//       while holding this lock.
//
// ─── Rules for adding new mutexes ───────────────────────────────────────────
//
// 1. If the new mutex is self-contained (no other lock is ever acquired
//    while it is held, and it is never acquired while holding another lock),
//    declare it as a leaf lock with a comment and add it to this list.
//
// 2. If the new mutex must be nested with an existing session lock, assign
//    it a DapLockId level, update validate_dap_lock_order(), and use
//    OrderedLockGuard to acquire it.
//
// 3. Never acquire a lower-level lock while holding a higher-level one.
//
// ═══════════════════════════════════════════════════════════════════════════════

#endif // LUMA_DAP_LOCK_ORDERING_HPP
