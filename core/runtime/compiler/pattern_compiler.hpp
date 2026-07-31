// ─────────────────────────────────────────────────────────────────────────────
// PatternCompiler
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Compile all pattern-matching constructs — match expressions,
//   match statements, tuple destructuring, and the individual pattern-test
//   and arm-binding helpers.  Extracted from the Compiler god class.
//
// Design: Holds a non-owning reference to an ICompilationBackend interface, which
//   provides controlled access to Compiler's emit, scope, and variable
//   management methods.
//
// ICompilationBackend methods used:
//   - emit()                   — single-byte opcodes (Dup, Pop, Equal, etc.)
//   - emit_u16()               — GetLocal / SetLocal with slot indices
//   - emit_constant()          — pattern values and destructuring indices
//   - emit_jump()              — arm-skip and end-of-match jumps
//   - patch_jump()             — back-patch arm-skip and end jumps
//   - compile_expression()     — subject, guard, and arm body expressions
//   - compile_body_as_expression() — multi-statement arm bodies
//   - compile_statement()      — arm body statements (match statement)
//   - declare_local()          — binding variables in arm scopes
//   - resolve_local()          — find hidden subject slot for choice bindings
//   - begin_scope() / end_scope() — arm binding scopes
//   - current_scope()          — direct locals inspection for arm cleanup
//   - add_name()               — intern variant-type names for IsType opcode
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_COMPILER_PATTERN_COMPILER_HPP
#define LUMA_COMPILER_PATTERN_COMPILER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

#include "analysis/ast/expression.hpp"
#include "analysis/ast/statement.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/compiler/compiler_helper.hpp"

namespace luma {

// Type-safe pattern data for each match arm kind.
struct ComparisonPattern {
    TokenType comparison_op{TokenType::EqualsEquals};
    const Expression* comparison_value{nullptr};
};

struct VariantPattern {
    std::string enum_variant;
};

struct RecordPattern {
    std::string record_type;
};

struct StringPattern {
    std::string string_value;
};

struct BooleanPattern {
    bool boolean_value{false};
};

struct IntegerPattern {
    std::int64_t integer_value{0};
};

struct IntegerRangePattern {
    std::int64_t lo{0};
    std::int64_t hi{0};
    bool inclusive{false};
};

struct NonePattern {};

struct SuccessResultPattern {};

struct FailureResultPattern {};

struct SomePattern {};

struct ElsePattern {};

using PatternData =
    std::variant<ComparisonPattern, VariantPattern, StringPattern, BooleanPattern, IntegerPattern,
                 IntegerRangePattern, NonePattern, SuccessResultPattern, FailureResultPattern,
                 SomePattern, ElsePattern, RecordPattern>;

// Parameters for a single pattern test emission (RT-21).
// SourceLocation is shared across all pattern kinds.
struct PatternTestArgs {
    PatternData data;
    SourceLocation loc;
};

// Handles all pattern-matching compilation: match expressions, match
// statements, tuple destructuring, and the supporting test/binding helpers.
// Lifetime is bounded by the Compiler that owns it by value.
class PatternCompiler : public CompilerHelper {
public:
    using CompilerHelper::CompilerHelper;

    // Result of compiling an optional guard expression on a match arm.
    struct GuardResult {
        bool has_guard{false};
        std::size_t skip_jump{0};
    };

    // Slot range occupied by bindings introduced in one match arm.
    struct MatchArmBindingInfo {
        std::size_t binding_local_count{0};
        std::size_t first_binding_slot{0};
    };

    void compile_match_expression(const MatchExpression& expr);
    void compile_match_statement(const MatchStatement& stmt);
    void compile_match_statement_as_expression(const MatchStatement& stmt);
    void compile_tuple_destructuring(const TupleDestructuringStatement& stmt);
    void compile_record_destructuring(const RecordDestructuringStatement& stmt);

