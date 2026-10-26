// vm_helpers.cpp — Shared VM infrastructure methods.
//
// Core call dispatch, error handling, and exception management.
// Category-specific dispatch helpers live in vm_dispatch_*.cpp files.

#include <exception>
#include <format>
#include <shared_mutex>
#include <span>
#include <string>
#include <vector>

#include "analysis/errors/error.hpp"
#include "common/resource_limits.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_error_messages.hpp"

namespace luma {

// ─────────── Call dispatch ───────────

void VM::call_value(const Value& callee, int arg_count) {
    if (callee.is_function()) {
        call_function(*callee.as_function(), arg_count);
    } else if (callee.is_native_function()) {
        call_native(*callee.as_native_function(), arg_count);
    } else {
        runtime_error(vm_errors::cannot_call(callee.display_type_name()));
    }
}

void VM::call_function(const FunctionValue& func, int arg_count) {
    if (func.compiled != nullptr) {
        validate_call_depth();
        check_cancellation();

        // The type checker rejects a call with too few required arguments at
        // the source level, but the REPL skips type checking and a crafted
        // .lumc can carry an arbitrary Call arg_count, so the VM must reject
        // it here too — otherwise a missing required parameter would silently
        // run as `none` instead of raising a clear error.
        const auto required_arity = func.compiled->required_arity;

        if (arg_count < required_arity) [[unlikely]] {
            runtime_error(vm_errors::missing_required_arguments(required_arity, arg_count));
        }

        // Pad missing optional parameters with None values.
        const auto arity = func.compiled->arity;

        if (arg_count < arity) {
            for (int i = arg_count; i < arity; ++i) {
                push(Value{});
            }

            arg_count = arity;
        }

        // Compiled function — collect args and run bytecode.
        // Arguments are already on the stack. Set up a new call frame.
        // The stack currently has: [callee] [arg0] [arg1] ... [argN]
        // We need: [slot0=callee] [slot1=arg0] ... in the frame.
        const auto base_slot = call_frame_base_slot(arg_count);
        stack_.frames.push_back({
            .function = func.compiled,
            .ip = func.compiled->chunk().code.data(),
            .code_end = func.compiled->chunk().code.data() + func.compiled->chunk().code.size(),
            .slot_offset = base_slot,
        });
        auto& frame = stack_.frames.back();

        // Store the closure for upvalue access.
        auto& callee_val = stack_.base[base_slot];

        if (callee_val.is_function()) {
            frame.closure = callee_val.as_function().get();
        }

        return;
    }

    // Tree-walker FunctionValue — cannot be run in the VM.
    // Collect arguments and push a placeholder.
    for (int i = 0; i < arg_count; ++i) {
        (void)pop();
    }

    (void)pop(); // Pop the callee itself.
    push(Value{});
}

void VM::call_native(const NativeFunctionValue& func, int arg_count) {
    auto args = pop_sequence(static_cast<std::size_t>(arg_count));

    auto callee = pop(); // Pop the callee — keep it alive during the call.

    try {
        auto result =
            func.function(std::span<const Value>{args.data(), args.size()}, current_location());
        push(std::move(result));
    } catch (const std::runtime_error&) {
        // RuntimeError (with its Value payload), CancelledException, and every
        // other runtime_error-derived error already honour the catchable-error
        // contract — propagate them unchanged so try/catch and task
        // cancellation keep working. (ExitSignal is not a std::exception, so it
        // is unaffected by the broader handler below.)
        throw;
    } catch (const std::exception& error) {
        // A built-in hit an internal fault that is not a runtime_error — for
        // example std::bad_variant_access from an unguarded argument-type
        // access, or std::bad_alloc / std::length_error from an oversized
        // allocation. Convert it into a catchable RuntimeError instead of
        // letting it abort the interpreter, honouring the documented
        // "errors are catchable" contract (Luma_Error_Handling.md).
        throw RuntimeError{std::string{"internal error in built-in function: "} + error.what(),
                           current_location()};
    }
}

// ─────────── Error handling ───────────

void VM::validate_call_depth() const {
    const auto limit = effective_max_call_depth();

    if (stack_.frames.size() >= limit) [[unlikely]] {
        runtime_error(vm_errors::call_stack_overflow_recursion(static_cast<int>(limit)),
                      vm_errors::hint_check_recursion);
    }
}

void VM::runtime_error(std::string_view message, std::string_view hint) const {
    if (hint.empty()) {
        throw RuntimeError{message, current_location()};
    }
    throw RuntimeError{message, current_location(), std::string{hint}};
}

SourceLocation VM::current_location() const {
    if (stack_.frames.empty()) {
        return {};
    }

    const auto& frame = stack_.frames.back();
    auto offset = static_cast<std::size_t>(frame.ip - frame.function->chunk().code.data());

    return frame.function->chunk().location_at(offset > 0 ? offset - 1 : 0);
}

// ─────────── Exception handling ───────────

// Entry points for the run() dispatch loop's catch blocks.  Each materialises
// the error payload for the core handle_exception() below.  The
// std::optional<Value> is constructed here, in a normal stack frame, so it is
// never destroyed inside the run() catch funclet — see the note on the
// declarations in vm.hpp for why that matters on clang-cl.
bool VM::handle_exception(const RuntimeError& e) {
    return handle_exception(e.what(), e.get_error_payload<Value>());
}

bool VM::handle_exception(const CancelledException& e) {
    return handle_cancellation(e.what());
}

bool VM::handle_exception(const std::runtime_error& e) {
    return handle_exception(e.what(), std::nullopt);
}

void VM::notify_exception_hook(const std::string& message, bool is_caught) {
    // THREAD_SAFETY: snapshots captured under shared_lock; safe to use across threads.
    auto exc_hook_copy = debug_.copy_hook(&DebugCallbacks::exception_hook);
    auto pause_copy = debug_.copy_hook(&DebugCallbacks::pause_callback);

    if (exc_hook_copy && exc_hook_copy(message, is_caught)) {
        if (pause_copy) {
            pause_copy();
        }
    }
}

// Dispatches the active C++ exception to the nearest Luma `catch` handler.
// Returns true when a handler was found and the VM state was unwound to it
// (the dispatch loop then resumes at the handler's catch_ip).  Returns false
// when no handler exists; the caller re-raises the active exception with a bare
// `throw;` inside its own catch block.  Re-raising here instead would be a
// cross-function rethrow from a nested call, which the clang-cl MSVC-EH funclet
// codegen also mishandles.
bool VM::handle_exception(const std::string& message, const std::optional<Value>& error_value) {
    // First determine if the exception will be caught, then report it to the
    // debugger with that status.
    const bool has_handler = exception_handler_.has_handler_for(base_depth_);

    notify_exception_hook(message, has_handler);

    // Then handle, or signal the caller to re-raise.
    if (!has_handler) {
        return false; // No Luma handler — caller re-raises lexically.
    }

    auto handler = exception_handler_.pop();

    // Validate the handler before trusting it.  complete_return() drops a
    // returning frame's handlers, so a well-formed handler always belongs to a
    // live frame, records a stack depth at or below the current top, and
    // targets that frame's own code.  Anything else comes from crafted
    // bytecode; installing its catch_ip into an unrelated frame would let the
    // fetch loop read outside that frame's chunk.  Treat it as "no handler".
    if (!is_valid_handler(handler)) [[unlikely]] {
        return false;
    }

    // `catch(err)` binds a string — the type checker types `err` as one — so a
    // non-string payload (a typed error from `Result.unwrap` on
    // `result<T, E>`) binds its text instead of the value.
    if (error_value.has_value()) {
        resume_at_handler(handler, error_value->is_string() ? *error_value
                                                            : Value{error_value->to_string()});
    } else {
        resume_at_handler(handler, Value{std::string{message}});
    }

    // A `finally` entered while its task is being cancelled runs to completion
    // whatever error unwound into it (see handle_cancellation()).
    if (is_self_cancelled() && is_cleanup(handler)) [[unlikely]] {
        exception_handler_.push_cancel_shield(handler.frame_index, handler.stack_depth);
    }

    return true;
}

bool VM::handle_cancellation(const std::string& message) {
    // A task that is itself being cancelled must stop: its `catch` blocks are
    // skipped (and dropped, as their frames are being abandoned) so they
    // cannot swallow the cancellation, but every `finally` on the way out
    // still runs.  Without this, a `finally` caught the cancellation and its
    // Rethrow re-raised it as an ordinary RuntimeError, failing the whole
    // task_scope with "task cancelled".
    const bool self_cancelled = is_self_cancelled();
    std::optional<ExceptionHandler> target;
    bool cleanup = false;

    while (exception_handler_.has_handler_for(base_depth_)) {
        const auto handler = exception_handler_.pop();

        if (!is_valid_handler(handler)) [[unlikely]] {
            break; // Crafted bytecode — treat as "no handler" (see above).
        }

        cleanup = is_cleanup(handler);

        if (cleanup || !self_cancelled) {
            target = handler;
            break;
        }
    }

    notify_exception_hook(message, target.has_value());

    if (!target.has_value()) {
        return false;
    }

    // A cleanup handler gets the marker so its Rethrow re-raises the
    // cancellation; a `catch` block (another task's cancellation, observed via
    // await) binds the message like any other error.
    resume_at_handler(*target, cleanup ? cancellation_marker() : Value{std::string{message}});

    // Let the cleanup code of a cancelled task run to completion instead of
    // stopping again at its first call or loop iteration.
    if (cleanup && self_cancelled) {
        exception_handler_.push_cancel_shield(target->frame_index, target->stack_depth);
    }

    return true;
}

bool VM::is_cleanup(const ExceptionHandler& handler) const {
    const auto& code = stack_.frames[handler.frame_index].function->chunk().code;

    return is_cleanup_handler(code, static_cast<std::size_t>(handler.catch_ip - code.data()));
}

void VM::resume_at_handler(const ExceptionHandler& handler, Value error_value) {
    // Unwind any task_scope blocks entered after this handler was installed.
    // The frame/stack unwind below cannot see them, so without this their
    // children would be orphaned — neither cancelled nor joined — and leak on
    // task_manager_.task_scopes, violating the structured-concurrency guarantee
    // that every child completes before its scope exits.  Mirror the
    // cancel → join → pop cleanup that Op::TaskScopeEnd performs on failure.
    unwind_task_scopes_to(handler.task_scope_depth);

    while (stack_.frames.size() > handler.frame_index + 1) {
        stack_.frames.pop_back();
    }

    // A shielded cleanup handler the error unwinds out of is over.
    exception_handler_.end_cancel_shields_from(handler.frame_index, handler.stack_depth);

    // Destroy the values the unwound code left on the stack (see
    // VMStack::truncate_to) rather than merely moving the top down.
    stack_.truncate_to(handler.stack_depth);

    // Push the error value for the handler code.
    push(std::move(error_value));

    stack_.frames.back().ip = handler.catch_ip;
}

bool VM::is_valid_handler(const ExceptionHandler& handler) const noexcept {
    if (handler.frame_index >= stack_.frames.size() || handler.stack_depth > stack_size() ||
        handler.task_scope_depth > task_manager_.task_scopes.size()) {
        return false;
    }

    const auto& frame = stack_.frames[handler.frame_index];
    const auto* code_start = frame.function->chunk().code.data();

    return handler.catch_ip >= code_start && handler.catch_ip <= frame.code_end;
}

} // namespace luma
