// vm_debug.cpp — VM debug hook infrastructure.
//
// Implements all debugger-integration methods: hook setters, the
// check_debug_hooks() line-change check, and data-breakpoint
// validation helpers used by the set-local and set-global opcodes.
//
// Thread safety: debug_.callbacks is protected by debug_.callbacks_mutex.
// Setters take a unique_lock (exclusive). Readers take a shared_lock,
// copy the callback, then release the lock before invoking it to avoid
// holding the mutex during potentially blocking callback execution.

#include <optional>
#include <shared_mutex>
#include <string>

#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_debug_types.hpp"

namespace luma {

// ─────────── Debug hook setters ───────────
//
// Callback storage and its locking discipline live in VMDebugInterface
// (see vm_debug_interface.hpp); these methods simply delegate to it.

void VM::set_debug_callbacks(DebugCallbacks callbacks) {
    debug_.set_all(std::move(callbacks));
}

void VM::set_debug_hook(DebugHook hook) {
    debug_.set_callback(&DebugCallbacks::debug_hook, std::move(hook));
}

void VM::set_pause_callback(PauseCallback callback) {
    debug_.set_callback(&DebugCallbacks::pause_callback, std::move(callback));
}

void VM::set_exception_hook(ExceptionHook hook) {
    debug_.set_callback(&DebugCallbacks::exception_hook, std::move(hook));
}

void VM::set_data_breakpoint_hook(DataBreakpointHook hook) {
    debug_.set_callback(&DebugCallbacks::data_breakpoint_hook, std::move(hook));
}

void VM::set_task_spawn_hook(TaskSpawnHook hook) {
    debug_.set_callback(&DebugCallbacks::task_spawn_hook, std::move(hook));
}

void VM::set_task_exit_hook(TaskExitHook hook) {
    debug_.set_callback(&DebugCallbacks::task_exit_hook, std::move(hook));
}

// ─────────── Line-change hook ───────────

// Threading: last_line/last_file are only accessed by the VM's owning thread.
// The atomic pause_requested is set by the debugger thread; all other fields
// here are thread-local to the VM.
// Callbacks are copied under a shared_lock and invoked after releasing
// the lock to avoid holding the mutex during potentially blocking calls.
bool VM::check_debug_hooks() {
    // Fast path: gate on the cheap pause_requested atomic BEFORE taking the
    // shared_lock and copying the std::function hook.  This runs once per
    // opcode, so on the overwhelmingly common no-pause path (no debugger, or a
    // debugger attached but not stepping) it must cost a single atomic load —
    // not a shared_mutex acquire plus a std::function copy.  request_pause_check()
    // is the sole per-line arming path, so debugging still fires the hook
    // whenever pause_requested is set; when it is clear the hook never runs, so
    // testing it first is behaviour-preserving.
    if (!debug_.pause_requested.load(std::memory_order_acquire)) {
        return false;
    }

    // THREAD_SAFETY: snapshot captured under shared_lock; safe to use across threads.
    auto debug_hook_copy = debug_.copy_hook(&DebugCallbacks::debug_hook);
    if (!debug_hook_copy) {
        return false;
    }

    auto loc = current_location();

    if (loc.line == debug_.last_line && loc.file_id == debug_.last_file) {
        return false;
    }

    debug_.last_line = loc.line;
    debug_.last_file = loc.file_id;

    if (!debug_hook_copy(loc.file_id, loc.line, stack_.frames.size())) {
        return false;
    }

    debug_.pause_requested.store(false, std::memory_order_release);

    // THREAD_SAFETY: snapshot captured under shared_lock; safe to use across threads.
    auto pause_copy = debug_.copy_hook(&DebugCallbacks::pause_callback);
    if (pause_copy) {
        if (!pause_copy()) {
            return true; // Debugger requested termination.
        }
    }

    return false;
}

// ─────────── Data-breakpoint notifications ───────────

// Shared implementation: copies the data-breakpoint hook under a shared lock,
// invokes the name provider to obtain the variable name, calls the hook, and
// requests a pause if the hook returns true.
template <typename NameProvider> void VM::notify_data_breakpoint_impl(NameProvider name_provider) {
    // THREAD_SAFETY: snapshot captured under shared_lock; safe to use across threads.
    auto hook_copy = debug_.copy_hook(&DebugCallbacks::data_breakpoint_hook);
    if (!hook_copy) {
        return;
    }
    auto name = name_provider();
    if (!name) {
        return;
    }
    if (hook_copy(*name)) {
        debug_.last_line = -1;
        debug_.pause_requested.store(true, std::memory_order_release);
    }
}

void VM::notify_local_data_breakpoint(const CallFrame& cf, std::uint16_t slot) {
    notify_data_breakpoint_impl([&]() -> std::optional<std::string> {
        if (slot >= cf.function->debug_info.local_names.size()) {
            return std::nullopt;
        }
        const auto& vname = cf.function->debug_info.local_names[slot];
        if (vname.empty()) {
            return std::nullopt;
        }
        return std::string{vname};
    });
}

void VM::notify_global_data_breakpoint(std::string_view name) {
    notify_data_breakpoint_impl([&]() -> std::optional<std::string> { return std::string{name}; });
}

} // namespace luma
