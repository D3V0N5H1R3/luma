#ifndef LUMA_DAP_VM_ASSERT_HPP
#define LUMA_DAP_VM_ASSERT_HPP

// ─────────────────────────────────────────────────────────────────────────────
// LUMA_ASSERT_VM — assert that a ThreadState's VM pointer is non-null.
//
// Use this macro at call sites where vm is assumed to be active (i.e. on
// the execution thread, inside a debug hook that only fires while the VM
// is running).  It produces a clear assertion message if the invariant
// is violated.
//
// Do NOT use this on the protocol thread — there, vm may legitimately be
// null (e.g. after session teardown) and callers must null-check instead.
//
// See ThreadState documentation in dap_session_types.hpp for the full
// vm lifecycle description.
// ─────────────────────────────────────────────────────────────────────────────

#include <cassert>

// Assert that the vm pointer in a ThreadState (passed by reference) is
// non-null.  Fires in both debug and release builds via assert().
// Usage:  LUMA_ASSERT_VM(state)   — where state is a ThreadState&
#define LUMA_ASSERT_VM(state)                                                                      \
    assert((state).vm != nullptr &&                                                                \
           "VM must be active — see ThreadState::vm lifecycle in dap_session_types.hpp")

#endif // LUMA_DAP_VM_ASSERT_HPP
