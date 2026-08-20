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
        if (stack_.frames.size() >= static_cast<std::size_t>(ResourceLimits::max_call_depth))
            [[unlikely]] {
            runtime_error(vm_errors::call_stack_overflow(ResourceLimits::max_call_depth));
        }

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

bool VM::handle_exception(const std::runtime_error& e) {
    return handle_exception(e.what(), std::nullopt);
}

// Dispatches the active C++ exception to the nearest Luma `catch` handler.
// Returns true when a handler was found and the VM state was unwound to it
// (the dispatch loop then resumes at the handler's catch_ip).  Returns false
// when no handler exists; the caller re-raises the active exception with a bare
// `throw;` inside its own catch block.  Re-raising here instead would be a
// cross-function rethrow from a nested call, which the clang-cl MSVC-EH funclet
// codegen also mishandles.
bool VM::handle_exception(const std::string& message, const std::optional<Value>& error_value) {
    // First determine if the exception will be caught.
    const bool has_handler = exceptions_.has_handler_for(base_depth_);

    // Then call the hook with the is_caught status.
    // THREAD_SAFETY: snapshots captured under shared_lock; safe to use across threads.
    auto exc_hook_copy = debug_.copy_hook(&DebugCallbacks::exception_hook);
    auto pause_copy = debug_.copy_hook(&DebugCallbacks::pause_callback);

    if (exc_hook_copy && exc_hook_copy(message, has_handler)) {
        if (pause_copy) {
            pause_copy();
        }
    }

    // Then handle, or signal the caller to re-raise.
    if (!has_handler) {
        return false; // No Luma handler — caller re-raises lexically.
    }

    auto handler = exceptions_.pop_handler();

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

    stack_.top = stack_.base + handler.stack_depth;

    // Push error value for catch block.
    if (error_value.has_value()) {
        push(*error_value);
    } else {
        push(Value{std::string{message}});
    }

    stack_.frames.back().ip = handler.catch_ip;
    return true;
}

} // namespace luma
