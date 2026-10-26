// ─────────────────────────────────────────────────────────────────────────────
// VM Exception Handler
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Provide the VM-facing exception-handler boundary.
//
// VMExceptionManager remains available as a storage-level compatibility type.
// This component owns the boundary used by VM execution and keeps the handler
// stack implementation out of the execution engine.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_VM_VM_EXCEPTION_HANDLER_HPP
#define LUMA_VM_VM_EXCEPTION_HANDLER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "runtime/vm/vm_exception_manager.hpp"

namespace luma {

// True when the handler whose code starts at `catch_offset` in `code` is a
// cleanup handler — the compiled form of a `finally` clause — rather than a
// user `catch` block.  TryCatch carries no such flag, so it is recovered from
// the handler's code: a cleanup handler keeps the error value the VM pushed
// for it on the stack while the finally body runs and ends by re-raising it
// with Op::Rethrow, whereas a catch block binds the error to its variable and
// pops it when the block ends.  The walk follows normal control flow from
// `catch_offset` (never another handler's exception edge), tracking the stack
// depth relative to the handler's own error value, and reports a cleanup
// handler when it reaches a Rethrow of exactly that value.  A path that pops
// the error value has left the handler and is not followed further.  Used only
// when a task cancellation is dispatched, so the walk is off the hot path.
[[nodiscard]] bool is_cleanup_handler(const std::vector<std::uint8_t>& code,
                                      std::size_t catch_offset);

class VMExceptionHandler {
public:
    static constexpr std::size_t k_max_depth{VMExceptionManager::k_max_depth};

    void push(ExceptionHandler handler);

    [[nodiscard]] ExceptionHandler pop();

    void discard();

    [[nodiscard]] bool has_handler_for(std::size_t base_depth) const;

    [[nodiscard]] const ExceptionHandler& current() const;

    [[nodiscard]] bool empty() const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

    void clear() noexcept;

    // Drop the handlers of frames at index >= frame_count (frames that have
    // returned or been unwound), together with their cancellation shields.
    void discard_frames_from(std::size_t frame_count) noexcept {
        manager_.discard_frames_from(frame_count);
    }

    // Cancellation shields — see VMExceptionManager.
    void push_cancel_shield(std::size_t frame_index, std::size_t stack_depth) {
        manager_.push_cancel_shield(frame_index, stack_depth);
    }

    void end_cancel_shields_from(std::size_t frame_index, std::size_t stack_depth) noexcept {
        manager_.end_cancel_shields_from(frame_index, stack_depth);
    }

    [[nodiscard]] bool is_cancel_shielded() const noexcept {
        return manager_.is_cancel_shielded();
    }

private:
    VMExceptionManager manager_;
};

} // namespace luma

#endif // LUMA_VM_VM_EXCEPTION_HANDLER_HPP
