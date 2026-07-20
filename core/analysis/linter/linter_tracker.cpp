#include "analysis/linter/linter_tracker.hpp"

#include <algorithm>
#include <cassert>
#include <ranges>

namespace luma {

// ─────────── Lifecycle ───────────

void LinterTracker::clear() {
    scopes_.clear();
    include_tracker_.clear();
}

// ─────────── Scope management ───────────

void LinterTracker::push_scope() {
    // No maximum-depth guard here — unlike pop_scope there is no natural upper
    // bound enforced at this layer; the parser/type-checker bound nesting depth
    // before we get this far.  The asymmetry with pop_scope's assert is
    // intentional: underflow (popping an empty stack) is a definite bug,
    // whereas overflow would require pathologically deep nesting that earlier
    // pipeline stages already reject.
    scopes_.push();
}

void LinterTracker::pop_scope() {
    assert(!scopes_.empty() && "LinterTracker::pop_scope called with empty scope stack");
    scopes_.pop();
}

bool LinterTracker::empty() const {
    return scopes_.empty();
}

bool LinterTracker::is_shadowed(std::string_view name) const {
    // Need at least 2 scopes: one outer to check, one inner to skip.
    if (scopes_.depth() <= 1) {
        return false;
    }

    return std::ranges::any_of(scopes_.rbegin() + 1, scopes_.rend(), [name](const auto& scope) {
        return scope.variables.contains(name);
    });
}

const StringMap<LinterTracker::VariableInfo>& LinterTracker::current_variables() const {
    return scopes_.current().variables;
}

// ─────────── Variable tracking ───────────

void LinterTracker::track_variable(std::string_view name, SourceLocation loc, bool is_parameter,
                                   bool is_mutable) {
    if (name.empty() || name.starts_with("_") || scopes_.empty()) {
        return;
    }

    scopes_.current().variables[std::string(name)] = VariableInfo{.location = loc,
                                                                  .is_parameter = is_parameter,
                                                                  .is_mutable = is_mutable,
                                                                  .used = false,
                                                                  .mutated = false};
}

void LinterTracker::mark_used(std::string_view name) {
    apply_to_variable(name, [](VariableInfo& info) { info.used = true; });
}

void LinterTracker::mark_mutated(std::string_view name) {
    apply_to_variable(name, [](VariableInfo& info) { info.mutated = true; });
}

// ─────────── Include tracking ───────────

void LinterTracker::track_include(std::string_view path, SourceLocation loc) {
    include_tracker_.push_back({.path = std::string(path), .location = loc, .used = false});
}

void LinterTracker::mark_include_used(std::string_view path) {
    // Mark every tracker entry for this path — a path may be included more than
    // once, and each `include` statement has its own entry.  Marking only one
    // (e.g. via a path → index map that the latest duplicate overwrote) would
    // leave the others spuriously reported as unused.
    for (auto& info : include_tracker_) {
        if (info.path == path) {
            info.used = true;
        }
    }
}

const std::vector<LinterTracker::IncludeInfo>& LinterTracker::includes() const {
    return include_tracker_;
}

// ─────────── Private helpers ───────────

template <typename Func> void LinterTracker::apply_to_variable(std::string_view name, Func func) {
    // Linear scan from innermost scope outward.  In practice the stack is
    // shallow (a handful of nested blocks per function), so O(depth) is
    // negligible and a flat index would add complexity without measurable gain.
    for (auto& scope : std::views::reverse(scopes_)) {
        if (auto var_it = scope.variables.find(name); var_it != scope.variables.end()) {
            func(var_it->second);
            return;
        }
    }
}

} // namespace luma
