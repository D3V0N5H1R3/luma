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

#include "runtime/vm/vm_exception_manager.hpp"

namespace luma {

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

private:
    VMExceptionManager manager_;
};

} // namespace luma

#endif // LUMA_VM_VM_EXCEPTION_HANDLER_HPP
