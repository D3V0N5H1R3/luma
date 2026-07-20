#ifndef LUMA_DAP_HANDLER_BASE_HPP
#define LUMA_DAP_HANDLER_BASE_HPP

// ─────────────────────────────────────────────────────────────────────────────
// DapHandler — common base for all DAP handler groups.
//
// All four handler groups (lifecycle, execution, breakpoints, inspection)
// hold a reference to DapHandlerContext and register their handlers with
// the DapServer dispatch table.  This base class documents the shared
// interface without forcing virtual dispatch overhead.
// ─────────────────────────────────────────────────────────────────────────────

#include "dap_handler_context.hpp"

namespace luma::dap {

// Base class for handler groups.  Provides the shared ctx_ member and
// documents that all handler groups follow the same construction pattern.
//
// NOTE: Handlers are not polymorphically dispatched — DapServer holds them
// by concrete type.  The base class exists to enforce the construction
// pattern, share documentation, and allow future uniform registration.
class DapHandler {
public:
    explicit DapHandler(DapHandlerContext& ctx) : ctx_(ctx) {}

    // Non-copyable, non-movable — handlers hold a reference to shared state.
    DapHandler(const DapHandler&) = delete;
    DapHandler& operator=(const DapHandler&) = delete;
    DapHandler(DapHandler&&) = delete;
    DapHandler& operator=(DapHandler&&) = delete;

protected:
    ~DapHandler() = default;

    DapHandlerContext& ctx_;
};

} // namespace luma::dap

#endif // LUMA_DAP_HANDLER_BASE_HPP
