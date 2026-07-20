// ─────────────────────────────────────────────────────────────────────────────
// Type Checking Context
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Define the shared state and service interface used by all
// type checking sub-components (ExpressionTypeChecker, StatementTypeChecker,
// GenericResolver, StdlibTypeHandler).
//
// Two types are defined here:
//
//   TypeCheckingContext   — Transient, per-pass state that changes during a
//                           single type-check pass and is reset at the start
//                           of each check() call.  Groups fields that were
//                           previously scattered across the TypeChecker class.
//
//   TypeCheckingServices  — Abstract interface that exposes the cross-cutting
//                           services sub-checkers need from their host.
//                           Currently the sub-checkers hold a TypeChecker&
//                           back-reference and use friend access.  This
//                           interface captures that implicit contract so
//                           that future refactoring can decouple them.
//
// Design notes:
//   - TypeCheckingContext is a plain data bundle (struct).  It carries no
//     behaviour beyond a reset() convenience method.
//   - TypeCheckingServices is a pure-virtual interface.  TypeChecker is the
//     only intended implementation.  The interface exists to document the
//     API surface consumed by sub-checkers and to enable testing with
//     lightweight stubs in the future.
//   - Neither type owns AST nodes.  All AST pointers are non-owning and
//     valid only during a single type-check pass (the Program must outlive
//     the TypeChecker).
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "analysis/types/type_checking_services_roles.hpp"
#include "analysis/types/type_info.hpp"
#include "common/scope_guard.hpp"

namespace luma {

// ─────────────────────── Per-Pass Transient State ───────────────────────

// Transient state for a single type-checking pass.
// Reset at the beginning of TypeChecker::check().
struct TypeCheckingContext {
    // ─── Scope tracking ───
    std::shared_ptr<TypeScope> current_scope;

    // ─── Function context ───
    std::optional<TypeInfo> current_return_type;
    int main_count{0};
    bool is_in_main{false};

    // ─── Expression context ───
    bool is_in_pipe{false};

    // ─── Nesting depths ───
    int loop_depth{0};
    int task_scope_depth{0};
    int type_resolve_depth{0};

    // ─── Namespace context ───
    // Namespace currently being type-checked (set while visiting a namespace's
    // members). Used to allow access to internal members from within.
    std::string current_namespace;

    // Reset all fields for a fresh type-check pass.
    void reset() {
        *this = TypeCheckingContext{};
    }
};

// ─────────────────────── Shared Services Interface ───────────────────────

// Aggregate interface exposing the cross-cutting services that type checking
// sub-components (ExpressionTypeChecker, StatementTypeChecker,
// GenericResolver) consume from their host.
//
// The services are split (Interface Segregation Principle) into focused role
// interfaces in type_checking_services_roles.hpp; TypeCheckingServices
// composes them by inheritance so that:
//
//   1. Documentation — the implicit contract between sub-checkers and the
//      host is now explicit, auditable, and grouped by concern.
//   2. Testability — a future lightweight stub can implement only the role(s)
//      a sub-checker consumes, unit-testing it in isolation without
//      instantiating a full TypeChecker.
//   3. Narrowing — a sub-checker can be refactored to depend on only the role
//      interface it actually uses, instead of the whole aggregate.
//
// The composed roles (each mirroring one concern group):
//   - IPassContextService        Per-pass context access
//   - IDiagnosticsService        Diagnostics (errors, warnings, suggestions)
//   - IScopeService              Scope management
//   - ITypeResolutionService     Type resolution and compatibility
//   - IExpressionInferenceService Expression type inference
//   - IStatementCheckingService  Statement checking
//   - ISymbolRegistryService     Symbol lookup (find-by-name + registry iteration)
//   - IMatchAnalysisService      Match exhaustiveness
//   - IGenericsService           Generic inference
//   - IStdlibTypeService         Standard library type metadata
//   - IRefinementService         Flow-sensitive type refinements
//
// This class adds only the non-virtual convenience helpers that compose
// across roles (scope guard, variable lookup).  TypeChecker is the only
// production implementation.  The virtual destructor ensures correct cleanup
// through base pointers if stubs are used in tests.

class TypeCheckingServices : public IPassContextService,
                             public IDiagnosticsService,
                             public IScopeService,
                             public ITypeResolutionService,
                             public IExpressionInferenceService,
                             public IStatementCheckingService,
                             public ISymbolRegistryService,
                             public IMatchAnalysisService,
                             public IGenericsService,
                             public IStdlibTypeService,
                             public IRefinementService {
public:
    ~TypeCheckingServices() override = default;

    // Non-virtual convenience: push a scope and return an RAII guard
    // that pops it on destruction.
    [[nodiscard]] auto make_scope_guard() {
        push_scope();
        return ScopeGuard{[this] {
            pop_scope();
        }};
    }

    // Convenience wrappers over context().current_scope->lookup / lookup_mut.
    // Prefer these over the verbose three-part path when looking up a named
    // symbol in the active lexical scope.  Returns nullptr if no scope is
    // active (which should not occur during a normal check() pass).  They live
    // on the aggregate because they compose IPassContextService::context()
    // with the active lexical scope.

    [[nodiscard]] const SymbolInfo* lookup_variable(std::string_view name) const {
        const auto& scope = context().current_scope;
        return scope ? scope->lookup(name) : nullptr;
    }

    [[nodiscard]] SymbolInfo* lookup_variable_mut(std::string_view name) {
        const auto& scope = context().current_scope;
        return scope ? scope->lookup_mut(name) : nullptr;
    }

protected:
    TypeCheckingServices() = default;
    TypeCheckingServices(const TypeCheckingServices&) = default;
    TypeCheckingServices& operator=(const TypeCheckingServices&) = default;
    TypeCheckingServices(TypeCheckingServices&&) = default;
    TypeCheckingServices& operator=(TypeCheckingServices&&) = default;
};

} // namespace luma
