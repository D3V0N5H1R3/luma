#ifndef LUMA_DAP_I_VM_CONTROL_HPP
#define LUMA_DAP_I_VM_CONTROL_HPP

// ─────────────────────────────────────────────────────────────────────────────
// IVMControl — abstract interface for VM execution control.
//
// Decouples the debugger from the concrete VM class so that
// DebugExecutionEngine can be tested with mock implementations.
//
// Covers two concerns:
//   1. Execution — starting and running a compiled program.
//   2. Pause signalling — requesting the VM to check for a pause
//      at the next source-line change.
//
// ─── Abstraction boundary ──────────────────────────────────────────────────
//
// DebugExecutionEngine interacts with the VM exclusively through IVMControl
// and IVMIntrospection.  The concrete VMDebugAdapter implements both
// interfaces and is the only component that touches the VM directly.
//
// ThreadState::vm is a raw VM* used for per-thread pause signalling
// (request_pause_check) and ad-hoc introspection during stepping.  This
// coupling is intentional: each thread needs its own VM reference because
// Luma tasks spawn independent VM instances.  The raw pointer is guarded
// by ThreadState::mutex and is nulled by ThreadStateManager::null_all_vms()
// before the VM is destroyed.
//
// No additional proxy layer is needed — the IVMControl / IVMIntrospection
// interfaces already provide the abstraction seam for testing.
// ─────────────────────────────────────────────────────────────────────────────

#include <vector>

#include "runtime/vm/vm_debug_types.hpp"

namespace luma {
struct CompiledFunction;
} // namespace luma

namespace luma::dap {

class IVMControl {
public:
    virtual ~IVMControl() = default;

    // Signal the VM to check for a pause at the next line change.
    // Thread-safe: may be called from the protocol thread while the VM runs.
    virtual void request_pause_check() = 0;

    // Execute a compiled program to completion.
    virtual void execute(const std::vector<CompiledFunction>& functions,
                         const CompiledFunction& top_level) = 0;

    // Install all debug callbacks at once.
    virtual void set_debug_callbacks(DebugCallbacks callbacks) = 0;
};

} // namespace luma::dap

#endif // LUMA_DAP_I_VM_CONTROL_HPP
