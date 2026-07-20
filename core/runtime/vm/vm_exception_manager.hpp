// ─────────────────────────────────────────────────────────────────────────────
// VM Exception Manager
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Manage the exception handler stack for the VM.
//
// Owns the stack of ExceptionHandler records that track active try/catch
// blocks during bytecode execution.  The VM pushes a handler when entering
// a try block (Op::TryCatch), pops it when leaving normally (Op::TryEnd),
// and queries/pops it when dispatching a caught exception (handle_exception).
//
// This class is a pure data manager — it does not touch the VM's value
// stack, call frames, or instruction pointer.  The actual exception
// dispatch logic (stack unwinding, frame restoration, IP redirection)
// remains in VM::handle_exception() because it is tightly coupled to
// the execution engine's internal state.
//
// Extracted from the VM class to reduce its responsibilities.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_VM_VM_EXCEPTION_MANAGER_HPP
#define LUMA_VM_VM_EXCEPTION_MANAGER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/resource_limits.hpp"

namespace luma {

struct ExceptionHandler {
    const std::uint8_t* catch_ip{nullptr};
    std::size_t frame_index{0};
    std::size_t stack_depth{0};
    std::size_t task_scope_depth{0};
};

/// Manages the exception handler stack within the VM.
///
/// Each active try/catch block registers an ExceptionHandler that records
/// where to jump on exception (catch_ip), which call frame it belongs to
/// (frame_index), the stack depth to restore (stack_depth), and the number of
/// active task_scope blocks at entry (task_scope_depth) so any scopes entered
/// inside the try can be cancelled and joined when an exception unwinds past
/// them.
///
/// The handler stack is a simple LIFO structure: handlers are pushed on
/// try-block entry and popped on normal exit or exception dispatch.
class VMExceptionManager {
public:
    static constexpr std::size_t k_max_depth{ResourceLimits::k_max_exception_handler_depth};

    /// Push a handler for a newly entered try block.
    /// Throws if the handler stack exceeds k_max_depth.
    void push_handler(ExceptionHandler handler);

    /// Pop and return the most recent handler.
    /// Precondition: the handler stack must not be empty.
    [[nodiscard]] ExceptionHandler pop_handler();

    /// Pop the most recent handler without returning it.
    /// Precondition: the handler stack must not be empty.
    void pop_handler_discard();

    /// Check whether a handler exists that belongs to a frame at or above
    /// the given base depth.  Used to determine if an exception will be
    /// caught or should propagate via re-throw.
    [[nodiscard]] bool has_handler_for(std::size_t base_depth) const;

    /// Return a const reference to the most recent handler.
    /// Precondition: the handler stack must not be empty.
    [[nodiscard]] const ExceptionHandler& current() const;

    [[nodiscard]] bool empty() const noexcept {
        return handlers_.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return handlers_.size();
    }

    /// Remove all handlers (used for test-isolation resets).
    void clear() noexcept {
        handlers_.clear();
    }

private:
    std::vector<ExceptionHandler> handlers_;
};

} // namespace luma

#endif // LUMA_VM_VM_EXCEPTION_MANAGER_HPP
