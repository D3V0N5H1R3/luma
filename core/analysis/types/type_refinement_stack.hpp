#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "analysis/types/type_info.hpp"

namespace luma {

// ─────────────────────────────────────────────────────────────────────────────
// TypeRefinementStack — flow-sensitive type-narrowing bookkeeping
// ─────────────────────────────────────────────────────────────────────────────
// Single responsibility: maintain the ordered stack of active is<T> type
// narrowings and answer "what is the current narrowed type for variable v?".
//
// Extracted from ExpressionTypeChecker so the narrowing bookkeeping is
// independent of expression dispatch and can be unit-tested in isolation.
//
// Usage: callers record mark() before entering a narrowed region (e.g. an
// if-then branch), push() one refinement per narrowed variable, and pop_to()
// the recorded mark on exit, restoring the stack to its previous depth.
// find() returns the most recent narrowing for a variable (innermost wins).
// ─────────────────────────────────────────────────────────────────────────────
class TypeRefinementStack {
public:
    // Record a narrowing: variable `var` has narrowed type `narrowed`.
    void push(const std::string& var, TypeInfo narrowed) {
        refinements_.push_back(
            TypeRefinement{.variable_name = var, .narrowed_type = std::move(narrowed)});
    }

    // Current stack depth — use as a restore point for pop_to().
    [[nodiscard]] std::size_t mark() const noexcept {
        return refinements_.size();
    }

    // Restore the stack to a previous depth, discarding later refinements.
    void pop_to(std::size_t mark) {
        if (mark < refinements_.size()) {
            refinements_.resize(mark);
        }
    }

    // Most-recent narrowed type for `var`, or nullptr if not refined.
    [[nodiscard]] const TypeInfo* find(const std::string& var) const {
        // Search from back to front so that the most recent refinement wins.
        const auto it =
            std::ranges::find_if(refinements_.rbegin(), refinements_.rend(),
                                 [&var](const auto& r) { return r.variable_name == var; });
        return it != refinements_.rend() ? &it->narrowed_type : nullptr;
    }

private:
    std::vector<TypeRefinement> refinements_;
};

} // namespace luma
