#include "runtime/compiler/pattern_compiler.hpp"

#include <format>
#include <ranges>

#include "common/comparison_ops.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/hidden_var.hpp"
#include "runtime/compiler/i_compilation_backend.hpp"
#include "runtime/compiler/opcode.hpp"
#include "runtime/compiler/scratch_slot_guard.hpp"
#include "runtime/interpreter/value.hpp"

namespace luma {

// ─── Pattern test emission ────────────────────────────────────────────────────

// Named visitor struct replacing the previous if-constexpr dispatch chain.
// Each operator() overload handles one PatternData variant.  The compiler
// enforces exhaustiveness: a missing overload for a new variant is a
// compile error.
struct PatternVisitor {
    ICompilationBackend& api;
    SourceLocation loc;

    void operator()(const IntegerPattern& p) const {
        api.emit_constant(Value{p.integer_value}, loc);
        api.emit(Op::Equal, loc);
    }

    void operator()(const StringPattern& p) const {
        api.emit_constant(Value{p.string_value}, loc);
        api.emit(Op::Equal, loc);
    }

    void operator()(const BooleanPattern& p) const {
        api.emit(p.boolean_value ? Op::True : Op::False, loc);
        api.emit(Op::Equal, loc);
    }

    void operator()(const NonePattern& /*unused*/) const {
        api.emit(Op::None, loc);
        api.emit(Op::Equal, loc);
    }

    void operator()(const ComparisonPattern& p) const {
        if (p.comparison_value != nullptr) {
            api.compile_expression(*p.comparison_value);
            const auto op = token_to_comparison_op(p.comparison_op);
            api.emit(op.value_or(Op::Equal), loc);
        }
    }

    void operator()(const SuccessResultPattern& /*unused*/) const {
        api.emit(Op::IsSuccess, loc);
    }

    void operator()(const FailureResultPattern& /*unused*/) const {
        api.emit(Op::IsSuccess, loc);
        api.emit(Op::Not, loc);
    }

    void operator()(const SomePattern& /*unused*/) const {
        api.emit(Op::IsSome, loc);
    }

    void operator()(const VariantPattern& p) const {
        auto name_idx = api.add_name(p.enum_variant);
        api.emit_u16(Op::IsType, name_idx, loc);
    }

