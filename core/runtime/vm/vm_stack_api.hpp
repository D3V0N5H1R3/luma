#ifndef LUMA_VM_VM_STACK_API_HPP
#define LUMA_VM_VM_STACK_API_HPP

#include <cstdint>
#include <string_view>

#include "runtime/interpreter/value.hpp"

namespace luma {

struct CallFrame; // vm_stack.hpp
class TaskScope;  // concurrency/task_scope.hpp

// ─────────────────────────────────────────────────────────────────────────────
// VMStackAPI — the narrow, mockable contract that bytecode dispatch handlers
// depend on, instead of reaching into VM's private members (stack_,
// task_manager_, exceptions_, …) directly through `this`.
//
// Introduced per TODO(refactor) in vm.hpp.  The VM implements this interface,
// so existing handlers keep working unchanged, while new or extracted handlers
// can take a `VMStackAPI&` and be unit-tested against a lightweight mock — no
// full VM instance required.  See vm_stack_api_test.cpp for a worked example.
//
// Performance note: the VM is declared `final`, so calls made on a statically
// known VM (every `this->push()` in the dispatch loop) devirtualize back to
// direct calls under optimisation — the seam adds no hot-path overhead.
// ─────────────────────────────────────────────────────────────────────────────
class VMStackAPI {
public:
    VMStackAPI() = default;
    virtual ~VMStackAPI() = default;

    VMStackAPI(const VMStackAPI&) = delete;
    VMStackAPI& operator=(const VMStackAPI&) = delete;
    VMStackAPI(VMStackAPI&&) = delete;
    VMStackAPI& operator=(VMStackAPI&&) = delete;

    // ─── Value stack ───
    virtual void push(Value value) = 0;
    [[nodiscard]] virtual Value pop() = 0;
    [[nodiscard]] virtual Value& peek(std::size_t distance = 0) = 0;

    // ─── Frame / scope context ───
    // The innermost active call frame (top of the frame stack).
    [[nodiscard]] virtual const CallFrame& current_frame() const = 0;
    // The active task scope for this thread, or nullptr outside task_scope { }.
    [[nodiscard]] virtual TaskScope* current_task_scope() const noexcept = 0;

    // ─── Operand readers (advance the current frame's instruction pointer) ───
    [[nodiscard]] virtual std::uint8_t read_byte() = 0;
    [[nodiscard]] virtual std::uint16_t read_u16() = 0;
    [[nodiscard]] virtual std::uint32_t read_u32() = 0;

    // ─── Error reporting ───
    // Abort the current execution with a runtime error.
    [[noreturn]] virtual void runtime_error(std::string_view message,
                                            std::string_view hint = {}) const = 0;
};

} // namespace luma

#endif // LUMA_VM_VM_STACK_API_HPP
