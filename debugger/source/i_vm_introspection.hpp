#ifndef LUMA_DAP_I_VM_INTROSPECTION_HPP
#define LUMA_DAP_I_VM_INTROSPECTION_HPP

// ─────────────────────────────────────────────────────────────────────────────
// IVMIntrospection — abstract interface for read-only VM state queries.
//
// Decouples the debugger from the concrete VMIntrospector so that
// DebugExecutionEngine can be tested with mock implementations.
//
// All methods require the VM to be paused.  Implementations are not
// expected to be thread-safe for concurrent use with a running VM.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "runtime/vm/vm_introspection.hpp"

namespace luma::dap {

class IVMIntrospection {
public:
    virtual ~IVMIntrospection() = default;

    // Number of call frames on the stack.
    [[nodiscard]] virtual std::size_t frame_count() const = 0;

    // Source location for frame at `frame_index` (0 = bottom, N-1 = top).
    [[nodiscard]] virtual FrameLocation frame_location(std::size_t frame_index) const = 0;

    // All source locations from top to bottom (most-recent first).
    [[nodiscard]] virtual std::vector<FrameLocation> stack_trace() const = 0;

    // Local variables for a given frame.
    [[nodiscard]] virtual std::vector<LocalVariable> locals(std::size_t frame_index) const = 0;

    // Look up a single local variable by name in a frame.
    [[nodiscard]] virtual std::optional<LocalVariable>
    find_local(std::size_t frame_index, const std::string& name) const = 0;

    // Upvalues captured by the closure at a given frame.
    [[nodiscard]] virtual std::vector<UpvalueVariable> upvalues(std::size_t frame_index) const = 0;

    // Whether the frame has a closure with captured upvalues.
    [[nodiscard]] virtual bool has_upvalues(std::size_t frame_index) const = 0;

    // Current source location (top frame).
    [[nodiscard]] virtual FrameLocation current_location() const = 0;

    // Current call depth (number of frames).
    [[nodiscard]] virtual std::size_t depth() const = 0;
};

} // namespace luma::dap

#endif // LUMA_DAP_I_VM_INTROSPECTION_HPP
