// ─────────────────────────────────────────────────────────────────────────────
// IVariableManager — Narrow interface for local/name resolution
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Declare and resolve local variables to stack slots, resolve
//   variable references (local/upvalue/global), and intern/register names.
//
// Part of the ICompilationBackend interface-segregation (ISP) split.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_I_VARIABLE_MANAGER_HPP
#define LUMA_COMPILER_I_VARIABLE_MANAGER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "analysis/source/source_location.hpp"
#include "runtime/compiler/string_interner.hpp"

namespace luma {

struct VarSlot;

// Variable declaration, resolution, and name-table surface.
class IVariableManager {
public:
    virtual ~IVariableManager() = default;

    [[nodiscard]] virtual std::uint16_t declare_local(std::string_view name, bool is_mutable,
                                                      SourceLocation loc = {}) = 0;
    [[nodiscard]] virtual std::optional<std::uint16_t>
    resolve_local(std::string_view name) const = 0;
    [[nodiscard]] virtual VarSlot resolve_variable(std::string_view name,
                                                   const SourceLocation& loc) = 0;
    [[nodiscard]] virtual InternedString intern_name(std::string_view name) const = 0;

    // Name table — add a name to the chunk's name table, returning its index.
    [[nodiscard]] virtual std::uint16_t add_name(std::string_view name) = 0;

    // Reserve/release placeholder local slots that account for operand-stack
    // temporaries which are live while a sub-expression is compiled. They keep
    // local slot indices aligned with true runtime stack positions so that a
    // value-producing block (match/if used as an expression) appearing as an
    // operand — a call argument, collection element, binary operand, etc. —
    // computes correct slots. Reserved slots emit no runtime bytecode; release
    // simply drops the placeholders once the operand has been compiled.
    virtual void reserve_scratch_slots(std::size_t count, SourceLocation loc = {}) = 0;
    virtual void release_scratch_slots(std::size_t count) = 0;

protected:
    IVariableManager() = default;
    IVariableManager(const IVariableManager&) = default;
    IVariableManager& operator=(const IVariableManager&) = default;
};

} // namespace luma

#endif // LUMA_COMPILER_I_VARIABLE_MANAGER_HPP
