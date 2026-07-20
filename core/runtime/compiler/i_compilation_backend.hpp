// ─────────────────────────────────────────────────────────────────────────────
// ICompilationBackend — Composed interface for compiler helper classes
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Compose the focused compiler-backend role interfaces into a
//   single aggregate that all compiler helper classes depend on, decoupling
//   them from the concrete Compiler type.
//
// Design: The backend surface is split (Interface Segregation Principle) into
//   focused role interfaces, each owning one concern:
//
//     IConstantEmitter   — emit(Op) / emit_constant(Value)        (pre-existing)
//     IBytecodeEmitter   — raw opcode/jump/loop emission + patching
//     IScopeLifecycle    — scope/loop/function begin/end + current scope/offset
//     IVariableManager   — local declaration/resolution + name interning/table
//     ISubCompiler       — recursive expression/statement compilation + folding
//     IDiagnosticSink    — error/warning reporting
//     IContextAccess     — CompilationContext access
//
//   ICompilationBackend inherits from all of them, so existing helpers that
//   take an ICompilationBackend& are unaffected, while new or refactored
//   helpers (and mocks/tests) can depend on only the narrow slice they use.
//   Every method continues to correspond to a method on CompilerAccess, which
//   implements the aggregate by delegating to the Compiler.
//
// Benefits:
//   1. Unit-testing: helper classes can be tested with a mock of just the
//      role(s) they consume, without spinning up a full Compiler.
//   2. Explicit dependency direction: helpers depend on abstract role
//      interfaces, not on the concrete Compiler or CompilerAccess type.
//   3. Alternative backends: a tree-walk interpreter backend or a
//      verification-only backend could implement the same interfaces.
//
// See also: CompilerAccess (compiler_access.hpp) for the concrete
//   implementation that delegates to Compiler.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_I_COMPILATION_BACKEND_HPP
#define LUMA_COMPILER_I_COMPILATION_BACKEND_HPP

#include "runtime/compiler/chunk.hpp"
#include "runtime/compiler/i_bytecode_emitter.hpp"
#include "runtime/compiler/i_constant_emitter.hpp"
#include "runtime/compiler/i_context_access.hpp"
#include "runtime/compiler/i_diagnostic_sink.hpp"
#include "runtime/compiler/i_scope_lifecycle.hpp"
#include "runtime/compiler/i_sub_compiler.hpp"
#include "runtime/compiler/i_variable_manager.hpp"

namespace luma {

// Aggregate compilation-backend interface, composed from focused role
// interfaces (see file header).  All compiler helper classes (PatternCompiler,
// LoopCompiler, etc.) depend on this interface — or, preferably, on the
// narrowest role interface that covers their needs — rather than on the
// concrete CompilerAccess or Compiler types, enabling mock-based testing and
// alternative backends.
class ICompilationBackend : public IConstantEmitter,
                            public IBytecodeEmitter,
                            public IScopeLifecycle,
                            public IVariableManager,
                            public ISubCompiler,
                            public IDiagnosticSink,
                            public IContextAccess {
public:
    ~ICompilationBackend() override = default;

    // ─── RAII scope guard ───
    // Uses virtual begin_scope/end_scope through the interface, so it works
    // with any ICompilationBackend implementation (concrete or mock).
    class ScopeGuard {
    public:
        explicit ScopeGuard(ICompilationBackend& backend) : backend_(backend) {
            backend_.begin_scope();
        }

        ~ScopeGuard() {
            backend_.end_scope();
        }

        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;

    private:
        ICompilationBackend& backend_;
    };

protected:
    // Protected constructor — only derived classes can construct.
    ICompilationBackend() = default;
    ICompilationBackend(const ICompilationBackend&) = default;
    ICompilationBackend& operator=(const ICompilationBackend&) = default;
};

} // namespace luma

#endif // LUMA_COMPILER_I_COMPILATION_BACKEND_HPP
