#pragma once

#include <span>
#include <vector>

#include "analysis/ast/ast_dispatcher.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/source/source_location.hpp"
#include "analysis/types/type_info.hpp"

namespace luma {

class TypeCheckingServices;

// ─────────────────────────────────────────────────────────────────────────────
// StatementTypeChecker — checks type correctness of all statement AST nodes.
//
// ─── Coupling to TypeCheckingServices ────────────────────────────────────
//
// This class holds a mutable reference to TypeCheckingServices (tc_), the
// abstract interface defined in type_checking_context.hpp.  It does NOT
// depend on the concrete TypeChecker class directly.  The services consumed
// through tc_ fall into these categories:
//
//   - Diagnostic emission:  tc_.error(), tc_.warn()
//   - Type resolution:      tc_.resolve_type(), tc_.is_assignable()
//   - Type inference:       tc_.infer_expression_type(), tc_.infer_assignment_target()
//   - Scope management:     tc_.push_scope(), tc_.pop_scope(),
//                            tc_.make_scope_guard()
//   - Symbol lookups:       tc_.lookup_variable(),
//                            tc_.lookup_variable_mut(),
//                            tc_.suggest_type_name()
//   - Per-pass context:     tc_.context() (scope, return type, loop depth)
//   - Type refinements:     tc_.push_refinement(), tc_.pop_refinements(),
//                            tc_.refinement_mark(),
//                            tc_.try_extract_is_refinement()
//   - Match exhaustiveness: tc_.check_match_exhaustiveness(),
//                            tc_.is_match_exhaustive()
//
// The TypeCheckingServices interface decouples StatementTypeChecker from the
// concrete TypeChecker implementation.  No friend access is needed — the
// existing interface supports stub-based testing and keeps the dependency
// surface explicit and auditable.
// ─────────────────────────────────────────────────────────────────────────────

class StatementTypeChecker : public StatementDispatcher<StatementTypeChecker> {
public:
    explicit StatementTypeChecker(TypeCheckingServices& tc);

    void check_statement(const Statement& stmt);
    void check_statement_list(const std::vector<StatementPtr>& stmts);
    [[nodiscard]] bool definitely_returns(std::span<const StatementPtr> stmts) const;

    // ─── Statement dispatch handlers (called by StatementDispatcher) ───

    void visit_variable_declaration(const VariableDeclStatement& stmt);
    void visit_assignment(const AssignmentStatement& stmt);
    void visit_compound_assignment(const CompoundAssignmentStatement& stmt);
    void visit_increment(const IncrementStatement& stmt);
    void visit_decrement(const DecrementStatement& stmt);
    void visit_return(const ReturnStatement& stmt);
    void visit_for(const ForStatement& stmt);
    void visit_if_statement(const IfStatement& stmt);
    void visit_match_statement(const MatchStatement& stmt);
    void visit_tuple_destructuring(const TupleDestructuringStatement& stmt);
    void visit_record_destructuring(const RecordDestructuringStatement& stmt);
    void visit_block(const BlockStatement& stmt);
    void visit_while(const WhileStatement& stmt);
    void visit_try(const TryStatement& stmt);
    void visit_expression_statement(const ExpressionStatement& stmt);
    void visit_break(const BreakStatement& stmt);
    void visit_continue(const ContinueStatement& stmt);

private:
    // Report an error when an outer-scope unique variable is consumed inside a
    // loop body, where the next iteration would reuse the already-consumed
    // value.  Shared by visit_for and visit_while.
    void check_unique_consumption_in_loop(const TypeScope::OwnershipSnapshot& before,
                                          const SourceLocation& loc);

    // True when a match statement is exhaustive and every arm definitely
    // returns, so the match itself cannot fall through.  Used by
    // definitely_returns().
    [[nodiscard]] bool match_definitely_returns(const MatchStatement& match_stmt) const;

    // Returns true if a `break` targeting the immediately-enclosing loop can
    // appear at a *reachable* point in `stmts`.  Descends into if/block/match/try
    // bodies but NOT into nested while/for loops, whose `break`s bind to the
    // inner loop, and stops at the first statement that definitely returns so a
    // `break` in unreachable dead code (e.g. after a guaranteed return) is not
    // counted.  Used to keep definitely_returns() sound for `while true` bodies
    // that can exit via break instead of returning.
    [[nodiscard]] bool body_can_break(const std::vector<StatementPtr>& stmts) const;

    // visit_compound_assignment phases.
    void check_compound_target_mutability(const CompoundAssignmentStatement& stmt);
    void check_compound_literal_folding(const CompoundAssignmentStatement& stmt);
    void check_compound_operand_types(const CompoundAssignmentStatement& stmt,
                                      const TypeInfo& target_type, const TypeInfo& value_type);

    // How an assignment target is being written — selects the diagnostic
    // wording ("assign to" vs "compound-assign to") used by
    // check_target_mutability, and whether the plain-assignment-only checks
    // (consumed-unique and invalid-target) apply.
    enum class AssignmentKind {
        Plain,
        Compound
    };

    // Validate that `target` (a Variable, FieldAccess, or IndexAccess) may be
    // written: it must not be immutable or a borrow, and — for plain assignment
    // to a bare variable — not an already-consumed unique.  Emits the matching
    // diagnostics at `loc`, worded per `kind`, and marks the root variable
    // written.
    void check_target_mutability(const Expression& target, const SourceLocation& loc,
                                 AssignmentKind kind);

    // visit_match_statement per-arm phases.
    void check_match_arm_comparison(const MatchArm& arm, const TypeInfo& subject_type);

    TypeCheckingServices& tc_;
};

} // namespace luma
