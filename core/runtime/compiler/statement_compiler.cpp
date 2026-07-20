#include <ranges>

#include "analysis/source/source_location.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/compiler_errors.hpp"
#include "runtime/compiler/hidden_var.hpp"
#include "runtime/compiler/opcode.hpp"

namespace luma {

// ─────────── Statement compilation ───────────
//
// compile_statement() dispatches each statement kind to a compile_X()
// handler via the StatementDispatcher<Compiler> CRTP base
// (ast_dispatcher.hpp).  The inline visit_X() forwarders in compiler.hpp
// bridge the CRTP visit_X() names to the compile_X() method names used
// throughout this file.
//
// Exhaustiveness: the switch in StatementDispatcher::dispatch_stmt() has
// no default case, so the compiler warns with -Wswitch when a new
// StatementKind is added to the enum but not to the dispatcher switch.
//
// Groups:
//   Declarations:  VariableDeclStatement
//   Assignment:    AssignmentStatement, CompoundAssignmentStatement,
//                  IncrementStatement, DecrementStatement
//   Control flow:  IfStatement, ForStatement, WhileStatement,
//                  MatchStatement, ReturnStatement, BreakStatement,
//                  ContinueStatement
//   Expressions:   ExpressionStatement
//   Blocks:        BlockStatement
//   Error:         TryStatement
//   Destructuring: TupleDestructuringStatement

void Compiler::compile_statement(const Statement& stmt) {
    dispatch_stmt(stmt);
}

void Compiler::visit_break(const BreakStatement& stmt) {
    if (!current_scope().loop_context.is_active()) {
        auto e = compiler_errors::break_outside_loop();
        error(e.message, stmt.location, e.hint);
        return;
    }

    emit_loop_scope_unwind(stmt.location);

    auto jump = emit_jump(Op::Jump, stmt.location);
    current_scope().loop_context.add_break(jump);
}

void Compiler::visit_continue(const ContinueStatement& stmt) {
    if (!current_scope().loop_context.is_active()) {
        auto e = compiler_errors::continue_outside_loop();
        error(e.message, stmt.location, e.hint);
        return;
    }

    emit_loop_scope_unwind(stmt.location);
    emit_loop(current_scope().loop_context.current_start(), stmt.location);
}

void Compiler::emit_try_unwind(std::size_t count, SourceLocation loc) {
    auto& ctx = current_scope().exception_context;

    // Unwind the innermost `count` handlers one at a time, detaching each
    // handler BEFORE emitting its finally body but leaving the handlers OUTER
    // to it active while that finally is compiled.
    //
    // Detaching the current handler first is what bounds the recursion:
    // compiling a finally body can itself compile a non-local exit
    // (return/break/continue or `?`), and if the handler whose finally is being
    // emitted stayed active that exit would re-enter this routine over the same
    // handler and recurse without bound — a compile-time stack overflow
    // (e.g. `try { return 1 } finally { return 2 }`).
    //
    // Keeping the OUTER handlers active is equally essential for correctness: a
    // non-local exit inside a finally must still run every ENCLOSING finally
    // (Java/Python semantics) — e.g. an inner `return` has to trigger the outer
    // finally.  Popping all `count` handlers up front would leave no active
    // handler while the inner finally is compiled, silently skipping the outer
    // finally bodies.  Emitting incrementally lets the nested exit unwind the
    // still-active outer handlers; any duplicate finally emitted afterwards on
    // this path is dead code following the nested exit's unconditional jump.
    //
    // The detached handlers are restored before returning so the caller's
    // ongoing try/finally bookkeeping is unaffected.
    std::vector<const std::vector<StatementPtr>*> detached;
    detached.reserve(count);

    for (std::size_t i = 0; i < count && ctx.is_active(); ++i) {
        const auto* finally_body = ctx.rbegin()->finally_body;
        ctx.pop();
        detached.push_back(finally_body);

        emit(Op::TryEnd, loc);

        if (finally_body != nullptr) {
            const ScopeDepthGuard scope(*this);

            for (const auto& s : *finally_body) {
                compile_statement(*s);
            }
        }
    }

    // Restore the detached handlers (outermost first, innermost last) so the
    // enclosing try/finally compilation continues with an unchanged context.
    for (auto it = detached.rbegin(); it != detached.rend(); ++it) {
        ctx.push(*it);
    }
}

// Scope-unwinding helper for break/continue inside loops.
// Pops locals added since the loop began and unwinds any try blocks
// nested inside the loop. This is the single consolidated point for
// all loop-exit scope cleanup — called by both break and continue
// compilation paths above.
void Compiler::emit_loop_scope_unwind(SourceLocation loc) {
    const auto& loop_ctx = current_scope().loop_context;

    // Pop locals from inner scopes before jumping out of the loop.
    for (auto& local : std::views::reverse(current_scope().locals)) {
        if (local.depth <= loop_ctx.current_scope_depth()) {
            break;
        }

        emit(Op::Pop, loc);
    }

    // Unwind exception handlers and emit finally blocks for try
    // blocks nested inside the current loop.
    auto tries_inside =
        current_scope().exception_context.depth() - loop_ctx.current_try_depth_at_entry();
    emit_try_unwind(tries_inside, loc);
}

void Compiler::compile_variable_decl(const VariableDeclStatement& stmt) {
    if (stmt.initializer) {
        compile_expression(*stmt.initializer);
    } else {
        emit(Op::None, stmt.location);
    }

    // Integer → number widening: if the variable is declared as `number`,
    // ensure the value on the stack is promoted to a double.
    emit_number_widening_if_needed(stmt.type, stmt.location);

    // Value semantics: deep-copy the initializer for mutable bindings
    // so that mutation of the copy does not affect the original.
    if (stmt.is_mutable && stmt.initializer) {
        emit(Op::Clone, stmt.location);
    }

    if (current_scope().scope_depth > 0) {
        // Local variable — it's already on the stack.
        (void)declare_local(stmt.name, stmt.is_mutable, stmt.location);

        // Track number-typed locals for widening on assignment.
        // Uses TypeAnnotation::is_number_type() to check the type.
        // The check runs once per variable declaration, and the result is cached
        // in Local::is_number_type for O(1) lookups during assignment compilation.
        if (stmt.type.is_number_type()) {
            current_scope().locals.back().is_number_type = true;
        }
    } else {
        // Global variable.
        auto name_idx = add_name(stmt.name);
        emit_u16(Op::SetGlobal, name_idx, stmt.location);
        emit(Op::Pop, stmt.location);
    }
}

void Compiler::compile_return(const ReturnStatement& stmt) {
    // Tail-call optimization: if we're returning the result of a direct
    // function call and we're not inside a try block, emit a
    // TailCall instead of Call + Return.
    const bool inside_try = current_scope().exception_context.is_active();

    if (stmt.value && stmt.value->kind == ExpressionKind::Call && !inside_try) {
        const auto& call = static_cast<const CallExpression&>(*stmt.value);

        // Only optimize simple calls (no named arguments).
        if (call.named_arguments.empty()) {
            if (call.arguments.size() > CompilerLimits::k_max_arguments) {
                auto e =
                    compiler_errors::too_many_tail_call_arguments(CompilerLimits::k_max_arguments);
                error(e.message, stmt.location, e.hint);
                return;
            }

            // Compile the callee and arguments.
            compile_expression(*call.callee);

            // Mirror compile_call: the callee and each already-compiled argument
            // stay on the operand stack while later arguments are compiled, so
            // reserve a placeholder local per temporary. Without this, a
            // value-producing block (a match/if used as an expression) passed as
            // a tail-call argument computes local slot indices that ignore those
            // temporaries and corrupts the stack.
            std::size_t scratch = 0;
            for (const auto& arg : call.arguments) {
                reserve_scratch_slots(1, stmt.location);
                ++scratch;
                compile_expression(*arg);
            }
            release_scratch_slots(scratch);

            emit_u8(Op::TailCall, static_cast<std::uint8_t>(call.arguments.size()), stmt.location);
            emit(Op::Return, stmt.location);
            return;
        }
    }

    if (stmt.value) {
        compile_expression(*stmt.value);
    } else {
        emit(Op::None, stmt.location);
    }

    // If we are inside one or more try blocks, emit TryEnd (and any
    // pending finally blocks) before returning so that exception handlers
    // are properly unwound and cleanup code runs even on early return.
    emit_try_unwind(current_scope().exception_context.depth(), stmt.location);

    emit(Op::Return, stmt.location);
}

void Compiler::compile_if_statement(const IfStatement& stmt) {
    compile_expression(*stmt.condition);

    auto then_jump = emit_jump(Op::JumpIfFalse, stmt.location);
    emit(Op::Pop, stmt.location); // Pop the condition.

    {
        const ScopeDepthGuard scope(*this);

        for (const auto& s : stmt.then_body) {
            compile_statement(*s);
        }
    }

    auto else_jump = emit_jump(Op::Jump, stmt.location);
    patch_jump(then_jump);
    emit(Op::Pop, stmt.location); // Pop the condition.

    {
        const ScopeDepthGuard scope(*this);

        for (const auto& s : stmt.else_body) {
            compile_statement(*s);
        }
    }

    patch_jump(else_jump);
}

void Compiler::compile_try(const TryStatement& stmt) {
    const bool has_finally = !stmt.finally_body.empty();
    const bool has_catch = !stmt.catch_var.empty();

    // A try block with BOTH a catch and a finally is lowered as two nested
    // single-handler layers, equivalent to:
    //
    //     try { try A catch(e) B } finally C
    //
    // This guarantees the finally body C runs on every exit path from BOTH the
    // try body A and the catch body B — normal completion, a caught error, an
    // error raised inside the catch body, or break/continue/return out of
    // either body.  Compiling it as a single handler (below) would leave the
    // catch body unprotected, so finally would be skipped whenever the catch
    // body threw or returned.
    if (has_catch && has_finally) {
        compile_try_catch_finally(stmt);
        return;
    }

    // ── Try body ──────────────────────────────────────────────

    // Track this try block for break/continue/return exception handler unwinding.
    current_scope().exception_context.push(has_finally ? &stmt.finally_body : nullptr);

    // TryCatch <catch_offset> — registers exception handler.
    // If a runtime error occurs, the VM jumps to the catch block.
    auto catch_jump = emit_jump(Op::TryCatch, stmt.location);

    {
        const ScopeDepthGuard scope(*this);

        for (const auto& s : stmt.try_body) {
            compile_statement(*s);
        }
    }

    // Try body completed normally — remove exception handler.
    emit(Op::TryEnd, stmt.location);
    current_scope().exception_context.pop();

    auto finally_jump = emit_jump(Op::Jump, stmt.location);

    // ── Catch clause ──────────────────────────────────────────

    // Error string is on top of stack.
    patch_jump(catch_jump);

    if (has_catch) {
        // User-provided catch block.
        const ScopeDepthGuard scope(*this);
        (void)declare_local(stmt.catch_var, false, stmt.location);

        for (const auto& s : stmt.catch_body) {
            compile_statement(*s);
        }
    } else if (has_finally) {
        // No catch block, only finally — save error, run finally, re-throw.
        // GetLocal must happen before end_scope pops the error local.
        {
            const ScopeDepthGuard scope(*this);
            (void)declare_local(to_string(HiddenVar::Error), false, stmt.location);

            for (const auto& s : stmt.finally_body) {
                compile_statement(*s);
            }

            const auto err_slot = resolve_local(to_string(HiddenVar::Error));
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the local was just declared above.
            emit_u16(Op::GetLocal, *err_slot, stmt.location);
        }
        emit(Op::Rethrow, stmt.location);
    } else {
        // The VM always pushes the error string; discard it when no handler.
        emit(Op::Pop, stmt.location);
    }

    // ── Finally clause ────────────────────────────────────────

    // Normal fall-through path.
    patch_jump(finally_jump);

    if (has_finally) {
        compile_finally_body(stmt);
    }
}

void Compiler::compile_finally_body(const TryStatement& stmt) {
    const ScopeDepthGuard scope(*this);

    for (const auto& s : stmt.finally_body) {
        compile_statement(*s);
    }
}

void Compiler::compile_try_catch_finally(const TryStatement& stmt) {
    // Lowers `try A catch(e) B finally C` as nested single-handler layers:
    //
    //     try { try A catch(e) B } finally C
    //
    // The OUTER layer is a try/finally whose protected body is the INNER
    // try/catch.  Because the outer handler stays active while the catch body
    // B runs, any error raised inside B (and any break/continue/return) unwinds
    // through the outer finally C before propagating — matching the documented
    // guarantee that finally always runs.

    // ── Outer layer: try { <inner> } finally C ────────────────
    current_scope().exception_context.push(&stmt.finally_body);
    auto outer_handler_jump = emit_jump(Op::TryCatch, stmt.location);

    {
        const ScopeDepthGuard outer_scope(*this);

        // ── Inner layer: try A catch(e) B ─────────────────────
        current_scope().exception_context.push(nullptr);
        auto inner_handler_jump = emit_jump(Op::TryCatch, stmt.location);

        {
            const ScopeDepthGuard try_scope(*this);

            for (const auto& s : stmt.try_body) {
                compile_statement(*s);
            }
        }

        // Inner try body completed normally — remove inner handler.
        emit(Op::TryEnd, stmt.location);
        current_scope().exception_context.pop();

        auto inner_done_jump = emit_jump(Op::Jump, stmt.location);

        // Inner catch clause — error string is on top of stack.
        patch_jump(inner_handler_jump);
        {
            const ScopeDepthGuard catch_scope(*this);
            (void)declare_local(stmt.catch_var, false, stmt.location);

            for (const auto& s : stmt.catch_body) {
                compile_statement(*s);
            }
        }

        patch_jump(inner_done_jump);
        // ── End inner layer ───────────────────────────────────
    }

    // Inner try/catch completed normally — remove outer handler.
    emit(Op::TryEnd, stmt.location);
    current_scope().exception_context.pop();

    auto outer_done_jump = emit_jump(Op::Jump, stmt.location);

    // Outer cleanup — a runtime error escaped the try body or the catch body.
    // Save it, run finally, then re-throw so it propagates past this block.
    // GetLocal must happen before end_scope pops the error local.
    patch_jump(outer_handler_jump);
    {
        const ScopeDepthGuard cleanup_scope(*this);
        (void)declare_local(to_string(HiddenVar::Error), false, stmt.location);

        for (const auto& s : stmt.finally_body) {
            compile_statement(*s);
        }

        const auto err_slot = resolve_local(to_string(HiddenVar::Error));
        emit_u16(Op::GetLocal, *err_slot, stmt.location);
    }
    emit(Op::Rethrow, stmt.location);

    // Normal fall-through path — run finally and continue after the block.
    patch_jump(outer_done_jump);
    compile_finally_body(stmt);
}

void Compiler::compile_expression_statement(const ExpressionStatement& stmt) {
    compile_expression(*stmt.expression);
    emit(Op::Pop, stmt.location); // Discard the expression result.
}

void Compiler::compile_body_as_expression(const std::vector<StatementPtr>& body,
                                          SourceLocation loc) {
    if (body.empty()) {
        emit(Op::None, loc);
        return;
    }

    auto pre_local_count = current_scope().locals.size();

    // ── Compile non-final statements ──
    for (std::size_t i = 0; i + 1 < body.size(); ++i) {
        compile_statement(*body[i]);
    }

    // ── Compile final statement as expression ──
    const auto& last = *body.back();

    if (last.kind == StatementKind::Expression) {
        compile_expression(*static_cast<const ExpressionStatement&>(last).expression);
    } else if (last.kind == StatementKind::If) {
        // An if-statement as the last item in an expression body — compile it
        // as an if-expression so the branch values remain on the stack.
        const auto& if_stmt = static_cast<const IfStatement&>(last);

        compile_expression(*if_stmt.condition);
        auto then_jump = emit_jump(Op::JumpIfFalse, if_stmt.location);
        emit(Op::Pop, if_stmt.location);

        compile_body_as_expression(if_stmt.then_body, if_stmt.location);

        auto else_jump = emit_jump(Op::Jump, if_stmt.location);
        patch_jump(then_jump);
        emit(Op::Pop, if_stmt.location);

        if (!if_stmt.else_body.empty()) {
            compile_body_as_expression(if_stmt.else_body, if_stmt.location);
        } else {
            emit(Op::None, if_stmt.location);
        }

        patch_jump(else_jump);
    } else if (last.kind == StatementKind::Match) {
        // A match-statement as the last item in an expression body — compile it
        // as a match-expression so the selected arm's value remains on the stack.
        // The type checker enforces exhaustiveness in this position, so an arm
        // always matches and leaves exactly one value.
        compile_match_statement_as_expression(static_cast<const MatchStatement&>(last));
    } else {
        compile_statement(last);
        emit(Op::None, loc);
    }

    // ── Clean up local variables ──
    auto locals_declared = current_scope().locals.size() - pre_local_count;

    if (locals_declared > 0) {
        // Stack: [... | local_0 | local_1 | ... | local_N | result]
        // Save the expression result into the first body-local's slot.
        emit_u16(Op::SetLocal, static_cast<std::uint16_t>(pre_local_count), loc);

        // Pop N values: the result copy on TOS plus N-1 other locals.
        for (std::size_t i = 0; i < locals_declared; ++i) {
            emit(Op::Pop, loc);
        }

        // The saved value at pre_local_count is now on TOS.
        // Remove the locals from compiler tracking so end_scope() won't
        // emit duplicate pops.
        while (current_scope().locals.size() > pre_local_count) {
            current_scope().locals.pop_back();
        }
    }
}

void Compiler::compile_block(const BlockStatement& stmt) {
    const ScopeDepthGuard scope(*this);

    for (const auto& s : stmt.statements) {
        compile_statement(*s);
    }
}

} // namespace luma
