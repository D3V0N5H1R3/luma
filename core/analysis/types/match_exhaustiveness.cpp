#include "analysis/types/match_exhaustiveness.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <variant>
#include <vector>

#include "analysis/ast/declaration.hpp"
#include "analysis/ast/expression.hpp"
#include "analysis/types/type_checking_context.hpp"
#include "common/string_hash.hpp"

namespace luma {

namespace {

// Resolve the choice declaration a match's arms refer to.
//
// A match's subject type and its arm patterns now carry the choice's canonical
// name — qualified for a namespaced choice (e.g. "Terminal.Color") — so the
// direct lookup is unambiguous even when two choices share a bare name
// (Terminal.Color vs a top-level Color).  A bare name still occurs for a
// `use`-imported choice matched via a two-part pattern (`use Traffic` then
// `case Light.Red`); the fallback then matches a declaration by its bare name,
// disambiguating any bare-name clash by variant membership.
[[nodiscard]] const ChoiceDeclaration*
find_choice_for_arms(const StringMap<const ChoiceDeclaration*>& choices, std::string_view type_name,
                     const std::vector<MatchArm>& arms) {
    if (const auto it = choices.find(type_name); it != choices.end()) {
        return it->second;
    }

    for (const auto& [key, decl] : choices) {
        if (std::string_view{decl->name} != type_name) {
            continue;
        }

        const bool declares_a_matched_variant = std::ranges::any_of(arms, [&](const MatchArm& arm) {
            if (arm.kind() != MatchArm::Kind::ChoiceCase &&
                arm.kind() != MatchArm::Kind::VariantCase) {
                return false;
            }

            return std::ranges::any_of(decl->variants, [&](const ChoiceVariant& variant) {
                return variant.name == arm.enum_variant();
            });
        });

        if (declares_a_matched_variant) {
            return decl;
        }
    }

    return nullptr;
}

// Does an arm's `enum_type` name the given choice declaration?
//
// A match arm can name its choice with a qualified ("Signals.Command") or bare
// ("Command") type name, while its subject may carry the other form — e.g. a
// namespace-internal function whose bare `Command` parameter is matched with
// three-part `Signals.Command.*` arms.  When the arm's type name is a live map
// key, trust the direct pointer comparison (this also keeps two choices sharing
// a bare name distinct — Terminal.Color vs a top-level Color).  Only when the
// name is not a key (e.g. a `use`-imported alias that was never registered) fall
// back to the declaration's bare name.
[[nodiscard]] bool enum_type_names_choice(const StringMap<const ChoiceDeclaration*>& choices,
                                          std::string_view enum_type,
                                          const ChoiceDeclaration* choice_decl) {
    if (const auto it = choices.find(enum_type); it != choices.end()) {
        return it->second == choice_decl;
    }

    return enum_type == std::string_view{choice_decl->name};
}

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

// A constant a pattern compares against: an integer (possibly negated),
// number, string, boolean, or none literal.
using PatternConstant = LiteralExpression::Value;

[[nodiscard]] std::optional<PatternConstant> literal_constant(const Expression& expr) {
    if (const auto value = get_signed_integer_value(expr)) {
        return PatternConstant{*value};
    }

    if (const auto* literal = try_cast<LiteralExpression>(expr)) {
        return literal->value;
    }

    return std::nullopt;
}

// The single constant a pattern matches exactly — a bare literal arm, or an
// `== <literal>` comparison — if any.
[[nodiscard]] std::optional<PatternConstant> equality_constant(const MatchPattern& pattern) {
    switch (pattern.kind()) {
        case MatchArm::Kind::IntegerCase:
            return PatternConstant{pattern.integer_value()};
        case MatchArm::Kind::StringCase:
            return PatternConstant{pattern.string_value()};
        case MatchArm::Kind::BooleanCase:
            return PatternConstant{pattern.boolean_value()};
        case MatchArm::Kind::NoneCase:
            return PatternConstant{std::monostate{}};
        case MatchArm::Kind::Comparison:
            if (pattern.comparison_op() == TokenType::EqualsEquals && pattern.comparison_value()) {
                return literal_constant(*pattern.comparison_value());
            }

            return std::nullopt;
        default:
            return std::nullopt;
    }
}

// Does an unguarded arm match every value of any type?  An 'else' does; so
// does an unguarded `case != c` when another unguarded arm matches exactly `c`
// — together the two cover every value.  A lone `case != c` does not: the
// value `c` itself would fall through every arm (and a match expression would
// then yield the subject unchanged).
[[nodiscard]] bool has_catch_all(const std::vector<MatchArm>& arms) {
    std::vector<PatternConstant> exact;

    for (const auto& arm : arms) {
        if (arm.kind() == MatchArm::Kind::Else) {
            return true;
        }

        if (arm.has_guard()) {
            continue;
        }

        if (auto constant = equality_constant(arm)) {
            exact.push_back(*std::move(constant));
        }

        for (const auto& alt : arm.alternatives) {
            if (auto constant = equality_constant(alt)) {
                exact.push_back(*std::move(constant));
            }
        }
    }

    return std::ranges::any_of(arms, [&](const MatchArm& arm) {
        if (arm.has_guard() || arm.kind() != MatchArm::Kind::Comparison ||
            arm.comparison_op() != TokenType::BangEquals || !arm.comparison_value()) {
            return false;
        }

        const auto excluded = literal_constant(*arm.comparison_value());

        return excluded && std::ranges::find(exact, *excluded) != exact.end();
    });
}

// Does every pattern of every arm (and alternative) satisfy `pred`, ignoring
// 'else' arms?
template <typename Pred>
[[nodiscard]] bool all_patterns(const std::vector<MatchArm>& arms, Pred pred) {
    return std::ranges::all_of(arms, [&](const MatchArm& arm) {
        return arm.kind() == MatchArm::Kind::Else ||
               (pred(arm.kind()) && std::ranges::all_of(arm.alternatives, [&](const auto& alt) {
                    return pred(alt.kind());
                }));
    });
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
    } else if (subject_type.kind == TypeInfo::Kind::Record) {
        // A record is a single-shape product type, so an unguarded record
        // pattern (or an 'else') irrefutably covers it.
        const bool covered = std::ranges::any_of(arms, [](const MatchArm& arm) {
            return !arm.has_guard() &&
                   (arm.kind() == MatchArm::Kind::RecordCase || arm.kind() == MatchArm::Kind::Else);
        });

        if (!covered) {
            tc_.error("match on a record must include an unguarded record pattern or an 'else' arm",
                      loc, "add a 'case Type { ... } { ... }' arm or an 'else' arm");
        }
    } else {
        // Other subjects have no finite set of values to enumerate, so any
        // refutable arm needs a catch-all.  A permissive subject (unknown or
        // StdlibAny) keeps the pattern-based rule: only comparison, string,
        // and integer arms demand one.
        const bool is_dynamic_subject = subject_type.kind == TypeInfo::Kind::StdlibAny ||
                                        subject_type.kind == TypeInfo::Kind::Unknown ||
                                        subject_type.kind == TypeInfo::Kind::Interface;
        bool has_comparison{false};
        bool has_refutable{false};

        for (const auto& arm : arms) {
            if (arm.kind() == MatchArm::Kind::Comparison ||
                arm.kind() == MatchArm::Kind::StringCase ||
                arm.kind() == MatchArm::Kind::IntegerCase ||
                arm.kind() == MatchArm::Kind::IntegerRangeCase) {
                has_comparison = true;
            }

            if (arm.kind() != MatchArm::Kind::Else) {
                has_refutable = true;
            }
        }

        if (has_catch_all(arms)) {
            return;
        }

        if (has_comparison) {
            tc_.error("match with comparison arms must include an 'else' arm", loc,
                      "add an 'else' arm to handle values not covered by comparisons (a "
                      "'case != v' arm covers the rest only alongside a 'case v' arm)");
        } else if (has_refutable && !is_dynamic_subject) {
            tc_.error(
                std::format("match on '{}' must include an 'else' arm", subject_type.to_string()),
                loc, "add an 'else' arm to handle values the other arms do not match");
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Public — exhaustiveness query (no diagnostics)
// ═══════════════════════════════════════════════════════════

bool MatchExhaustivenessChecker::is_exhaustive(const MatchStatement& match_stmt) const {
    const auto& arms = match_stmt.arms;

    if (arms.empty()) {
        return false;
    }

    if (has_catch_all(arms)) {
        return true;
    }

    // This query serves return-path analysis, which has no subject type.  The
    // arms' patterns are validated against the subject when the match is
    // checked, so infer the subject's kind from them instead: arms cover the
    // subject only when every pattern belongs to one kind and together they
    // enumerate it.  Mixing kinds (`case true` beside `success(v)`) proves
    // nothing about any single subject type.
    using Kind = MatchArm::Kind;

    const auto unguarded_covers = [&](Kind kind) {
        return std::ranges::any_of(arms, [&](const MatchArm& arm) {
            return !arm.has_guard() && (arm.kind() == kind ||
                                        std::ranges::any_of(arm.alternatives, [&](const auto& alt) {
                                            return alt.kind() == kind;
                                        }));
        });
    };

    if (all_patterns(arms, [](Kind k) { return k == Kind::BooleanCase; })) {
        const auto bool_cov = collect_boolean_coverage(arms);
        return bool_cov.has_true && bool_cov.has_false;
    }

    if (all_patterns(arms,
                     [](Kind k) { return k == Kind::SuccessResult || k == Kind::FailureResult; })) {
        return unguarded_covers(Kind::SuccessResult) && unguarded_covers(Kind::FailureResult);
    }

    if (all_patterns(arms, [](Kind k) { return k == Kind::SomeCase || k == Kind::NoneCase; })) {
        return unguarded_covers(Kind::SomeCase) && unguarded_covers(Kind::NoneCase);
    }

    // An unguarded record pattern irrefutably covers a record subject.
    if (all_patterns(arms, [](Kind k) { return k == Kind::RecordCase; })) {
        return unguarded_covers(Kind::RecordCase);
    }

    return is_choice_exhaustive(arms);
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
    // Resolve the matched choice.  The subject type carries the choice's
    // canonical (qualified for a namespaced choice) name, so a namespaced
    // stdlib choice (Weekday, Month, Terminal.Color, …) resolves directly and
    // unambiguously — even when another choice shares its bare name.
    const ChoiceDeclaration* choice_decl =
        find_choice_for_arms(tc_.choices(), subject_type.name, arms);

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

        // An arm covers a variant when its type names the matched choice —
        // whether it was written with the choice's qualified or bare name, and
        // regardless of which form the subject carries.
        if (entry.enum_type().empty() ||
            enum_type_names_choice(tc_.choices(), entry.enum_type(), choice_decl)) {
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

    StringSet covered_variants;

    // Resolve the choice declaration from the first choice/variant arm, then
    // check every other arm names that same declaration by identity — not by
    // raw type-name string.  An arm's type name may be qualified for a
    // namespaced choice ("Signals.Command") or bare for a `use`-imported one
    // ("Command"), so a match that mixes both forms for a single choice (e.g. a
    // three-part `Signals.Command.Turn` arm alongside a two-part `Command.Move`
    // arm) is still recognised as covering one choice.  Comparing the raw names
    // would spuriously reject that mix and, via definite-return analysis, report
    // a fully-covered match as falling through.
    const ChoiceDeclaration* choice_decl{nullptr};

    for (const auto& arm : arms) {
        if (arm.kind() != MatchArm::Kind::ChoiceCase && arm.kind() != MatchArm::Kind::VariantCase) {
            return false;
        }

        if (choice_decl == nullptr) {
            choice_decl = find_choice_for_arms(tc_.choices(), arm.enum_type(), arms);

            if (choice_decl == nullptr) {
                return false;
            }
        } else if (!enum_type_names_choice(tc_.choices(), arm.enum_type(), choice_decl)) {
            return false;
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
