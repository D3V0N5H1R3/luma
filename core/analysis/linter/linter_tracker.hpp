#ifndef LUMA_LINTER_LINTER_TRACKER_HPP
#define LUMA_LINTER_LINTER_TRACKER_HPP

#include <string>
#include <string_view>
#include <vector>

#include "analysis/source/source_location.hpp"
#include "common/scope_stack.hpp"
#include "common/string_hash.hpp"

namespace luma {

// Tracks variable and include usage during linting.
//
// Owns the scope stack and include list but does NOT emit diagnostics.
// The Linter queries the tracker for unused variables/includes and
// handles the actual warning emission itself.
class LinterTracker {
public:
    // ─── Data types ─────────────────────────────────────────────────
    struct VariableInfo {
        SourceLocation location;
        bool is_parameter{false};
        bool is_mutable{false};
        bool used{false};
        bool mutated{false};
    };

    // Combined scope data — each entry holds variable usage tracking info.
    // StringMap enables heterogeneous lookup: string_view keys avoid heap
    // allocation when querying with find() or contains().  ScopeData is stored
    // in a flat ScopeStack<ScopeData> (see scopes_ below); see
    // common/scope_stack.hpp §Component Usage for why that model fits.
    struct ScopeData {
        StringMap<VariableInfo> variables;
    };

    struct IncludeInfo {
        std::string path;
        SourceLocation location;
        bool used{false};
    };

    // ─── Lifecycle ──────────────────────────────────────────────────
    void clear();

    // ─── Scope management ───────────────────────────────────────────
    // push_scope / pop_scope maintain a flat ScopeStack<ScopeData>.  The Linter
    // brackets them with the shared ScopeManager<Linter> guard (see
    // analysis/common/scope_manager.hpp); this flat storage stays distinct from
    // NameResolver's shared_ptr parent chain, which is why only the guard
    // lifecycle is unified — see common/scope_stack.hpp §Component Usage.
    void push_scope();
    // Pops the current scope WITHOUT reporting unused variables.
    // The caller (Linter::pop_scope) must call report helpers first.
    void pop_scope();
    [[nodiscard]] bool empty() const;

    // Returns true if `name` is defined in any outer scope (skips innermost).
    [[nodiscard]] bool is_shadowed(std::string_view name) const;

    // Access current scope's variables for unused-variable reporting.
    [[nodiscard]] const StringMap<VariableInfo>& current_variables() const;

    // ─── Variable tracking ──────────────────────────────────────────
    void track_variable(std::string_view name, SourceLocation loc, bool is_parameter,
                        bool is_mutable = false);
    void mark_used(std::string_view name);
    void mark_mutated(std::string_view name);

    // ─── Include tracking ───────────────────────────────────────────
    void track_include(std::string_view path, SourceLocation loc);
    void mark_include_used(std::string_view path);

    // Access tracked includes for unused-include reporting.
    [[nodiscard]] const std::vector<IncludeInfo>& includes() const;

private:
    // Walk the scope stack (innermost first) looking for a variable by name,
    // then apply `func` to its VariableInfo.  Only called from mark_used /
    // mark_mutated so only a non-const overload is required.
    template <typename Func> void apply_to_variable(std::string_view name, Func func);

    ScopeStack<ScopeData> scopes_;
    std::vector<IncludeInfo> include_tracker_;
};

} // namespace luma

#endif // LUMA_LINTER_LINTER_TRACKER_HPP
