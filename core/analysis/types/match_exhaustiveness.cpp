#include "analysis/types/match_exhaustiveness.hpp"

#include <algorithm>
#include <format>

#include "analysis/ast/declaration.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "common/string_hash.hpp"

namespace luma {

namespace {

struct BooleanCoverage {
    bool has_true{false};
    bool has_false{false};
};

[[nodiscard]] BooleanCoverage collect_boolean_coverage(const std::vector<MatchArm>& arms) {
    BooleanCoverage cov;

    // Records that a boolean pattern covers its value. An arm and an
    // alternative are both a MatchPattern, so one helper serves both.
    const auto record_boolean = [&cov](const MatchPattern& entry) {
        if (entry.kind() != MatchArm::Kind::BooleanCase) {
            return;
        }

        if (entry.boolean_value()) {
            cov.has_true = true;
        } else {
            cov.has_false = true;
        }
    };

    for (const auto& arm : arms) {
        // A guarded arm is conditional, so it cannot guarantee coverage of its
        // pattern; an unguarded fallback (or 'else') is still required.
        if (arm.has_guard()) {
            continue;
        }

        record_boolean(arm);

        for (const auto& alt : arm.alternatives) {
            record_boolean(alt);
        }

        if (arm.kind() == MatchArm::Kind::Else) {
            cov.has_true = true;
            cov.has_false = true;
        }
    }

    return cov;
}

} // namespace

// ═══════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════

MatchExhaustivenessChecker::MatchExhaustivenessChecker(TypeCheckingServices& tc) : tc_{tc} {}

// ═══════════════════════════════════════════════════════════
// Public — diagnostic entry point
// ═══════════════════════════════════════════════════════════

void MatchExhaustivenessChecker::check(const std::vector<MatchArm>& arms,
                                       const TypeInfo& subject_type, const SourceLocation& loc) {
    if (arms.empty()) {
        tc_.error("match must have at least one arm", loc,
                  "add at least one pattern arm to the match expression");

        return;
    }

    if (subject_type.kind == TypeInfo::Kind::Boolean) {
        check_boolean_coverage(arms, loc);
    } else if (subject_type.kind == TypeInfo::Kind::Choice) {
        check_choice_coverage(arms, subject_type, loc);
    } else if (subject_type.kind == TypeInfo::Kind::Result) {
        check_result_coverage(arms, loc);
    } else if (subject_type.kind == TypeInfo::Kind::Optional) {
        check_optional_coverage(arms, loc);
    } else {
        // For other types, an else arm is required if comparison, string,
        // or integer case arms are used.
        bool has_else = std::ranges::any_of(
            arms, [](const auto& arm) { return arm.kind() == MatchArm::Kind::Else; });
        bool has_comparison{false};

        for (const auto& arm : arms) {
            if (arm.kind() == MatchArm::Kind::Comparison ||
                arm.kind() == MatchArm::Kind::StringCase ||
                arm.kind() == MatchArm::Kind::IntegerCase) {
                has_comparison = true;

                // A != arm acts as a catch-all, but only when it is unguarded —
                // a guarded != might not match, so it cannot stand in for 'else'.
                if (arm.kind() == MatchArm::Kind::Comparison &&
                    arm.comparison_op() == TokenType::BangEquals && !arm.has_guard()) {
                    has_else = true;
                }
            }
        }

        if (has_comparison && !has_else) {
            tc_.error("match with comparison arms must include an 'else' arm", loc,
                      "add an 'else' arm to handle values not covered by comparisons");
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Public — exhaustiveness query (no diagnostics)
// ═══════════════════════════════════════════════════════════

bool MatchExhaustivenessChecker::is_exhaustive(const MatchStatement& match_stmt) const {
    if (match_stmt.arms.empty()) {
        return false;
    }

    bool has_else{false};
    bool has_success{false};
    bool has_failure{false};
    bool has_some{false};
    bool has_none{false};

    const auto bool_cov = collect_boolean_coverage(match_stmt.arms);

    for (const auto& arm : match_stmt.arms) {
        // Guarded arms are conditional and cannot make a match exhaustive.
        if (arm.has_guard()) {
            continue;
        }

        if (arm.kind() == MatchArm::Kind::Else) {
            has_else = true;
        }

        if (arm.kind() == MatchArm::Kind::SuccessResult) {
            has_success = true;
        }

        if (arm.kind() == MatchArm::Kind::FailureResult) {
            has_failure = true;
        }

        if (arm.kind() == MatchArm::Kind::SomeCase) {
            has_some = true;
        }

        if (arm.kind() == MatchArm::Kind::NoneCase) {
            has_none = true;
        }
    }

    const bool simple_exhaustive = has_else || (has_success && has_failure) ||
                                   (has_some && has_none) ||
                                   (bool_cov.has_true && bool_cov.has_false);

    if (simple_exhaustive) {
        return true;
    }

    return is_choice_exhaustive(match_stmt.arms);
}

// ═══════════════════════════════════════════════════════════
// Private — type-specific coverage checks
// ═══════════════════════════════════════════════════════════

void MatchExhaustivenessChecker::check_boolean_coverage(const std::vector<MatchArm>& arms,
                                                        const SourceLocation& loc) {
    const auto cov = collect_boolean_coverage(arms);

    if (!cov.has_true || !cov.has_false) {
        tc_.error("match on boolean must cover both 'true' and 'false'", loc,
                  "add the missing 'true' or 'false' arm, or add an 'else' arm");
    }
}

void MatchExhaustivenessChecker::check_choice_coverage(const std::vector<MatchArm>& arms,
                                                       const TypeInfo& subject_type,
                                                       const SourceLocation& loc) {
    // A user choice is keyed in choices() by its (bare) name, so a direct find
    // succeeds.  A namespaced stdlib choice (e.g. Json.Value) is keyed by its
    // qualified name, yet the subject type carries only the bare type name
    // ("Value"), so the direct find misses — fall back to a declaration whose
    // bare name matches.  Without this, exhaustiveness would be silently
    // unenforced for every namespaced stdlib choice (Weekday, Month, Color, …).
    const ChoiceDeclaration* choice_decl = nullptr;

    if (const auto it = tc_.choices().find(subject_type.name); it != tc_.choices().end()) {
        choice_decl = it->second;
    } else {
        for (const auto& [key, decl] : tc_.choices()) {
            if (decl->name == subject_type.name) {
                choice_decl = decl;
                break;
            }
        }
    }

    if (choice_decl == nullptr) {
        return;
    }

    const auto& variants = choice_decl->variants;
    StringSet covered;

    // Records the variant named by a choice/variant pattern. An arm and an
    // alternative are both a MatchPattern, so one helper serves both.
    const auto cover_variant = [&](const MatchPattern& entry) {
        if (entry.kind() != MatchArm::Kind::ChoiceCase &&
            entry.kind() != MatchArm::Kind::VariantCase) {
            return;
        }

        if (entry.enum_type().empty() || entry.enum_type() == subject_type.name) {
            covered.insert(entry.enum_variant());
        }
    };

    for (const auto& arm : arms) {
        // Guarded arms are conditional and do not cover their variant.
        if (arm.has_guard()) {
            continue;
        }

        cover_variant(arm);

        for (const auto& alt : arm.alternatives) {
            cover_variant(alt);
        }

        if (arm.kind() == MatchArm::Kind::Else) {
            for (const auto& variant : variants) {
                covered.insert(variant.name);
            }
        }
    }

    for (const auto& variant : variants) {
        if (!covered.contains(variant.name)) {
            tc_.error(std::format("match on choice '{}' is missing variant '{}'", subject_type.name,
                                  variant.name),
                      loc, "add a case for the missing variant, or add an 'else' arm");
        }
    }
}

void MatchExhaustivenessChecker::check_two_arm_coverage(
    const std::vector<MatchArm>& arms, MatchArm::Kind first_kind, MatchArm::Kind second_kind,
    std::string_view error_message, std::string_view hint, const SourceLocation& loc) {
    bool has_first{false};
    bool has_second{false};

    for (const auto& arm : arms) {
        // Guarded arms are conditional and do not cover their pattern.
        if (arm.has_guard()) {
            continue;
        }

        if (arm.kind() == first_kind) {
            has_first = true;
        }

        if (arm.kind() == second_kind) {
            has_second = true;
        }

        for (const auto& alt : arm.alternatives) {
            if (alt.kind() == first_kind) {
                has_first = true;
            }

            if (alt.kind() == second_kind) {
                has_second = true;
            }
        }

        if (arm.kind() == MatchArm::Kind::Else) {
            has_first = true;
            has_second = true;
        }
    }

    if (!has_first || !has_second) {
        tc_.error(error_message, loc, hint);
    }
}

void MatchExhaustivenessChecker::check_result_coverage(const std::vector<MatchArm>& arms,
                                                       const SourceLocation& loc) {
    check_two_arm_coverage(arms, MatchArm::Kind::SuccessResult, MatchArm::Kind::FailureResult,
                           "match on result must cover both 'success' and 'failure'",
                           "add 'success value { ... }' and 'failure error { ... }' arms", loc);
}

void MatchExhaustivenessChecker::check_optional_coverage(const std::vector<MatchArm>& arms,
                                                         const SourceLocation& loc) {
    check_two_arm_coverage(arms, MatchArm::Kind::SomeCase, MatchArm::Kind::NoneCase,
                           "match on optional must cover both 'some' and 'none'",
                           "add 'some value { ... }' and 'none { ... }' arms", loc);
}

// ═══════════════════════════════════════════════════════════
// Private — choice exhaustiveness (no diagnostics)
// ═══════════════════════════════════════════════════════════

bool MatchExhaustivenessChecker::is_choice_exhaustive(const std::vector<MatchArm>& arms) const {
    if (arms.empty()) {
        return false;
    }

    std::string choice_type;
    StringSet covered_variants;
    bool all_choice{true};

    for (const auto& arm : arms) {
        if (arm.kind() != MatchArm::Kind::ChoiceCase && arm.kind() != MatchArm::Kind::VariantCase) {
            all_choice = false;
            break;
        }

        if (choice_type.empty()) {
            choice_type = arm.enum_type();
        } else if (arm.enum_type() != choice_type) {
            all_choice = false;
            break;
        }

        // A guarded arm participates in choice-type detection but does not cover
        // its variant, so an unguarded fallback is still required.
        if (arm.has_guard()) {
            continue;
        }

        covered_variants.insert(arm.enum_variant());

        for (const auto& alt : arm.alternatives) {
            if (alt.kind() == MatchArm::Kind::ChoiceCase ||
                alt.kind() == MatchArm::Kind::VariantCase) {
                covered_variants.insert(alt.enum_variant());
            }
        }
    }

    if (!all_choice || choice_type.empty()) {
        return false;
    }

    // Mirror check_choice_coverage: a namespaced stdlib choice is keyed by its
    // qualified name while the arms carry only the bare type name, so fall back
    // to a declaration whose bare name matches when the direct find misses.
    const ChoiceDeclaration* choice_decl = nullptr;

    if (const auto ch_it = tc_.choices().find(choice_type); ch_it != tc_.choices().end()) {
        choice_decl = ch_it->second;
    } else {
        for (const auto& [key, decl] : tc_.choices()) {
            if (decl->name == choice_type) {
                choice_decl = decl;
                break;
            }
        }
    }

    if (choice_decl == nullptr) {
        return false;
    }

    for (const auto& variant : choice_decl->variants) {
        if (!covered_variants.contains(variant.name)) {
            return false;
        }
    }

    return true;
}

} // namespace luma
