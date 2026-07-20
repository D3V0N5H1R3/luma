// ─────────────────────────────────────────────────────────────────────────────
// VariableResolver — Compile-time scope and variable management
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: All compile-time scope lifecycle and variable resolution,
//   extracted from the Compiler.
//
//   Scope lifecycle:
//     - begin_scope() / end_scope() — track scope depth, pop locals on exit.
//
//   Variable management:
//     - declare_local()    — register a new local in the current scope.
//     - resolve_local()    — look up a local by name in the current scope.
//     - resolve_upvalue()  — walk enclosing scopes to capture as an upvalue.
//     - resolve_variable() — unified 3-level lookup: local → upvalue → global.
//
// Design: Holds a non-owning reference to an ICompilationBackend interface, which
//   provides controlled access to Compiler's scope stack, interner, and emit
//   helpers.  This replaces the former friend relationship with Compiler.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_VARIABLE_RESOLVER_HPP
#define LUMA_COMPILER_VARIABLE_RESOLVER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "analysis/source/source_location.hpp"
#include "runtime/compiler/string_interner.hpp"

namespace luma {

class ICompilationBackend;

// Resolved variable location: local, upvalue, or global.
enum class VarLocation {
    Local,
    Upvalue,
    Global
};

// Slot index alongside its resolution location.
struct VarSlot {
    VarLocation location;
    std::uint16_t slot;
};

// Handles compile-time scope lifecycle and variable resolution
// (local → upvalue → global).  Lifetime is bounded by the Compiler that
// owns it by value.
class VariableResolver {
public:
    explicit VariableResolver(ICompilationBackend& api) noexcept : api_(api) {}

    // ─── Scope lifecycle ───

    // Increment the scope depth of the current compiler scope.
    void begin_scope();

    // Decrement the scope depth and pop locals that belong to the
    // scope being exited (emits Op::Pop for each removed local).
    void end_scope();

    // ─── Variable declaration and resolution ───

    // Declare a new local in the current scope. Returns its slot index.
    [[nodiscard]] std::uint16_t declare_local(std::string_view name, bool is_mutable,
                                              SourceLocation loc = {});

    // Check for a duplicate variable in the current scope. Returns the
    // existing slot index if a duplicate is found, or std::nullopt otherwise.
    [[nodiscard]] std::optional<std::uint16_t>
    check_duplicate_in_scope(std::string_view name, InternedString interned,
                             SourceLocation loc = {}) const;

    // Search the current scope's locals by name. Returns the slot if found.
    [[nodiscard]] std::optional<std::uint16_t> resolve_local(std::string_view name) const;

    // Walk enclosing scopes to find and capture a variable as an upvalue.
    [[nodiscard]] std::optional<std::uint16_t> resolve_upvalue(std::string_view name);

    // Unified 3-level resolution: local → upvalue → global.
    [[nodiscard]] VarSlot resolve_variable(std::string_view name, const SourceLocation& loc);

private:
    // Walks the scope stack from `scope_index` upward to find and capture
    // the named variable as an upvalue.
    [[nodiscard]] std::optional<std::uint16_t>
    resolve_upvalue_in(std::size_t scope_index, InternedString name, int depth = 0);

    ICompilationBackend& api_;
};

} // namespace luma

#endif // LUMA_COMPILER_VARIABLE_RESOLVER_HPP
