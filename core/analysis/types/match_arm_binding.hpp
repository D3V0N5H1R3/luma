#ifndef LUMA_TYPES_MATCH_ARM_BINDING_HPP
#define LUMA_TYPES_MATCH_ARM_BINDING_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Match-arm binding helpers — shared by the expression and statement checkers.
// ─────────────────────────────────────────────────────────────────────────────
// ExpressionTypeChecker (match expressions) and StatementTypeChecker (match
// statements) both bind the same per-arm names before checking an arm body and
// merge per-arm ownership afterwards.  These free functions hold that logic in
// one place so the two checkers stay in lockstep, following the
// type_check_helpers precedent of shared, TypeCheckingServices-based utilities.
//
// Each function takes TypeCheckingServices& for scope, symbol, generic, type
// resolution, and diagnostic access, and emits diagnostics directly.
// ─────────────────────────────────────────────────────────────────────────────

#include <vector>

#include "analysis/source/source_location.hpp"
#include "analysis/types/type_info.hpp"

namespace luma {

class TypeCheckingServices;
struct MatchArm;

} // namespace luma

namespace luma::match_arm_binding {

// Bind the success/failure/some capture name introduced by a result/optional
// match arm (e.g. `success value`, `failure err`, `some x`) into the current
// scope with the element type extracted from `subject_type`.  No-op for arms
// without a capture binding.
void bind_arm_names(TypeCheckingServices& tc, const MatchArm& arm, const TypeInfo& subject_type);

// Bind the destructured fields of a choice-variant match arm
// (e.g. `case Cons(head, tail)`) into the current scope.  For generic choices,
// temporarily binds the choice's type parameters — to the subject's concrete
// type arguments when present, otherwise as unknown placeholders — so recursive
// field types resolve, restoring prior bindings on return.
//
// When `report_unknown_variant` is true, emits a diagnostic if the arm names a
// variant the choice does not declare (the match-statement behaviour); the
// match-expression path passes false and stays silent.
void bind_choice_fields(TypeCheckingServices& tc, const MatchArm& arm, const TypeInfo& subject_type,
                        const SourceLocation& location, bool report_unknown_variant);

// Bind the fields of a record destructuring — shared by record-destructuring
// bindings (`Point { x, y } = p`) and record match patterns
// (`case Point { x, y }`).  Each listed field is bound in the current scope to
// its declared type (with the record's generic type params resolved from
// `subject_type`'s concrete arguments when present).  A subset of fields may be
// listed.  Emits diagnostics for an unknown record type, an unknown field, or a
// duplicated field name; unresolved bindings fall back to a permissive type so
// later checks do not cascade.
void bind_record_fields(TypeCheckingServices& tc, std::string_view record_type,
                        const std::vector<std::string>& fields, const TypeInfo& subject_type,
                        const SourceLocation& location, bool is_mutable);

// Merge per-branch ownership snapshots back into the current scope — shared by
// if statements, if expressions, match statements, and match expressions.  A
// unique variable consumed on any branch is consumed afterwards (conservative —
// prevents use-after-move on the path that consumed it).  An `if` without an
// `else` passes `before` itself as the snapshot of its implicit empty branch.
// Callers pass only the branches that fall through: one that ends in return,
// break, or continue never reaches the code after the if/match.  With no
// snapshots at all (no branch falls through), the pre-branch state is kept.
void merge_branch_ownership(TypeCheckingServices& tc, const TypeScope::OwnershipSnapshot& before,
                            const std::vector<TypeScope::OwnershipSnapshot>& branch_snapshots);

// Validate a match arm's case values (or ranges), including `case A | B`
// alternatives, against the subject type — shared by match statements and
// match expressions.  Infers each comparison value's type and reports an
// integer/string/range case on a subject of another type, or a comparison
// value incompatible with the subject.  Diagnostics without a more precise
// position are reported at the arm, or at `match_location` when the arm has
// no location.
void check_arm_comparison(TypeCheckingServices& tc, const MatchArm& arm,
                          const TypeInfo& subject_type, const SourceLocation& match_location);

} // namespace luma::match_arm_binding

#endif // LUMA_TYPES_MATCH_ARM_BINDING_HPP
