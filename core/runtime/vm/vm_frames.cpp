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
    retained_function_tables_ = std::move(other.retained_function_tables_);
    loop_iterations_ = other.loop_iterations_;
    base_depth_ = other.base_depth_;
    cancel_token_ = std::move(other.cancel_token_);
    is_task_vm_ = other.is_task_vm_;
    task_snapshot_layers_ = std::move(other.task_snapshot_layers_);
    globals_version_ = other.globals_version_;
    exception_handler_ = std::move(other.exception_handler_);
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

// ─────────── Task global environment ───────────

// Spawned tasks must not share the parent's live global Environment: its
// binding map and values are unsynchronised, so a global written by the parent
// (or by a task) while another thread reads it is a data race.  Each task
// instead receives a deep-copied snapshot (Architecture §12.5), shared
// read-only by every task spawned while this VM's globals are unchanged.
// Tasks never write their snapshot (see reject_global_write_in_task), so
// sharing it is safe.
//
// Each scope of the global chain (normally just the root; embedders such as
// the test harness layer the program's globals over a shared stdlib scope)
// becomes one snapshot layer of two environments, so re-snapshotting stays
// cheap: the scope's immutable bindings (the stdlib — large, but never
// reassigned) form a base environment that is rebuilt only when the scope
// grows, and its mutable bindings (every user global: SetGlobal defines
// globals as mutable, since the bytecode does not record declared
// mutability) a top environment that is rebuilt after this VM writes a global
// (globals_version_).  A rebuild deep-copies only the bindings written since
// their cached copy was made (Binding::write_version); every other binding —
// a function, a large array that is never written — shares its earlier copy.
EnvPtr VM::spawn_environment() {
    if (is_task_vm_) {
        // Already an immutable snapshot — nested spawns can share it.
        return global_env_;
    }

    std::vector<const Environment*> chain;

    for (const auto* scope = global_env_.get(); scope != nullptr; scope = scope->parent().get()) {
        chain.push_back(scope);
    }

    std::ranges::reverse(chain); // Root first: each layer is the parent of the next.

    if (task_snapshot_layers_.size() != chain.size()) {
        task_snapshot_layers_.clear();
        task_snapshot_layers_.resize(chain.size());
    }

    EnvPtr parent;
    bool parent_rebuilt = false;

    for (std::size_t i{0}; i < chain.size(); ++i) {
        const auto& scope = *chain[i];
        auto& layer = task_snapshot_layers_[i];
        const auto binding_count = scope.local_binding_count();

        if (parent_rebuilt || !layer.base || layer.source != &scope ||
            layer.binding_count != binding_count) {
            layer.base = scope.copy_bindings(parent, /*mutable_bindings=*/false, layer.copies);
            layer.source = &scope;
            layer.binding_count = binding_count;
            layer.has_mutable_bindings = scope.has_mutable_bindings();
            layer.top.reset();
        }

        if (!layer.top || layer.version != globals_version_) {
            auto top =
                layer.has_mutable_bindings
                    ? scope.copy_bindings(layer.base, /*mutable_bindings=*/true, layer.copies)
                    : layer.base;
            parent_rebuilt = top != layer.top;
            layer.top = std::move(top);
            layer.version = globals_version_;
        } else {
            parent_rebuilt = false;
        }

        parent = layer.top;
    }

    return parent;
}

void VM::reject_global_write_in_task(std::string_view name) const {
    if (is_task_vm_) [[unlikely]] {
        runtime_error(vm_errors::cannot_assign_global_in_task(name),
                      vm_errors::hint_global_write_in_task);
    }
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

    validate_call_depth();

    stack_.frames.push_back({
        .function = func->compiled,
        .closure = func,
        .ip = func->compiled->chunk().code.data(),
        .code_end = func->compiled->chunk().code.data() + func->compiled->chunk().code.size(),
        .slot_offset = call_frame_base_slot(arg_count),
    });

    return true;
}

} // namespace luma
