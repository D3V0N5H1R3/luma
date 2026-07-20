#pragma once

#include <string_view>
#include <vector>

#include "analysis/ast/match_pattern.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/source/source_location.hpp"
#include "analysis/types/type_info.hpp"

namespace luma {

class TypeCheckingServices;

// ─────────────────────────────────────────────────────────────────────────────
// Match Exhaustiveness Checker
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Verify that match expressions cover all possible cases for
// the matched type (boolean, choice, result, optional, comparisons).
//
// Extracted from StatementTypeChecker to keep match-specific exhaustiveness
// logic in a self-contained, single-responsibility class.
//
// ─── Coupling to TypeCheckingServices ────────────────────────────────────
//
// This class holds a mutable reference to TypeCheckingServices (tc_), the
// abstract interface defined in type_checking_context.hpp.  It does NOT
// depend on the concrete TypeChecker class directly.  The services consumed
// through tc_ are:
//
//   - Diagnostic emission:  tc_.error()
//   - Symbol lookups:       tc_.choices()
//
// The TypeCheckingServices interface decouples MatchExhaustivenessChecker from
// the concrete TypeChecker implementation.
// ─────────────────────────────────────────────────────────────────────────────

class MatchExhaustivenessChecker {
public:
    explicit MatchExhaustivenessChecker(TypeCheckingServices& tc);

    // Entry point: check that the arms of a match expression/statement
    // cover all values of subject_type, reporting errors if not.
    void check(const std::vector<MatchArm>& arms, const TypeInfo& subject_type,
               const SourceLocation& loc);

    // Returns true if the match statement's arms form an exhaustive
    // covering of the subject type (without reporting diagnostics).
    [[nodiscard]] bool is_exhaustive(const MatchStatement& match_stmt) const;

private:
    void check_boolean_coverage(const std::vector<MatchArm>& arms, const SourceLocation& loc);
    void check_choice_coverage(const std::vector<MatchArm>& arms, const TypeInfo& subject_type,
                               const SourceLocation& loc);
    void check_two_arm_coverage(const std::vector<MatchArm>& arms, MatchArm::Kind first_kind,
                                MatchArm::Kind second_kind, std::string_view error_message,
                                std::string_view hint, const SourceLocation& loc);
    void check_result_coverage(const std::vector<MatchArm>& arms, const SourceLocation& loc);
    void check_optional_coverage(const std::vector<MatchArm>& arms, const SourceLocation& loc);

    // Returns true if all variants of a choice type are covered.
    [[nodiscard]] bool is_choice_exhaustive(const std::vector<MatchArm>& arms) const;

    TypeCheckingServices& tc_;
};

} // namespace luma
