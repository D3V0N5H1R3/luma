// ─────────────────────────────────────────────────────────────────────────────
// IScopeLifecycle — Narrow interface for compile-time scope/function lifecycle
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Open and close lexical scopes, loops, and functions during
//   compilation, and expose the current scope and bytecode offset.
//
// Part of the ICompilationBackend interface-segregation (ISP) split.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_I_SCOPE_LIFECYCLE_HPP
#define LUMA_COMPILER_I_SCOPE_LIFECYCLE_HPP

#include <cstddef>
#include <string>

#include "runtime/compiler/compiled_function.hpp"

namespace luma {

struct CompilerScope;

// Compile-time scope, loop, and function lifecycle surface.
class IScopeLifecycle {
public:
    virtual ~IScopeLifecycle() = default;

    virtual void begin_scope() = 0;
    virtual void end_scope() = 0;
    virtual void begin_loop(std::size_t loop_start) = 0;
    virtual void end_loop() = 0;
    virtual void begin_function(const std::string& name, int arity) = 0;
    [[nodiscard]] virtual CompiledFunction end_function() = 0;
    [[nodiscard]] virtual CompilerScope& current_scope() = 0;
    [[nodiscard]] virtual const CompilerScope& current_scope() const = 0;
    [[nodiscard]] virtual std::size_t current_offset() const = 0;

protected:
    IScopeLifecycle() = default;
    IScopeLifecycle(const IScopeLifecycle&) = default;
    IScopeLifecycle& operator=(const IScopeLifecycle&) = default;
};

} // namespace luma

#endif // LUMA_COMPILER_I_SCOPE_LIFECYCLE_HPP
