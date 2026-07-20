#ifndef LUMA_VM_INTROSPECTION_HPP
#define LUMA_VM_INTROSPECTION_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "runtime/interpreter/value.hpp"

namespace luma {

class VM;
struct CallFrame;

// ═══════════════════════════════════════════════════════════
// VM introspection — high-level state access for debuggers.
//
// Encapsulates the slot arithmetic and frame-walking logic
// that would otherwise be duplicated in every debugger query.
// All methods require the VM to be paused (not thread-safe
// for concurrent use with a running VM).
//
// ─── Thread safety ──────────────────────────────────────────
// VMIntrospector is designed for use ONLY while the VM is
// paused (e.g., the debugger has halted the execution loop).
// It holds a const-reference to the VM; concurrent mutation
// of VM state while an introspector exists is undefined
// behaviour.  Do not use from a second thread while the VM
// is running.
//
// ─── Lifetime ───────────────────────────────────────────────
// The introspector is a non-owning view.  Its lifetime must
// not exceed the VM's pause period.  Destroy or discard the
// introspector before resuming VM execution.
//
// ─── Mutating methods ───────────────────────────────────────
// Most methods are read-only (const).  The following static
// methods mutate VM state:
//   set_local()   — writes a new value to a local variable slot.
//   set_upvalue() — writes a new value to an upvalue slot.
// These are intended for debugger "set variable" operations
// and require a mutable VM reference.
// ═══════════════════════════════════════════════════════════

// A single local variable at a resolved stack slot.
struct LocalVariable {
    std::string name;
    Value value;
    bool is_mutable{false};
};

// A single upvalue captured by a closure.
struct UpvalueVariable {
    std::string name;
    Value value;
    bool is_mutable{false};
};

// Source location for a call frame.
struct FrameLocation {
    std::string function_name;
    int file_id{-1};
    int line{0};
    int column{0};
};

// Non-owning view into a paused VM's state.
// Created via vm_introspect(vm).  Lifetime must not exceed
// the VM's pause period.
class VMIntrospector {
public:
    explicit VMIntrospector(const VM& vm);

    // Number of call frames on the stack.
    [[nodiscard]] std::size_t frame_count() const;

    // Source location for frame at `frame_index` (0 = bottom, N-1 = top).
    [[nodiscard]] FrameLocation frame_location(std::size_t frame_index) const;

    // All source locations from top to bottom (most-recent first).
    [[nodiscard]] std::vector<FrameLocation> stack_trace() const;

    // Local variables for a given frame.
    [[nodiscard]] std::vector<LocalVariable> locals(std::size_t frame_index) const;

    // Look up a single local variable by name in a frame.
    [[nodiscard]] std::optional<LocalVariable> find_local(std::size_t frame_index,
                                                          const std::string& name) const;

    // Upvalues captured by the closure at a given frame.
    [[nodiscard]] std::vector<UpvalueVariable> upvalues(std::size_t frame_index) const;

    // Whether the frame has a closure with captured upvalues.
    [[nodiscard]] bool has_upvalues(std::size_t frame_index) const;

    // Current source location (top frame).
    [[nodiscard]] FrameLocation current_location() const;

    // Current call depth (frames().size()).
    [[nodiscard]] std::size_t depth() const;

    // Write a new value to a local variable slot.
    // Returns false if the variable is not found or immutable.
    [[nodiscard]] static bool set_local(VM& vm, std::size_t frame_index, const std::string& name,
                                        const Value& new_value);

    // Write a new value to an upvalue slot.
    // Returns false if the upvalue is not found or immutable.
    [[nodiscard]] static bool set_upvalue(VM& vm, std::size_t frame_index, const std::string& name,
                                          const Value& new_value);

private:
    const VM& vm_;

    // Resolve upvalue names by walking parent frames.
    [[nodiscard]] std::vector<std::string> resolve_upvalue_names(std::size_t frame_index) const;

    // Resolve upvalue mutability by walking parent frames.
    [[nodiscard]] std::vector<bool> resolve_upvalue_mutability(std::size_t frame_index) const;

    // Compute the stack slot range [start, end) for a frame.
    struct SlotRange {
        std::size_t start{0};
        std::size_t end{0};
    };

    [[nodiscard]] SlotRange slot_range(std::size_t frame_index) const;
};

// Convenience factory.
[[nodiscard]] inline VMIntrospector vm_introspect(const VM& vm) {
    return VMIntrospector(vm);
}

} // namespace luma

#endif // LUMA_VM_INTROSPECTION_HPP
