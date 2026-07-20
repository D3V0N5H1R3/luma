// ─────────────────────────────────────────────────────────────────────────────
// IContextAccess — Narrow interface for compilation context access
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Expose the shared CompilationContext (current function,
//   chunk, scope stack, and related per-compilation state).
//
// Part of the ICompilationBackend interface-segregation (ISP) split.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_I_CONTEXT_ACCESS_HPP
#define LUMA_COMPILER_I_CONTEXT_ACCESS_HPP

namespace luma {

struct CompilationContext;

// Compilation-context access surface.
class IContextAccess {
public:
    virtual ~IContextAccess() = default;

    [[nodiscard]] virtual CompilationContext& ctx() = 0;
    [[nodiscard]] virtual const CompilationContext& ctx() const = 0;

protected:
    IContextAccess() = default;
    IContextAccess(const IContextAccess&) = default;
    IContextAccess& operator=(const IContextAccess&) = default;
};

} // namespace luma

#endif // LUMA_COMPILER_I_CONTEXT_ACCESS_HPP