    void operator()(const ElsePattern& /*unused*/) const {
        // No-op for else/default arms.
    }
};

// Emit the bytecode for a single pattern test. The subject value must
// already be on top of the stack (via Dup). Pushes a boolean result.
void PatternCompiler::emit_pattern_test(const PatternTestArgs& args) {
    std::visit(PatternVisitor{.api = api_, .loc = args.loc}, args.data);
}

// ─── Pattern info builders ────────────────────────────────────────────────────

namespace {

/// Build the appropriate PatternData variant from a source that exposes
/// MatchArm-compatible fields (MatchArm or AlternativePattern).
template <typename PatternSource>
PatternTestArgs build_pattern_args_impl(const PatternSource& source, SourceLocation loc) {
    PatternData data = [&]() -> PatternData {
        switch (source.kind()) {
            case MatchArm::Kind::Comparison:
                return ComparisonPattern{source.comparison_op(), source.comparison_value().get()};
            case MatchArm::Kind::IntegerCase:
                return IntegerPattern{source.integer_value()};
            case MatchArm::Kind::StringCase:
                return StringPattern{source.string_value()};
            case MatchArm::Kind::BooleanCase:
                return BooleanPattern{source.boolean_value()};
            case MatchArm::Kind::NoneCase:
                return NonePattern{};
            case MatchArm::Kind::SuccessResult:
                return SuccessResultPattern{};
            case MatchArm::Kind::FailureResult:
                return FailureResultPattern{};
            case MatchArm::Kind::SomeCase:
                return SomePattern{};
            case MatchArm::Kind::ChoiceCase:
            case MatchArm::Kind::VariantCase:
                return VariantPattern{source.enum_variant()};
            default:
                return ElsePattern{};
        }
    }();

    return {.data = std::move(data), .loc = loc};
}

} // anonymous namespace

PatternTestArgs PatternCompiler::build_pattern_args(const MatchArm& arm, SourceLocation loc) {
    return build_pattern_args_impl(arm, loc);
}

PatternTestArgs PatternCompiler::build_pattern_args(const MatchArm::AlternativePattern& alt,
                                                    SourceLocation loc) {
    return build_pattern_args_impl(alt, loc);
}

std::size_t PatternCompiler::emit_arm_pattern_tests(const MatchArm& arm, SourceLocation loc,
                                                    std::size_t subject_temps_below) {
    // The duplicated subject, plus any untracked operand-stack temporaries
    // beneath it, must be reserved as scratch slots while the pattern test
    // compiles: a comparison value (`case == <expr>`) can be an arbitrary
    // expression that declares locals (an if/match/block in value position),
    // and those locals need slot indices matching their true runtime depth.
    const std::size_t scratch = subject_temps_below + 1;

    api_.emit(Op::Dup, loc);

    {
        const ScratchSlotGuard scratch_guard{api_, scratch, loc};
        emit_pattern_test(build_pattern_args(arm, loc));
    }

    std::vector<std::size_t> alt_match_jumps;

    if (!arm.alternatives.empty()) {
        alt_match_jumps.push_back(api_.emit_jump(Op::JumpIfTrue, loc));
        api_.emit(Op::Pop, loc);

        for (std::size_t ai = 0; ai < arm.alternatives.size(); ++ai) {
            const auto& alt = arm.alternatives[ai];

            api_.emit(Op::Dup, loc);

            {
                const ScratchSlotGuard scratch_guard{api_, scratch, loc};
                emit_pattern_test(build_pattern_args(alt, loc));
            }

            if (ai + 1 < arm.alternatives.size()) {
                alt_match_jumps.push_back(api_.emit_jump(Op::JumpIfTrue, loc));
                api_.emit(Op::Pop, loc);
            }
        }
    }

    auto skip_jump = api_.emit_jump(Op::JumpIfFalse, loc);

    for (auto offset : alt_match_jumps) {
        api_.patch_jump(offset);
    }

    api_.emit(Op::Pop, loc); // Pop the comparison result.

    return skip_jump;
}

// ─── Guard ────────────────────────────────────────────────────────────────────

PatternCompiler::GuardResult PatternCompiler::compile_optional_guard(const Expression* guard_expr,
                                                                     const SourceLocation& loc) {
    if (guard_expr == nullptr) {
        return {};
    }

    api_.compile_expression(*guard_expr);
    const std::size_t skip_jump = api_.emit_jump(Op::JumpIfFalse, loc);
    api_.emit(Op::Pop, loc); // Pop guard boolean.
    return {.has_guard = true, .skip_jump = skip_jump};
}

// ─── Arm bindings ─────────────────────────────────────────────────────────────

// Shared choice field binding logic — used by both match expressions and
// match statements.  Loads each non-wildcard binding from the subject at
// the given local slot by positional index.
std::size_t PatternCompiler::emit_choice_field_bindings(const MatchArm& arm,
                                                        std::uint16_t subject_slot,
                                                        SourceLocation loc) {
    std::size_t count = 0;

    for (std::size_t i = 0; i < arm.choice_bindings().size(); ++i) {
        const auto& binding = arm.choice_bindings()[i];

        if (!binding.empty() && binding != "_") {
            api_.emit_u16(Op::GetLocal, subject_slot, loc);
            api_.emit_constant(Value{static_cast<std::int64_t>(i)}, loc);
            api_.emit(Op::IndexGet, loc);
            (void)api_.declare_local(binding, false, loc);
            ++count;
        }
    }

    return count;
}

PatternCompiler::MatchArmBindingInfo
PatternCompiler::compile_match_arm_bindings(const MatchArm& arm, SourceLocation loc) {
    std::size_t binding_local_count = 0;
    std::size_t first_binding_slot = 0;

    if (arm.has_binding()) {
        if (arm.kind() == MatchArm::Kind::SuccessResult ||
            arm.kind() == MatchArm::Kind::FailureResult || arm.kind() == MatchArm::Kind::SomeCase) {
            api_.emit(Op::ResultInner, loc);
        }

        first_binding_slot = api_.current_scope().locals.size();
        (void)api_.declare_local(arm.binding_name(), false, loc);
        binding_local_count = 1;
    } else if (arm.has_choice_bindings()) {
        first_binding_slot = api_.current_scope().locals.size();
        (void)api_.declare_local(to_string(HiddenVar::MatchSubject), false, loc);
        binding_local_count = 1;

        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the local was just declared above.
        auto subject_slot = *api_.resolve_local(to_string(HiddenVar::MatchSubject));
        binding_local_count += emit_choice_field_bindings(arm, subject_slot, loc);
    }

    return {.binding_local_count = binding_local_count, .first_binding_slot = first_binding_slot};
}

void PatternCompiler::cleanup_match_arm(std::size_t binding_count, std::size_t first_slot,
                                        SourceLocation loc) {
    // Stack: [..., binding_locals..., body_locals..., body_result]
    // We need to end up with: [..., body_result]
    if (binding_count > 0) {
        // Count total locals in the current scope (bindings + body locals).
        std::size_t total_scope_locals = 0;

        for (auto& local : std::views::reverse(api_.current_scope().locals)) {
            if (local.depth < api_.current_scope().scope_depth) {
                break;
            }

            ++total_scope_locals;
        }

        api_.emit_u16(Op::SetLocal, static_cast<std::uint16_t>(first_slot), loc);

        // Pop: body_result copy (1) + all scope locals.
        for (std::size_t i = 0; i < total_scope_locals; ++i) {
            api_.emit(Op::Pop, loc);
        }

        // Now first_slot (with saved result) is on top.
    }

    // Manually remove scope locals so end_scope doesn't emit pops.
    discard_arm_scope_locals();
}

void PatternCompiler::discard_arm_scope_locals() {
    // Pop every local at or below the arm's scope depth off compile-time
    // tracking (the matching runtime Pops were already emitted), then leave the
    // scope so the arm's result value is the bare top of stack for the end-merge.
    while (!api_.current_scope().locals.empty() &&
           api_.current_scope().locals.back().depth > api_.current_scope().scope_depth - 1) {
        api_.current_scope().locals.pop_back();
    }

    --api_.current_scope().scope_depth;
}

// ─── Match expression ─────────────────────────────────────────────────────────

void PatternCompiler::compile_match_expression(const MatchExpression& expr) {
    emit_match_arms_as_value(*expr.subject, expr.arms, expr.location);
}

// Compile a match *statement* in value-producing position (e.g. as the final
// statement of an expression-valued block).  The statement and expression
// forms share identical subject/arms structure, so both delegate to the same
// value-producing emitter.  The type checker enforces exhaustiveness whenever a
// match appears in expression position, so a matching arm always runs.
void PatternCompiler::compile_match_statement_as_expression(const MatchStatement& stmt) {
    emit_match_arms_as_value(*stmt.subject, stmt.arms, stmt.location);
}

void PatternCompiler::emit_match_arms_as_value(const Expression& subject,
                                               const std::vector<MatchArm>& arms,
                                               SourceLocation loc) {
    api_.compile_expression(subject);

    std::vector<std::size_t> end_jumps;

    for (const auto& arm : arms) {
        if (arm.kind() == MatchArm::Kind::Else) {
            api_.emit(Op::Pop, loc);

            if (arm.body_expr) {
                api_.compile_expression(*arm.body_expr);
            } else {
                const ICompilationBackend::ScopeGuard scope(api_);
                api_.compile_body_as_expression(arm.body, loc);
            }

            // The else arm always matches, so it must terminate the match: jump to
            // the merge point rather than falling into the next arm. Without this,
            // a following arm's pattern test (emit_arm_pattern_tests, which Dups the
            // stack top) would run against the else BODY'S RESULT — the subject was
            // just popped and replaced — and, on a spurious match, execute the wrong
            // arm (`match x { else { 2 } case == 2 { 99 } }` yielded 99, not 2). The
            // statement form already jumps here (compile_match_statement); mirror it.
            // Placing else last leaves a harmless zero-distance forward jump.
            end_jumps.push_back(api_.emit_jump(Op::Jump, loc));
        } else if (arm.has_guard()) {
            // Guarded arms need the subject preserved across a failing guard.
            auto skip_jump = emit_arm_pattern_tests(arm, loc, /*subject_temps_below=*/1);
            emit_guarded_arm_as_value(arm, skip_jump, end_jumps, loc);
        } else {
            auto skip_jump = emit_arm_pattern_tests(arm, loc, /*subject_temps_below=*/1);

            // The subject is on top of the stack. Pop it unless we need
            // it for pattern bindings.
            const bool has_bindings = arm.has_binding() || arm.has_choice_bindings();

            if (!has_bindings) {
                api_.emit(Op::Pop, loc); // Pop the subject.
            }

            api_.begin_scope();

            auto [binding_local_count, first_binding_slot] = compile_match_arm_bindings(arm, loc);

            if (arm.body_expr) {
                api_.compile_expression(*arm.body_expr);
            } else {
                const ICompilationBackend::ScopeGuard scope(api_);
                api_.compile_body_as_expression(arm.body, loc);
            }

            cleanup_match_arm(binding_local_count, first_binding_slot, loc);

            end_jumps.push_back(api_.emit_jump(Op::Jump, loc));

            api_.patch_jump(skip_jump);
            api_.emit(Op::Pop, loc);
        }
    }

    for (auto offset : end_jumps) {
        api_.patch_jump(offset);
    }

    // The subject is consumed by the last arm or dropped here.
}

// Emit a guarded arm in value-producing position.
//
// On entry the matched subject S is bare on top of the stack at slot K (==
// locals.size()), and `skip_jump` targets the mismatch path where the stack is
// [..., S, comparison_bool].  Because a failing guard must leave S intact for
// the following arm, S is adopted as a hidden local rather than popped.
//
//   guard true : collapse [..., S, bindings..., R] -> [..., R] at slot K.
//   guard false: drop bindings/guard, leaving [..., S], then jump over the
//                mismatch Pop so both paths converge on [..., S].
void PatternCompiler::emit_guarded_arm_as_value(const MatchArm& arm, std::size_t skip_jump,
                                                std::vector<std::size_t>& end_jumps,
                                                SourceLocation loc) {
    api_.begin_scope();

    // Adopt the bare subject as a hidden local so its slot is tracked and a
    // failing guard can restore it for the next arm.
    const auto subject_slot = static_cast<std::uint16_t>(api_.current_scope().locals.size());
    (void)api_.declare_local(to_string(HiddenVar::MatchGuardSubject), false, loc);

    // Bind pattern variables by reading from the preserved subject.
    const std::size_t binding_count = emit_guard_arm_bindings(arm, subject_slot, loc);

    // Evaluate the guard; JumpIfFalse peeks the boolean (it is popped below).
    api_.compile_expression(arm.guard_ref());
    const auto guard_skip_jump = api_.emit_jump(Op::JumpIfFalse, loc);
    api_.emit(Op::Pop, loc); // Guard true: pop the guard boolean.

    // Compile the body, leaving its result on top of the stack.
    if (arm.body_expr) {
        api_.compile_expression(*arm.body_expr);
    } else {
        const ICompilationBackend::ScopeGuard scope(api_);
        api_.compile_body_as_expression(arm.body, loc);
    }

    // Collapse [..., S, bindings..., R] -> [..., R@slot K] by writing R into the
    // subject slot and popping the bindings plus the result copy.
    api_.emit_u16(Op::SetLocal, subject_slot, loc);

    for (std::size_t i = 0; i < binding_count + 1; ++i) {
        api_.emit(Op::Pop, loc);
    }

    // Remove the hidden subject and binding locals from compile-time tracking so
    // the result sits bare at slot K for the end-merge (mirrors cleanup_match_arm).
    discard_arm_scope_locals();

    end_jumps.push_back(api_.emit_jump(Op::Jump, loc));

    // Guard-false path: drop the guard boolean and bindings, restoring [..., S].
    api_.patch_jump(guard_skip_jump);
    api_.emit(Op::Pop, loc); // Pop the guard boolean.

    for (std::size_t i = 0; i < binding_count; ++i) {
        api_.emit(Op::Pop, loc);
    }

    // Skip over the mismatch Pop (which only runs when the comparison boolean is
    // still on the stack) so both fall-through paths converge on [..., S].
    const auto guard_fail_jump = api_.emit_jump(Op::Jump, loc);
    api_.patch_jump(skip_jump);
    api_.emit(Op::Pop, loc); // Mismatch path: pop the comparison result.
    api_.patch_jump(guard_fail_jump);
}

// Bind a guarded arm's variables by reading from the preserved subject local
// without consuming it (the unguarded path consumes the bare subject instead).
std::size_t PatternCompiler::emit_guard_arm_bindings(const MatchArm& arm,
                                                     std::uint16_t subject_slot,
                                                     SourceLocation loc) {
    if (arm.has_binding()) {
        api_.emit_u16(Op::GetLocal, subject_slot, loc);
        api_.emit(Op::ResultInner, loc);
        (void)api_.declare_local(arm.binding_name(), false, loc);
        return 1;
    }

    if (arm.has_choice_bindings()) {
        return emit_choice_field_bindings(arm, subject_slot, loc);
    }

    return 0;
}

// ─── Match statement ──────────────────────────────────────────────────────────

void PatternCompiler::compile_match_statement(const MatchStatement& stmt) {
    api_.compile_expression(*stmt.subject);

    // The subject occupies a stack slot. Declare it as a hidden local so
    // binding slots inside match arms are correctly numbered.
    (void)api_.declare_local(to_string(HiddenVar::MatchStmtSubject), false, stmt.location);

    std::vector<std::size_t> end_jumps;

    for (const auto& arm : stmt.arms) {
        if (arm.kind() == MatchArm::Kind::Else) {
            // Default arm — always matches.
            {
                const ICompilationBackend::ScopeGuard scope(api_);

                for (const auto& s : arm.body) {
                    api_.compile_statement(*s);
                }
            }

            end_jumps.push_back(api_.emit_jump(Op::Jump, stmt.location));
        } else {
            // The hidden subject local is already tracked, so only the Dup sits
            // untracked on the operand stack during the pattern test.
            auto skip_jump = emit_arm_pattern_tests(arm, stmt.location, /*subject_temps_below=*/0);

            api_.begin_scope();

            // Bind the match variable if needed.
            std::size_t binding_count = 0;

            if (arm.has_binding()) {
                auto subject_slot = api_.resolve_local(to_string(HiddenVar::MatchStmtSubject));
                api_.emit_u16(Op::GetLocal, *subject_slot, stmt.location);
                api_.emit(Op::ResultInner, stmt.location);
                (void)api_.declare_local(arm.binding_name(), false, stmt.location);
                ++binding_count;
            }

            // Bind choice destructuring fields using the shared helper.
            if (arm.has_choice_bindings()) {
                auto subject_slot = *api_.resolve_local(to_string(HiddenVar::MatchStmtSubject));
                binding_count += emit_choice_field_bindings(arm, subject_slot, stmt.location);
            }

            // Compile optional guard expression.
            auto [has_guard, guard_skip_jump] =
                compile_optional_guard(arm.guard.get(), stmt.location);

            for (const auto& s : arm.body) {
                api_.compile_statement(*s);
            }

            api_.end_scope();

            end_jumps.push_back(api_.emit_jump(Op::Jump, stmt.location));

            // Guard failure path: pop guard boolean and binding locals.
            if (has_guard) {
                api_.patch_jump(guard_skip_jump);
                api_.emit(Op::Pop, stmt.location); // Pop guard boolean.

                for (std::size_t i = 0; i < binding_count; ++i) {
                    api_.emit(Op::Pop, stmt.location);
                }

                // The subject is preserved as a hidden local (never consumed by
                // bindings), so after popping the guard boolean and bindings the
                // stack already holds the subject.  Skip over the mismatch Pop,
                // which only runs when the comparison boolean is still on the
                // stack, so both fall-through paths converge on [..., subject].
                auto guard_fail_jump = api_.emit_jump(Op::Jump, stmt.location);
                api_.patch_jump(skip_jump);
                api_.emit(Op::Pop, stmt.location); // Pop comparison result.
                api_.patch_jump(guard_fail_jump);
            } else {
                api_.patch_jump(skip_jump);
                api_.emit(Op::Pop, stmt.location); // Pop the comparison result.
            }
        }
    }

    // Patch all end jumps.
    for (auto offset : end_jumps) {
        api_.patch_jump(offset);
    }

    api_.emit(Op::Pop, stmt.location);      // Pop the subject.
    api_.current_scope().locals.pop_back(); // Remove the hidden subject local.
}

// ─── Tuple destructuring ──────────────────────────────────────────────────────

void PatternCompiler::compile_tuple_destructuring(const TupleDestructuringStatement& stmt) {
    api_.compile_expression(*stmt.initializer);

    // Register the tuple as a hidden local so bindings have correct slot indices.
    auto tuple_name = std::format("{}{}__", to_string(HiddenVar::TuplePrefix),
                                  api_.current_scope().locals.size());
    auto tuple_slot = api_.declare_local(tuple_name, false, stmt.location);

    // Destructure the tuple into individual locals.
    for (std::size_t i = 0; i < stmt.bindings.size(); ++i) {
        api_.emit_u16(Op::GetLocal, tuple_slot, stmt.location);
        api_.emit_constant(Value{static_cast<std::int64_t>(i)}, stmt.location);
        api_.emit(Op::IndexGet, stmt.location);
        (void)api_.declare_local(stmt.bindings[i].second, stmt.is_mutable, stmt.location);
    }

    // The hidden tuple local will be cleaned up by end_scope.
}

} // namespace luma
