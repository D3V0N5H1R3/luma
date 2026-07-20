#include "runtime/compiler/variable_resolver.hpp"

#include "runtime/compiler/compilation_context.hpp"
#include "runtime/compiler/compiler_errors.hpp"
#include "runtime/compiler/compiler_limits.hpp"
#include "runtime/compiler/i_compilation_backend.hpp"
#include "runtime/compiler/opcode.hpp"

namespace luma {

// ─────────── Scope lifecycle ───────────

void VariableResolver::begin_scope() {
    ++api_.current_scope().scope_depth;
}

void VariableResolver::end_scope() {
    --api_.current_scope().scope_depth;

    // Pop locals that belong to the scope we're leaving.
    // Captured locals were already copied at MakeClosure time (value capture),
    // so they can be popped normally.
    auto& scope = api_.current_scope();

    while (!scope.locals.empty() && scope.locals.back().depth > scope.scope_depth) {
        // No source location available — scope-exit cleanup is implicit.
        api_.emit(Op::Pop, {});
        scope.locals.pop_back();
    }
}

// ─────────── Variable declaration and resolution ───────────

std::optional<std::uint16_t> VariableResolver::check_duplicate_in_scope(std::string_view name,
                                                                        InternedString interned,
                                                                        SourceLocation loc) const {
    const auto& scope = api_.current_scope();

    for (auto it = scope.locals.rbegin(); it != scope.locals.rend(); ++it) {
        if (it->depth < scope.scope_depth) {
            break;
        }

        if (it->name == interned && !name.empty() && name != "_") {
            auto e = compiler_errors::variable_already_declared(name);
            api_.error(e.message, loc, e.hint);
            auto idx = static_cast<std::uint16_t>(
                scope.locals.size() - 1 - static_cast<std::size_t>(it - scope.locals.rbegin()));
            return idx;
        }
    }

    return std::nullopt;
}

std::uint16_t VariableResolver::declare_local(std::string_view name, bool is_mutable,
                                              SourceLocation loc) {
    const auto interned = api_.intern_name(name);
    auto& scope = api_.current_scope();

    // Check for duplicate in the current scope.
    if (auto existing = check_duplicate_in_scope(name, interned, loc)) {
        return *existing;
    }

    Local local;
    local.name = interned;
    local.depth = scope.scope_depth;
    local.is_mutable = is_mutable;
    local.is_captured = false;

    scope.locals.push_back(std::move(local));

    if (scope.locals.size() > CompilerLimits::k_max_locals) {
        auto e = compiler_errors::too_many_local_variables(CompilerLimits::k_max_locals);
        api_.error(e.message, loc, e.hint);
    }

    auto slot = static_cast<std::uint16_t>(scope.locals.size() - 1);

    // Populate debug info vectors so the VM can map slot indices to names.
    if (slot >= scope.function.debug_info.local_names.size()) {
        scope.function.debug_info.local_names.resize(slot + 1);
        scope.function.debug_info.local_mutable.resize(slot + 1, false);
    }

    scope.function.debug_info.local_names[slot] = std::string{name};
    scope.function.debug_info.local_mutable[slot] = is_mutable;

    return slot;
}

std::optional<std::uint16_t> VariableResolver::resolve_local(std::string_view name) const {
    // Intern the search key so all comparisons below are O(1) integer equality.
    const auto interned = api_.ctx().interner.intern(name);
    const auto& scope = api_.current_scope();

    for (int i = static_cast<int>(scope.locals.size()) - 1; i >= 0; --i) {
        if (scope.locals[static_cast<std::size_t>(i)].name == interned) {
            return static_cast<std::uint16_t>(i);
        }
    }

    return std::nullopt;
}

std::optional<std::uint16_t> VariableResolver::resolve_upvalue(std::string_view name) {
    if (api_.ctx().scope_stack.size() < 2) {
        return std::nullopt;
    }
    return resolve_upvalue_in(api_.ctx().scope_stack.size() - 1, api_.intern_name(name));
}

std::optional<std::uint16_t> VariableResolver::resolve_upvalue_in(std::size_t scope_index,
                                                                  InternedString name, int depth) {
    if (scope_index == 0) {
        return std::nullopt;
    }

    // Guard against pathological nesting depth to prevent stack overflow.
    if (depth >= CompilerLimits::k_max_upvalue_depth) {
        return std::nullopt;
    }

    auto& scope = api_.ctx().scope_stack[scope_index];
    auto& enclosing = api_.ctx().scope_stack[scope_index - 1];

    // Search for an existing upvalue descriptor matching `is_local` and `index`.
    auto find_existing = [&scope](bool is_local,
                                  std::uint16_t index) -> std::optional<std::uint16_t> {
        for (int j = 0; j < scope.function.upvalue_count; ++j) {
            const auto& existing = scope.function.upvalues[static_cast<std::size_t>(j)];
            if (existing.is_local == is_local && existing.index == index) {
                return static_cast<std::uint16_t>(j);
            }
        }
        return std::nullopt;
    };

    // Add a new upvalue descriptor and return its slot index.
    auto add_upvalue = [&scope](std::uint16_t index, bool is_local, bool is_mutable) {
        CompiledFunction::Upvalue uv;
        uv.index = index;
        uv.is_local = is_local;
        uv.is_mutable = is_mutable;

        scope.function.upvalues.push_back(uv);
        ++scope.function.upvalue_count;

        return static_cast<std::uint16_t>(scope.function.upvalue_count - 1);
    };

    // Check the immediate enclosing scope.
    for (int i = static_cast<int>(enclosing.locals.size()) - 1; i >= 0; --i) {
        if (enclosing.locals[static_cast<std::size_t>(i)].name == name) {
            enclosing.locals[static_cast<std::size_t>(i)].is_captured = true;

            auto slot = static_cast<std::uint16_t>(i);
            if (auto existing = find_existing(true, slot)) {
                return existing;
            }

            return add_upvalue(slot, true,
                               enclosing.locals[static_cast<std::size_t>(i)].is_mutable);
        }
    }

    // Recurse to grandparent scopes.
    const auto upvalue = resolve_upvalue_in(scope_index - 1, name, depth + 1);

    if (upvalue) {
        if (auto existing = find_existing(false, *upvalue)) {
            return existing;
        }

        return add_upvalue(*upvalue, false, enclosing.function.upvalues[*upvalue].is_mutable);
    }

    return std::nullopt;
}

VarSlot VariableResolver::resolve_variable(std::string_view name,
                                           [[maybe_unused]] const SourceLocation& loc) {
    if (const auto local = resolve_local(name)) {
        return {.location = VarLocation::Local, .slot = *local};
    }

    if (const auto upvalue = resolve_upvalue(name)) {
        return {.location = VarLocation::Upvalue, .slot = *upvalue};
    }

    auto name_idx = api_.add_name(std::string{name});
    return {.location = VarLocation::Global, .slot = name_idx};
}

} // namespace luma
