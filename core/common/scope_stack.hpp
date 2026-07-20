#ifndef LUMA_COMMON_SCOPE_STACK_HPP
#define LUMA_COMMON_SCOPE_STACK_HPP

#include <concepts>
#include <stdexcept>
#include <vector>

namespace luma {

// ─────────────────────────────────────────────────────────────────────────────
// §Component Usage — how the analysis/runtime scopes relate, and why they are
// not unified into a single Scope<Symbol> / ScopeBase<T> base.
// ─────────────────────────────────────────────────────────────────────────────
// This is the single authoritative rationale; resolver.hpp, linter_tracker.hpp,
// and type_info.hpp point here instead of repeating it.
//
// Components that USE ScopeStack<T>:
//   - LinterTracker (analysis/linter/linter_tracker.hpp)
//       ScopeStack<ScopeData> — a flat variable-usage tracking stack.  ScopeData
//       has no parent pointers or cross-scope lookup, so the vector-based
//       push/pop model is a natural fit.
//
// Components with CUSTOM scope implementations (cannot use ScopeStack<T>):
//   - ResolveScope (analysis/resolver/resolver.hpp)
//       shared_ptr-linked parent chain.  lookup() traverses parent pointers to
//       resolve variables across enclosing scopes, adjusting frame_depth at each
//       hop — a flat vector cannot express those parent relationships.
//   - TypeScope (analysis/types/type_info.hpp)
//       shared_ptr-linked parent chain.  Tracks ownership state
//       (unique/borrow/consumed) with flow-sensitive snapshot/restore for
//       branching control flow; the parent chain and snapshot/restore are
//       tightly coupled and cannot be replaced by a flat stack.
//   - Compiler scope stack (runtime/compiler/compiler.hpp)
//       std::vector<CompilerScope> directly: upvalue resolution needs indexed
//       access (scope_stack[i]).  ScopeStack offers at(), but the compiler
//       predates it and migrating offers no benefit.
//
// Why no shared base: the scopes differ in (1) element type — ResolvedVar (slot
// index, frame_depth, mutability) vs VariableInfo (location, usage flags)
// vs SymbolInfo (type + ownership); (2) architecture — shared_ptr parent-chain
// vs flat vector; (3) frame-depth accounting — ResolveScope tracks it per hop,
// the others have no equivalent.  ScopeStack<T> already covers the generic
// push/pop pattern; the per-pass symbol semantics stay specialised.
//
// The push_scope / pop_scope RAII lifecycle IS now unified (was
// TODO(refactor/A10)): analysis/common/scope_manager.hpp provides the
// ScopeManager<Derived> CRTP mixin — the "IScopeManager" seam that standardises
// only the make_scope_guard() enter/exit lifecycle without forcing a shared
// storage layout.  NameResolver and the Linter inherit it and keep their
// distinct scope element types and architectures above.  The TypeChecker
// deliberately stays out: it drives push/pop through its own virtual
// IScopeService role interface (see analysis/types/type_checking_services_roles.hpp).
// ─────────────────────────────────────────────────────────────────────────────

// Generic scope stack with RAII guard for scope entry/exit.
template <std::movable T> class ScopeStack {
public:
    // Push a new scope.
    void push(T scope = T{}) {
        scopes_.push_back(std::move(scope));
    }

    // Pop the most recent scope. Throws if stack is empty.
    void pop() {
        if (scopes_.empty()) {
            throw std::logic_error("ScopeStack::pop() on empty stack");
        }
        scopes_.pop_back();
    }

    // Access the current (top) scope. Throws if stack is empty.
    [[nodiscard]] T& current() {
        if (scopes_.empty()) {
            throw std::logic_error("ScopeStack::current() on empty stack");
        }
        return scopes_.back();
    }

    [[nodiscard]] const T& current() const {
        if (scopes_.empty()) {
            throw std::logic_error("ScopeStack::current() on empty stack");
        }
        return scopes_.back();
    }

    // Access the current (top) scope without throwing.
    // Returns a pointer to the top scope, or nullptr if the stack is empty.
    [[nodiscard]] T* try_current() noexcept {
        return scopes_.empty() ? nullptr : &scopes_.back();
    }

    [[nodiscard]] const T* try_current() const noexcept {
        return scopes_.empty() ? nullptr : &scopes_.back();
    }

    // Access a scope by depth (0 = bottom, size()-1 = top).
    [[nodiscard]] T& at(std::size_t depth) {
        return scopes_.at(depth);
    }

    [[nodiscard]] const T& at(std::size_t depth) const {
        return scopes_.at(depth);
    }

    [[nodiscard]] std::size_t depth() const noexcept {
        return scopes_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return scopes_.empty();
    }

    // Remove all scopes.
    void clear() noexcept {
        scopes_.clear();
    }

    // ─── Iteration ──────────────────────────────────────────────────────────
    // Forward iteration visits scopes from outermost to innermost.
    // Reverse iteration visits from innermost to outermost (useful for
    // name resolution, which searches the current scope first).
    [[nodiscard]] auto begin() {
        return scopes_.begin();
    }

    [[nodiscard]] auto end() {
        return scopes_.end();
    }

    [[nodiscard]] auto begin() const {
        return scopes_.begin();
    }

    [[nodiscard]] auto end() const {
        return scopes_.end();
    }

    [[nodiscard]] auto rbegin() {
        return scopes_.rbegin();
    }

    [[nodiscard]] auto rend() {
        return scopes_.rend();
    }

    [[nodiscard]] auto rbegin() const {
        return scopes_.rbegin();
    }

    [[nodiscard]] auto rend() const {
        return scopes_.rend();
    }

    // RAII guard: pushes scope on construction, pops on destruction.
    class Guard {
    public:
        explicit Guard(ScopeStack& stack, T scope = T{}) : stack_{stack} {
            stack_.push(std::move(scope));
        }

        ~Guard() {
            stack_.pop();
        }

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

    private:
        ScopeStack& stack_;
    };

private:
    std::vector<T> scopes_;
};

} // namespace luma

#endif // LUMA_COMMON_SCOPE_STACK_HPP