    // Emit the pattern test for a match arm (primary + alternatives).
    // `subject_temps_below` is the number of untracked operand-stack temporaries
    // sitting beneath the duplicated subject (1 when the subject is a bare value,
    // 0 when it has already been adopted as a hidden local).  These, together
    // with the Dup, are reserved as scratch slots so a comparison value that
    // declares locals receives slot indices matching its true runtime depth.
    // Returns the skip-jump offset to be patched on arm mismatch.
    [[nodiscard]] std::size_t emit_arm_pattern_tests(const MatchArm& arm, SourceLocation loc,
                                                     std::size_t subject_temps_below);

    // Compile an optional guard expression attached to a match arm.
    [[nodiscard]] GuardResult compile_optional_guard(const Expression* guard_expr,
                                                     const SourceLocation& loc);

private:
    // Build PatternTestArgs from a match arm.
    [[nodiscard]] static PatternTestArgs build_pattern_args(const MatchArm& arm,
                                                            SourceLocation loc);
    // Build PatternTestArgs from an alternative pattern.
    [[nodiscard]] static PatternTestArgs build_pattern_args(const MatchArm::AlternativePattern& alt,
                                                            SourceLocation loc);

    // Emit the bytecode for a single pattern test.  The subject value must
    // already be on top of the stack (via Dup). Pushes a boolean result.
    void emit_pattern_test(const PatternTestArgs& args);

    // Bind variables introduced by a match arm's binding_name or
    // choice_bindings.  Returns info needed by cleanup_match_arm.
    [[nodiscard]] MatchArmBindingInfo compile_match_arm_bindings(const MatchArm& arm,
                                                                 SourceLocation loc);

    // Pop all locals introduced by a match arm, leaving the body result on TOS.
    void cleanup_match_arm(std::size_t binding_count, std::size_t first_slot, SourceLocation loc);

    // Drop every local belonging to the current match-arm scope from
    // compile-time tracking (without emitting Pop opcodes) and leave that scope,
    // so the arm's result value sits bare on the stack for the end-merge. Shared
    // by the unguarded (cleanup_match_arm) and guarded (emit_guarded_arm_as_value)
    // teardown paths, which tear their arm scope down identically.
    void discard_arm_scope_locals();

    // Emit choice field bindings: load each non-wildcard binding from the
    // subject at the given local slot by positional index.  Returns the
    // number of bindings created.
    [[nodiscard]] std::size_t
    emit_choice_field_bindings(const MatchArm& arm, std::uint16_t subject_slot, SourceLocation loc);

    // Emit record field bindings: load each field from the subject at the given
    // local slot by name (GetField).  Returns the number of bindings created.
    [[nodiscard]] std::size_t
    emit_record_field_bindings(const MatchArm& arm, std::uint16_t subject_slot, SourceLocation loc);

    // Shared value-producing emitter for both match expressions and match
    // statements in expression position.  Compiles the subject, then each arm
    // so the selected arm's value is left on the stack.
    void emit_match_arms_as_value(const Expression& subject, const std::vector<MatchArm>& arms,
                                  SourceLocation loc);

    // Emit a single *guarded* arm in value-producing position.  Unlike the
    // unguarded path, the subject must survive a failing guard so the next arm
    // can test it, so it is adopted as a hidden local and restored on the
    // guard-false path.  `skip_jump` is the mismatch jump from
    // emit_arm_pattern_tests; `end_jumps` collects the arm's success jump.
    void emit_guarded_arm_as_value(const MatchArm& arm, std::size_t skip_jump,
                                   std::vector<std::size_t>& end_jumps, SourceLocation loc);

    // Bind a guarded arm's pattern variables by reading (without consuming)
    // from the preserved subject at `subject_slot`.  Returns the binding count.
    [[nodiscard]] std::size_t
    emit_guard_arm_bindings(const MatchArm& arm, std::uint16_t subject_slot, SourceLocation loc);
};

} // namespace luma

#endif // LUMA_COMPILER_PATTERN_COMPILER_HPP
