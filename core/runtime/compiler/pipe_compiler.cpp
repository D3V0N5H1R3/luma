// ─────────────────────────────────────────────────────────────────────────────
// Pipe expression compilation — Compiler method implementations
// ─────────────────────────────────────────────────────────────────────────────
// Extracted from expression_compiler.cpp: pipe (|>) and error-pipe (!>) operators,
// plus the shared emit_call_with_args helper they both use.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>

#include "analysis/source/source_location.hpp"
#include "runtime/compiler/compiler.hpp"
#include "runtime/compiler/compiler_errors.hpp"
#include "runtime/compiler/opcode.hpp"

namespace luma {

void Compiler::emit_call_with_args(const CallExpression& call, std::uint8_t pos_count,
                                   SourceLocation loc) {
    // The callee and piped value pushed by the caller remain on the operand
    // stack beneath these arguments; the caller has already reserved placeholder
    // locals for them. Reserve a placeholder for each additional argument left on
    // the stack so a value-producing block (match/if used as an expression)
    // argument computes local slot indices matching its true runtime position.
    std::size_t scratch = 0;
    bool first = true;

    for (const auto& arg : call.arguments) {
        if (!first) {
            reserve_scratch_slots(1, loc);
            ++scratch;
        }
        first = false;
        compile_expression(*arg);
    }

    if (!call.named_arguments.empty()) {
        for (const auto& named : call.named_arguments) {
            if (!first) {
                reserve_scratch_slots(1, loc);
                ++scratch;
            }
            first = false;
            (void)emit_constant(Value{named.name}, loc);
            reserve_scratch_slots(1, loc);
            ++scratch;
            compile_expression(*named.value);
        }
        release_scratch_slots(scratch);
        emit_u8(Op::CallNamed, pos_count, loc);
        emit_raw_byte(static_cast<std::uint8_t>(call.named_arguments.size()));
    } else {
        release_scratch_slots(scratch);
        emit_u8(Op::Call, pos_count, loc);
    }
}

void Compiler::compile_pipe(const PipeExpression& expr) {
    // left |> right — evaluate left, then call right(left_result) as first argument.
    // The pipe operator inserts the left value as the first argument to the right call.
    if (expr.right->kind == ExpressionKind::Call) {
        const auto& call = static_cast<const CallExpression&>(*expr.right);

        // Compile the callee (e.g. Array.filter).
        compile_expression(*call.callee);

        // The callee remains on the stack beneath the piped value.
        reserve_scratch_slots(1, expr.location);

        // Push the piped value as the first argument.
        compile_expression(*expr.left);

        if (call.arguments.size() >= CompilerLimits::k_max_arguments) {
            release_scratch_slots(1);
            auto e = compiler_errors::too_many_pipe_arguments(CompilerLimits::k_max_arguments - 1);
            error(e.message, expr.location, e.hint);
            return;
        }

        // The piped value remains on the stack beneath the call's own arguments.
        reserve_scratch_slots(1, expr.location);

        auto pos_count = static_cast<std::uint8_t>(1 + call.arguments.size());
        emit_call_with_args(call, pos_count, expr.location);
        release_scratch_slots(2);
    } else {
        // Simple pipe: val |> func — compile callee, compile piped value, call with 1 arg.
        compile_expression(*expr.right);
        reserve_scratch_slots(1, expr.location);
        compile_expression(*expr.left);
        release_scratch_slots(1);
        emit_u8(Op::Call, 1, expr.location);
    }
}

void Compiler::compile_error_pipe(const ErrorPipeExpression& expr) {
    // left !> right — unwrap success from left, pipe into right; short-circuit failure.
    // If left is a failure result, skip the RHS and keep the failure.
    compile_expression(*expr.left);

    // Check if the left value is a success result.
    emit(Op::Dup, expr.location);                               // [val, val]
    emit(Op::IsSuccess, expr.location);                         // [val, bool]
    auto fail_jump = emit_jump(Op::JumpIfFalse, expr.location); // [val, bool]

    // Success path: pop bool, unwrap, call the function.
    emit(Op::Pop, expr.location);    // [val]
    emit(Op::Unwrap, expr.location); // [inner]

    if (expr.right->kind == ExpressionKind::Call) {
        const auto& call = static_cast<const CallExpression&>(*expr.right);

        // The unwrapped value remains on the stack beneath the callee.
        reserve_scratch_slots(1, expr.location);
        compile_expression(*call.callee); // [inner, func]
        emit(Op::Swap, expr.location);    // [func, inner]

        if (call.arguments.size() >= CompilerLimits::k_max_arguments) {
            release_scratch_slots(1);
            auto e =
                compiler_errors::too_many_error_pipe_arguments(CompilerLimits::k_max_arguments - 1);
            error(e.message, expr.location, e.hint);
            return;
        }

        // The piped value remains on the stack beneath the call's own arguments.
        reserve_scratch_slots(1, expr.location);

        auto pos_count = static_cast<std::uint8_t>(1 + call.arguments.size());
        emit_call_with_args(call, pos_count, expr.location);
        release_scratch_slots(2);
    } else {
        // The unwrapped value remains on the stack beneath the function expression.
        reserve_scratch_slots(1, expr.location);
        compile_expression(*expr.right); // [inner, func]
        release_scratch_slots(1);
        emit(Op::Swap, expr.location); // [func, inner]
        emit_u8(Op::Call, 1, expr.location);
    }

    // Wrap the function's return value in success() if it is not already a result.
    emit(Op::EnsureSuccess, expr.location);

    auto end_jump = emit_jump(Op::Jump, expr.location);

    // Failure path: pop bool, keep the failure result on stack.
    patch_jump(fail_jump);
    emit(Op::Pop, expr.location); // [val]  (failure result stays)

    patch_jump(end_jump);
}

} // namespace luma
