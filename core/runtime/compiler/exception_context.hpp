// ─────────────────────────────────────────────────────────────────────────────
// ExceptionContext — tracks active try/catch/finally blocks during compilation.
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Manage the stack of active try blocks within a single
//   compiler scope so that break, continue, and return can correctly unwind
//   exception handlers and emit pending finally blocks.
//
// Extracted from CompilerScope to give exception-handler bookkeeping a named
// abstraction with a narrow interface.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <cstddef>
#include <vector>

#include "analysis/ast/statement.hpp"

namespace luma {

// Tracks active try/catch/finally blocks within a single compiler scope.
//
// Each time the compiler enters a try block it pushes a TryInfo entry;
// when the try body completes normally the entry is popped.  During
// break/continue/return the context is queried so that the compiler
// can emit TryEnd opcodes and inline finally bodies before control
// leaves the current scope.
class ExceptionContext {
public:
    // State for a single active try block.
    struct TryInfo {
        const std::vector<StatementPtr>* finally_body; // nullptr if no finally block.
    };

    // Enter a try block.  `finally_body` is non-null when the try has a
    // finally clause whose body must be inlined on non-local exit.
    void push(const std::vector<StatementPtr>* finally_body) {
        tries_.push_back({finally_body});
    }

    // Leave the innermost try block after its body completes normally.
    void pop() {
        tries_.pop_back();
    }

    // Number of currently active try blocks in this scope.
    [[nodiscard]] std::size_t depth() const {
        return tries_.size();
    }

    // True when at least one try block is active.
    [[nodiscard]] bool is_active() const {
        return !tries_.empty();
    }

    // Reverse iterators for unwinding from innermost to outermost try block.
    [[nodiscard]] auto rbegin() const {
        return tries_.rbegin();
    }

    [[nodiscard]] auto rend() const {
        return tries_.rend();
    }

private:
    std::vector<TryInfo> tries_;
};

} // namespace luma
