// vm_dispatch_concurrency.cpp — Concurrency, iteration, and I/O opcode
// handler methods.
//
// Extracted from vm_helpers.cpp / vm.cpp as part of the VM dispatch split.
// Contains:
//   - handle_spawn, handle_task_scope_end
//   - handle_for_iter_step, handle_for_iter_step_pair
//   - handle_await, handle_task_scope_begin (new)
//   - handle_for_iter_init, handle_print, handle_assert (new)

#include <format>
#include <future>
#include <iostream>
#include <limits>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>

#include "analysis/errors/error.hpp"
#include "common/overflow.hpp"
#include "common/resource_limits.hpp"
#include "common/utf8.hpp"
#include "common/utf8_iterator.hpp"
#include "runtime/concurrency/task_scope.hpp"
#include "runtime/concurrency/thread_pool.hpp"
#include "runtime/interpreter/runtime_exceptions.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/native_function.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_error_messages.hpp"

namespace luma {

// ─────────── Anonymous helpers ───────────

namespace {

// Iterator state indices are now centralised in VMConstants:
//   VMConstants::k_iter_state_iterable (0) — the iterable value
//   VMConstants::k_iter_state_index    (1) — the current iteration index
// Shorthand aliases for use within this translation unit.
constexpr auto k_iter_state_iterable = VMConstants::k_iter_state_iterable;
constexpr auto k_iter_state_index = VMConstants::k_iter_state_index;

/// Throws BytecodeError if callee is neither a native function nor a compiled function.
void validate_spawn_callable(const Value& callee) {
    if (!callee.is_native_function() &&
        (!callee.is_function() || (callee.as_function()->compiled == nullptr))) {
        throw BytecodeError{vm_errors::spawned_not_callable};
    }
}

/// RAII guard that ensures `task_exit_hook(task_id)` is called exactly once
/// when the spawned task lambda exits — whether normally or via exception.
struct TaskExitGuard {
    std::function<void(std::size_t)> hook;
    std::size_t id;

