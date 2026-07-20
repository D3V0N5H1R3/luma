#ifndef LUMA_ANALYSIS_COMMON_SCOPE_MANAGER_HPP
#define LUMA_ANALYSIS_COMMON_SCOPE_MANAGER_HPP

#include "common/scope_guard.hpp"

namespace luma {

// ─────────────────────────────────────────────────────────────────────────────
// ScopeManager — the IScopeManager seam from TODO(refactor/A10).
// ─────────────────────────────────────────────────────────────────────────────
// Standardises ONLY the push_scope() / pop_scope() RAII lifecycle shared by the
// analysis passes (NameResolver, Linter), without dictating how scopes are
// stored.  A derived pass supplies push_scope() / pop_scope() backed by whatever
// structure fits its semantics — NameResolver a shared_ptr-linked parent chain,
// the Linter a flat ScopeStack<ScopeData> — and inherits a single
// make_scope_guard() that brackets a block with a fresh lexical scope and closes
// it on exit (including on exception).
//
// The element type, parent-chain architecture, and frame-depth accounting stay
// specialised per pass; this seam deliberately unifies the guard lifecycle only.
// See common/scope_stack.hpp §Component Usage for the full rationale on why the
// scopes are not unified into a shared storage layout.
//
// A CRTP template — rather than a virtual interface — keeps the hot analysis
// path free of vtable dispatch and heap allocation.  The mixin reaches the
// derived pass's (typically private) push_scope() / pop_scope() through the
// static downcast, so the pass must befriend it:
//
//     class Foo : public ScopeManager<Foo> {
//         friend class ScopeManager<Foo>;
//         void push_scope();
//         void pop_scope();
//     };
// ─────────────────────────────────────────────────────────────────────────────
template <typename Derived> class ScopeManager {
protected:
    // Non-polymorphic mixin: a protected, non-virtual destructor prevents
    // deletion through a ScopeManager* while avoiding vtable overhead.
    ~ScopeManager() = default;

    // Open a fresh lexical scope now and close it when the returned guard is
    // destroyed (end of the enclosing block).  Mirrors the previously per-pass
    // make_scope_guard() idiom.
    [[nodiscard]] auto make_scope_guard() {
        auto* self = static_cast<Derived*>(this);
        self->push_scope();
        return ScopeGuard{[self] {
            self->pop_scope();
        }};
    }
};

} // namespace luma

#endif // LUMA_ANALYSIS_COMMON_SCOPE_MANAGER_HPP
