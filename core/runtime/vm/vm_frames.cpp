#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include "common/resource_limits.hpp"
#include "runtime/concurrency/thread_pool.hpp"
#include "runtime/interpreter/environment.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_error_messages.hpp"

namespace luma {

// ─────────── Constructor ───────────

VM::VM(EnvPtr global_env) : global_env_{std::move(global_env)} {
    stack_.frames.reserve(
        std::max(VMStack::k_frame_max, static_cast<std::size_t>(ResourceLimits::max_call_depth)));
    init_call_fn();
}

// Moving the underlying containers is noexcept; the only escape the analyzer
// can trace is MSVC STL bad_alloc, which a move operation never triggers.
VM::VM(VM&& other) noexcept : stack_{std::move(other.stack_)} { // NOLINT(bugprone-exception-escape)
    // stack_ is initialized above via the member initializer list to avoid
    // default-constructing a k_max-element heap allocation that move_from()
    // would immediately replace.  All other fields are transferred here.
    const std::unique_lock lock(other.debug_.callbacks_mutex);
    transfer_state(std::move(other));
    init_call_fn();
}

VM& VM::operator=(VM&& other) noexcept {
    if (this != &other) {
        move_from(std::move(other));
    }

    return *this;
}

void VM::move_from(VM&& other) noexcept {
    stack_ = std::move(other.stack_);
    const std::scoped_lock lock(debug_.callbacks_mutex, other.debug_.callbacks_mutex);
    transfer_state(std::move(other));
    init_call_fn();
}

// Transfers all movable state fields except stack_.
// The caller is responsible for holding the appropriate mutex locks on
// debug_.callbacks_mutex before calling (see declaration in vm.hpp).
void VM::transfer_state(VM&& other) noexcept {
    global_env_ = std::move(other.global_env_);
    compiled_functions_ = std::exchange(other.compiled_functions_, nullptr);
    loop_iterations_ = other.loop_iterations_;
    base_depth_ = other.base_depth_;
    exceptions_ = std::move(other.exceptions_);
    global_cache_ = std::move(other.global_cache_);
    global_index_cache_ = std::move(other.global_index_cache_);

    // Debug context — shared_mutex is non-movable; transfer data members only.
    debug_.callbacks = std::move(other.debug_.callbacks);
    debug_.last_line = other.debug_.last_line;
    debug_.last_file = other.debug_.last_file;
    debug_.pause_requested.store(other.debug_.pause_requested.load());

    // Task manager — atomic next_task_id is non-movable, so transfer fields.
    // The static thread-local current_scope is thread-global and not moved.
    task_manager_.task_scopes = std::move(other.task_manager_.task_scopes);
    task_manager_.owned_pool = std::move(other.task_manager_.owned_pool);
    task_manager_.shared_pool = std::exchange(other.task_manager_.shared_pool, nullptr);
    task_manager_.next_task_id.store(other.task_manager_.next_task_id.load());
}

// ─────────── Inline cache for global variables ───────────

Binding* VM::lookup_global_cache(std::string_view name) {
    return global_cache_.lookup(name, *global_env_);
}

// Lazily resolve (and cache on the frame) the per-function global binding
// vector, returning a reference to the slot for name handle `idx`.  The vector
// is sized to the chunk's name table so the caller's bounds-checked index is
// always in range.  See CallFrame::global_bindings and global_index_cache_.
Binding*& VM::global_slot(std::uint16_t idx) {
    auto& cf = stack_.frames.back();

    if (cf.global_bindings == nullptr) [[unlikely]] {
        auto& bindings = global_index_cache_[cf.function];
        const auto name_count = cf.function->chunk().names.size();
        if (bindings.size() < name_count) {
            bindings.assign(name_count, nullptr);
        }
        cf.global_bindings = &bindings;
    }

    return (*cf.global_bindings)[idx];
}

void VM::install_compiled_functions(const std::vector<CompiledFunction>* functions) {
    compiled_functions_ = functions;
    // Drop the pointer-keyed global cache: its CompiledFunction* keys refer to
    // the previous store, and a replacement store may reuse those freed
    // addresses.  Safe here because a new store is only installed at top-level
    // entry, before any frame that could reference the cache is pushed.
    global_index_cache_.clear();
}

// ─────────── Frame management ───────────

bool VM::call_closure(FunctionValue* func, std::uint8_t arg_count) {
    if ((func == nullptr) || (func->compiled == nullptr)) [[unlikely]] {
        runtime_error(vm_errors::nil_function_call, vm_errors::hint_nil_function);
        return false;
    }

    if (stack_.frames.size() >= static_cast<std::size_t>(ResourceLimits::max_call_depth))
        [[unlikely]] {
        runtime_error(vm_errors::call_stack_overflow_recursion(ResourceLimits::max_call_depth),
                      vm_errors::hint_check_recursion);
        return false;
    }

    if (stack_.frames.size() >= VMStack::k_frame_max) [[unlikely]] {
        runtime_error(vm_errors::call_frame_overflow(func->compiled->name, VMStack::k_frame_max),
                      vm_errors::hint_deep_recursion);
        return false;
    }

    stack_.frames.push_back({
        .function = func->compiled,
        .closure = func,
        .ip = func->compiled->chunk().code.data(),
        .slot_offset = call_frame_base_slot(arg_count),
    });

    return true;
}

} // namespace luma