    ~TaskExitGuard() {
        if (hook) {
            hook(id);
        }
    }
};

} // namespace

// ─── Generic iteration dispatch ─────────────────────────────────────────────
//
// iter_step_generic factors out the common iteration protocol shared by all
// eight iter_step_* / iter_step_pair_* helpers:
//
//   1. Call `body` to attempt to push the next element(s) and compute the
//      next index.  `body` returns std::optional<std::int64_t>: the new
//      index on success, or std::nullopt when the sequence is exhausted.
//   2. If the body succeeded, update the index in the iterator state tuple
//      and push true.
//   3. Otherwise push false.
//
// The per-type differences (bounds checking, element extraction, index
// arithmetic, key pushing) stay in each lambda — only the bookkeeping is
// shared.

template <typename BodyFn> void VM::iter_step_generic(TupleValue& state, BodyFn&& body) {
    auto next_idx = body();

    if (next_idx.has_value()) {
        state.elements[k_iter_state_index] = Value{*next_idx};
        push(Value{true});
    } else {
        push(Value{false});
    }
}

// ─────────── Spawn helpers ───────────

void VM::propagate_debug_hooks(const DebugCallbacks& parent_cbs, VM& child) {
    if (parent_cbs.debug_hook) {
        child.set_debug_hook(parent_cbs.debug_hook);
        child.set_pause_callback(parent_cbs.pause_callback);
        child.request_pause_check();
    }

    if (parent_cbs.exception_hook) {
        child.set_exception_hook(parent_cbs.exception_hook);
    }

    if (parent_cbs.data_breakpoint_hook) {
        child.set_data_breakpoint_hook(parent_cbs.data_breakpoint_hook);
    }
}

Value VM::execute_spawned_callable(VM& task_vm, Value& callee, std::vector<Value>& args,
                                   SourceLocation loc) {
    if (callee.is_native_function()) {
        return callee.as_native_function()->function(args, loc);
    }

    if (callee.is_function() && (callee.as_function()->compiled != nullptr)) {
        task_vm.push(callee);

        for (auto& arg : args) {
            task_vm.push(std::move(arg));
        }

        task_vm.call_value(callee, static_cast<int>(args.size()));
        return task_vm.run_to_return();
    }

    throw BytecodeError{vm_errors::spawned_not_callable};
}

void VM::prepare_spawn_environment(VM& task_vm, const DebugCallbacks& debug_cbs, int task_id) {
    propagate_debug_hooks(debug_cbs, task_vm);

    if (debug_cbs.task_spawn_hook) {
        debug_cbs.task_spawn_hook(task_vm, task_id);
    }
}

void VM::build_spawn_future(VM& task_vm, std::shared_ptr<std::promise<Value>>& promise,
                            Value& callee, std::vector<Value>& args, SourceLocation loc,
                            const std::shared_ptr<CancellationToken>& cancel_token) {
    const auto* prev_cancel = detail::active_cancel_token;
    detail::active_cancel_token = cancel_token ? &cancel_token : nullptr;

    try {
        auto result = execute_spawned_callable(task_vm, callee, args, loc);
        detail::active_cancel_token = prev_cancel;
        promise->set_value(std::move(result));
    } catch (...) {
        detail::active_cancel_token = prev_cancel;
        promise->set_exception(std::current_exception());
    }
}

auto VM::create_spawn_callable(
    std::shared_ptr<std::promise<Value>> promise, Value callable, std::vector<Value> args,
    SourceLocation loc,
    // Lifetime: `compiled_fns` is a raw pointer to the parent VM's function
    // table. The ThreadPool destructor joins all worker threads before
    // returning, which means every spawned task completes before the parent
    // VM (and its compiled_fns vector) is destroyed. The pointer is therefore
    // guaranteed to remain valid for the entire lifetime of this lambda.
    const std::vector<CompiledFunction>* compiled_fns, EnvPtr env,
    std::shared_ptr<CancellationToken> cancel_token, ThreadPool& pool_ref, DebugCallbacks debug_cbs,
    int task_id) -> std::function<void()> {
    return [promise = std::move(promise), callee = std::move(callable), args = std::move(args),
            compiled_fns, env = std::move(env), cancel_token = std::move(cancel_token), &pool_ref,
            loc, debug_cbs = std::move(debug_cbs), task_id]() mutable {
        try {
            if (cancel_token && cancel_token->is_cancelled()) {
                // No task VM is constructed on this early-out path, so there is
                // no dangling-pointer window to close; fire the exit hook
                // directly to preserve the prior exit notification.
                if (debug_cbs.task_exit_hook) {
                    debug_cbs.task_exit_hook(task_id);
                }

                promise->set_exception(
                    std::make_exception_ptr(CancelledException{"task cancelled"}));
                return;
            }

            VM task_vm{env, pool_ref, compiled_fns};

            // RAII guard that fires task_exit_hook exactly once when the task
            // finishes.  Declared *after* task_vm so it is destroyed *first*
            // (reverse declaration order): the hook nulls this task's
            // ThreadState::vm while task_vm is still alive, closing a
            // use-after-free window in which a concurrent
            // stackTrace/variables/evaluate on the exiting task could
            // dereference the freed VM.
            const TaskExitGuard exit_guard{
                .hook = debug_cbs.task_exit_hook
                            ? std::function<void(std::size_t)>{[&debug_cbs](std::size_t id) {
                                  debug_cbs.task_exit_hook(static_cast<int>(id));
                              }}
                            : std::function<void(std::size_t)>{},
                .id = static_cast<std::size_t>(task_id)};

            const NativeCallableScope scope{task_vm.vm_native_callable_};

            prepare_spawn_environment(task_vm, debug_cbs, task_id);
            build_spawn_future(task_vm, promise, callee, args, loc, cancel_token);
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };
}

// ─────────── Spawn ───────────

void VM::handle_spawn() {
    auto arg_count = static_cast<std::size_t>(read_byte());

    std::vector<Value> args(arg_count);

    // Deep-copy arguments to isolate the spawned task's values from
    // the parent thread. Primitives (null, bool, integer, number, string)
    // are immutable value types — no deep copy needed for thread safety.
    auto popped = pop_sequence(arg_count);
    for (std::size_t i = 0; i < arg_count; ++i) {
        args[i] = popped[i].has_category(ValueCategory::Primitive) ? std::move(popped[i])
                                                                   : popped[i].deep_copy();
    }

    auto callee_val = pop();
    auto callee = callee_val.has_category(ValueCategory::Primitive) ? std::move(callee_val)
                                                                    : callee_val.deep_copy();
    auto loc = current_location();

    validate_spawn_callable(callee);

    auto promise = std::make_shared<std::promise<Value>>();
    auto future = promise->get_future().share();

    auto cancel_token =
        (VMTaskManager::current_scope != nullptr) ? VMTaskManager::current_scope->token() : nullptr;
    auto task_id = task_manager_.next_task_id.fetch_add(1);

    // THREAD_SAFETY: Captures a consistent snapshot of debug callbacks under
    // shared_lock; safe to move into spawned task.
    auto debug_cbs_copy = debug_.copy_all();
    pool().enqueue(create_spawn_callable(promise, std::move(callee), std::move(args), loc,
                                         compiled_functions_, global_env_, cancel_token, pool(),
                                         std::move(debug_cbs_copy), task_id));

    auto task = std::make_shared<TaskValue>(std::move(future), cancel_token);

    if (VMTaskManager::current_scope != nullptr) {
        VMTaskManager::current_scope->add_child(task);
    }

    push(Value{std::move(task)});
}

// ─────────── Await ───────────

void VM::handle_await() {
    // Cooperative cancellation check.
    if ((detail::active_cancel_token != nullptr) &&
        (*detail::active_cancel_token)->is_cancelled()) {
        runtime_error(vm_errors::task_cancelled);
    }

    auto task_val = pop();

    if (!task_val.is_task()) {
        runtime_error(vm_errors::await_requires_task(task_val.display_type_name()));
    }

    const auto& task = task_val.as_task();

    if (!task->future.valid()) {
        runtime_error(vm_errors::await_consumed_task);
    }

    push(task->future.get());
}

// ─────────── Task scope ───────────

void VM::handle_task_scope_begin() {
    auto scope = std::make_unique<TaskScope>(VMTaskManager::current_scope);
    VMTaskManager::current_scope = scope.get();
    task_manager_.task_scopes.push_back(std::move(scope));
}

void VM::retire_task_scope(TaskScope* scope) {
    // Shared concurrency-teardown ordering for both normal end and exceptional
    // unwind: cancel children, wait for them to observe it, restore the parent
    // as current, then pop.  Keeping this in one place means the two paths can
    // never diverge (e.g. joining before cancelling on one of them).
    scope->cancel_all();

    try {
        (void)scope->join_all();
    } catch (...) { // NOLINT(bugprone-empty-catch)
        // Suppress secondary failures from children during teardown; any
        // exception already in flight is the one that propagates.
    }

    VMTaskManager::current_scope = scope->parent();
    task_manager_.task_scopes.pop_back();
}

void VM::handle_task_scope_end() {
    if (task_manager_.task_scopes.empty() || (VMTaskManager::current_scope == nullptr)) {
        runtime_error(vm_errors::task_scope_end_without_begin);
    }

    auto* scope = task_manager_.task_scopes.back().get();

    try {
        auto results = scope->join_all();

        auto arr = std::make_shared<ArrayValue>();
        *arr->elements = std::move(results);
        push(Value{std::move(arr)});

        // Retire the scope only after the result array is safely on the stack.
        // join_all() above keeps VMTaskManager::current_scope pointing at *this*
        // scope so nested task_scope blocks resolve correctly; deferring the
        // restore and pop_back until after push() means that if push() throws
        // (e.g. on stack overflow) the scope is still live and still on
        // task_scopes, so the catch block cleans it up exactly once rather than
        // dereferencing freed memory and popping a second time.
        VMTaskManager::current_scope = scope->parent();
        task_manager_.task_scopes.pop_back();
    } catch (...) {
        retire_task_scope(scope);
        std::rethrow_exception(std::current_exception());
    }
}

void VM::unwind_task_scopes_to(std::size_t target_depth) {
    // Pop innermost-first so each scope's parent() correctly restores
    // VMTaskManager::current_scope.  retire_task_scope() performs the exact
    // cancel → join → restore → pop teardown Op::TaskScopeEnd uses when a child
    // fails.
    while (task_manager_.task_scopes.size() > target_depth) {
        retire_task_scope(task_manager_.task_scopes.back().get());
    }
}

// ─────────── Iteration ───────────
//
// Iterator protocol
// -----------------
// Luma's for-loop iteration uses a two-phase protocol:
//
//   1. **Init** (handle_for_iter_init): pops the iterable, wraps it with a
//      zero-initialised index into a (iterable, index) tuple, and pushes the
//      resulting iterator state onto the stack.
//
//   2. **Next** (handle_for_iter_step / handle_for_iter_step_pair): pops the
//      iterator state, advances it, and pushes either:
//        - [value, true]       for single-value iteration, or
//        - [value, key, true]  for key-value pair iteration,
//      when more elements remain; or [false] when the sequence is exhausted.
//
// Each iterable type has a dedicated helper (iter_step_<type> /
// iter_step_pair_<type>) so the dispatch functions stay thin.

void VM::handle_for_iter_init() {
    auto iterable = pop();
    auto state = std::make_shared<TupleValue>();
    state->elements.push_back(std::move(iterable));
    state->elements.emplace_back(static_cast<std::int64_t>(0));
    push(Value{std::move(state)});
    // Reset the loop iteration counter so sequential loops
    // do not accumulate toward the safety limit.
    loop_iterations_ = 0;
}

// ─── Single-value iteration helpers ──────────────────────────────────────────

/// Advances a range iterator.  Pushes [current_value, true] or [false].
void VM::iter_step_range(TupleValue& state, std::int64_t idx) {
    const auto& range = state.elements[k_iter_state_iterable].as_range();

    iter_step_generic(state, [&]() -> std::optional<std::int64_t> {
        if (would_overflow_add<std::int64_t>(range->start, idx)) {
            return std::nullopt;
        }

        const auto current_val = range->start + idx;
        const bool in_range =
            range->inclusive ? current_val <= range->end : current_val < range->end;

        if (!in_range) {
            return std::nullopt;
        }

        push(Value{current_val});
        return idx + 1;
    });
}

/// Advances an array iterator.  Pushes [element, true] or [false].
void VM::iter_step_array(TupleValue& state, std::int64_t idx) {
    const auto& elems = *state.elements[k_iter_state_iterable].as_array()->elements;

    iter_step_generic(state, [&]() -> std::optional<std::int64_t> {
        if (static_cast<std::size_t>(idx) >= elems.size()) {
            return std::nullopt;
        }

        push(elems[static_cast<std::size_t>(idx)]);
        return idx + 1;
    });
}

/// Advances a string iterator over UTF-8 codepoints.
/// Pushes [codepoint_string, true] or [false].
void VM::iter_step_string(TupleValue& state, std::int64_t idx) {
    const auto& str = state.elements[k_iter_state_iterable].as_string();
    const auto byte_pos = static_cast<std::size_t>(idx);

    iter_step_generic(state, [&]() -> std::optional<std::int64_t> {
        if (byte_pos >= str.size()) {
            return std::nullopt;
        }

        push(Value{utf8_char_at_byte(str, byte_pos)});
        return idx + static_cast<std::int64_t>(utf8_advance(str, byte_pos));
    });
}

/// Advances a dictionary iterator (key iteration).
/// Pushes [key, true] or [false].
void VM::iter_step_dict(TupleValue& state, std::int64_t idx) {
    const auto& entries = state.elements[k_iter_state_iterable].as_dictionary()->entries;

    iter_step_generic(state, [&]() -> std::optional<std::int64_t> {
        if (static_cast<std::size_t>(idx) >= entries.size()) {
            return std::nullopt;
        }

        push(Value{entries[static_cast<std::size_t>(idx)].first});
        return idx + 1;
    });
}

// ─── Key-value pair iteration helpers ────────────────────────────────────────

/// Advances a range pair iterator.  Pushes [value, index, true] or [false].
void VM::iter_step_pair_range(TupleValue& state, std::int64_t idx) {
    const auto& range = state.elements[k_iter_state_iterable].as_range();

    iter_step_generic(state, [&]() -> std::optional<std::int64_t> {
        if (would_overflow_add<std::int64_t>(range->start, idx)) {
            return std::nullopt;
        }

        const auto current_val = range->start + idx;
        const bool in_range =
            range->inclusive ? current_val <= range->end : current_val < range->end;

        if (!in_range) {
            return std::nullopt;
        }

        push(Value{current_val}); // value
        push(Value{idx});         // key (index)
        return idx + 1;
    });
}

/// Advances an array pair iterator.  Pushes [element, index, true] or [false].
void VM::iter_step_pair_array(TupleValue& state, std::int64_t idx) {
    const auto& elems = *state.elements[k_iter_state_iterable].as_array()->elements;

    iter_step_generic(state, [&]() -> std::optional<std::int64_t> {
        if (static_cast<std::size_t>(idx) >= elems.size()) {
            return std::nullopt;
        }

        push(elems[static_cast<std::size_t>(idx)]); // value
        push(Value{idx});                           // key (index)
        return idx + 1;
    });
}

/// Advances a string pair iterator.
/// Pushes [codepoint_string, codepoint_index, true] or [false].
void VM::iter_step_pair_string(TupleValue& state, std::int64_t idx) {
    const auto& str = state.elements[k_iter_state_iterable].as_string();
    const auto byte_pos = static_cast<std::size_t>(idx);

    iter_step_generic(state, [&]() -> std::optional<std::int64_t> {
        if (byte_pos >= str.size()) {
            return std::nullopt;
        }

        push(Value{utf8_char_at_byte(str, byte_pos)});    // value
        push(Value{utf8_codepoint_index(str, byte_pos)}); // key (codepoint index)
        return idx + static_cast<std::int64_t>(utf8_advance(str, byte_pos));
    });
}

/// Advances a dictionary pair iterator.  Pushes [value, key, true] or [false].
void VM::iter_step_pair_dict(TupleValue& state, std::int64_t idx) {
    const auto& entries = state.elements[k_iter_state_iterable].as_dictionary()->entries;

    iter_step_generic(state, [&]() -> std::optional<std::int64_t> {
        if (static_cast<std::size_t>(idx) >= entries.size()) {
            return std::nullopt;
        }

        push(Value{entries[static_cast<std::size_t>(idx)].second}); // value
        push(Value{entries[static_cast<std::size_t>(idx)].first});  // key
        return idx + 1;
    });
}

// ─── Dispatch ────────────────────────────────────────────────────────────────
//
// The eight iter_step_* / iter_step_pair_* functions above each delegate to
// VM::iter_step_generic, which owns the shared bookkeeping (index update and
// continuation flag push).  Each function provides a lambda that performs
// bounds checking, pushes the element(s), and returns the next index
// (or std::nullopt when the sequence is exhausted).
//
// The per-type differences remain explicit in each lambda:
//   • range — overflow-safe arithmetic: current_val = range.start + idx.
//   • string — UTF-8 multibyte: byte-position via utf8_advance(),
//     codepoint index via utf8_codepoint_index().
//   • array — index into a shared_ptr<vector>.
//   • dict — index into a vector<pair<string, Value>>.
//
// dispatch_collection below routes handle_for_iter_step() /
// handle_for_iter_step_pair() to the appropriate function.

void VM::dispatch_iter_step(IterStepFn range_fn, IterStepFn array_fn, IterStepFn dict_fn,
                            IterStepFn string_fn) {
    auto state_val = pop();

    if (!state_val.is_tuple()) {
        runtime_error(vm_errors::invalid_iterator_state);
    }

    const auto& state = state_val.as_tuple();
    // A well-formed iterator state always carries [iterable, index]; reject a
    // malformed tuple (e.g. from a corrupt .lumc that jumps onto this opcode)
    // before indexing, so operator[] never reads out of bounds.
    if (state->elements.size() <= k_iter_state_index) {
        runtime_error(vm_errors::invalid_iterator_state);
    }

    const auto& iterable = state->elements[k_iter_state_iterable];
    // Guard the index element's type as well as the tuple's size: a corrupt
    // .lumc that jumps onto this opcode with a non-integer index element would
    // otherwise make as_integer() throw std::bad_variant_access instead of a
    // clean RuntimeError.
    if (!state->elements[k_iter_state_index].is_integer()) {
        runtime_error(vm_errors::invalid_iterator_state);
    }
    auto idx = state->elements[k_iter_state_index].as_integer();

    if (iterable.is_range()) {
        (this->*range_fn)(*state, idx);
        return;
    }

    dispatch_collection(
        iterable, [&] { (this->*array_fn)(*state, idx); }, [&] { (this->*dict_fn)(*state, idx); },
        [&] { (this->*string_fn)(*state, idx); },
        [&] { runtime_error(vm_errors::cannot_iterate_over(iterable.display_type_name())); });
}

void VM::handle_for_iter_step() {
    dispatch_iter_step(&VM::iter_step_range, &VM::iter_step_array, &VM::iter_step_dict,
                       &VM::iter_step_string);
}

void VM::handle_for_iter_step_pair() {
    dispatch_iter_step(&VM::iter_step_pair_range, &VM::iter_step_pair_array,
                       &VM::iter_step_pair_dict, &VM::iter_step_pair_string);
}

// ─────────── Output & Debugging ───────────

void VM::handle_print() {
    auto arg_count = read_byte();

    auto args = pop_sequence(static_cast<std::size_t>(arg_count));

    // Single-pass: convert each argument to string and append directly.
    std::string output;

    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            output += ' ';
        }
        output += args[i].to_string();
    }

    std::cout << output << "\n";
    push(Value{});
}

void VM::handle_assert() {
    auto arg_count = read_byte();

    if (arg_count >= 1) {
        std::string message = "Assertion failed";

        // Stack order: the compiler pushes `condition` first, then `message`.
        // `message` is therefore on top of the stack (popped first).
        if (arg_count >= 2) {
            message = pop().to_string();
        }

        auto condition = pop();

        if (!condition.is_truthy()) {
            runtime_error(message);
        }
    }

    push(Value{});
}

} // namespace luma
