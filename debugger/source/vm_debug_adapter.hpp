#ifndef LUMA_DAP_VM_DEBUG_ADAPTER_HPP
#define LUMA_DAP_VM_DEBUG_ADAPTER_HPP

// ─────────────────────────────────────────────────────────────────────────────
// VMDebugAdapter — bridges the concrete VM to IVMControl and IVMIntrospection.
//
// Wraps a non-owning reference to a VM instance and implements both debugger
// interfaces.  Introspection methods delegate to a VMIntrospector constructed
// on-the-fly from the wrapped VM.
//
// This is the sole concrete implementation of IVMControl and IVMIntrospection.
// DebugExecutionEngine owns one long-lived instance (vm_adapter_) for the
// main VM, and creates short-lived instances in resume_thread() and
// resolve_stop_state() for per-thread introspection via ThreadState::vm.
//
// Lifetime: the adapter must not outlive the VM it wraps.
// ─────────────────────────────────────────────────────────────────────────────

#include "i_vm_control.hpp"
#include "i_vm_introspection.hpp"
#include "runtime/vm/vm_introspection.hpp"

namespace luma {
class VM;
} // namespace luma

namespace luma::dap {

class VMDebugAdapter final : public IVMControl, public IVMIntrospection {
public:
    explicit VMDebugAdapter(VM& vm);

    // ─── IVMControl ───

    void request_pause_check() override;
    void execute(const std::vector<CompiledFunction>& functions,
                 const CompiledFunction& top_level) override;
    void set_debug_callbacks(DebugCallbacks callbacks) override;

    // ─── IVMIntrospection ───

    [[nodiscard]] std::size_t frame_count() const override;
    [[nodiscard]] FrameLocation frame_location(std::size_t frame_index) const override;
    [[nodiscard]] std::vector<FrameLocation> stack_trace() const override;
    [[nodiscard]] std::vector<LocalVariable> locals(std::size_t frame_index) const override;
    [[nodiscard]] std::optional<LocalVariable> find_local(std::size_t frame_index,
                                                          const std::string& name) const override;
    [[nodiscard]] std::vector<UpvalueVariable> upvalues(std::size_t frame_index) const override;
    [[nodiscard]] bool has_upvalues(std::size_t frame_index) const override;
    [[nodiscard]] FrameLocation current_location() const override;
    [[nodiscard]] std::size_t depth() const override;

    // ─── Escape hatch ───
    // Provides access to the underlying VM for code that still needs
    // VM-specific features (hook installation, global_env, etc.).

    [[nodiscard]] VM& underlying_vm() {
        return vm_;
    }

    [[nodiscard]] const VM& underlying_vm() const {
        return vm_;
    }

private:
    [[nodiscard]] VMIntrospector introspector() const;

    VM& vm_;
};

} // namespace luma::dap

#endif // LUMA_DAP_VM_DEBUG_ADAPTER_HPP
