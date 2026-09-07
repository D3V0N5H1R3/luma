#include "vm_hook_registry.hpp"

#include <format>
#include <mutex>

#include "breakpoint_manager.hpp"
#include "dap_helpers.hpp"
#include "dap_response_builders.hpp"
#include "debug_execution_engine.hpp"
#include "runtime/vm/vm.hpp"
#include "thread_state_manager.hpp"

namespace luma::dap {

namespace {

auto make_line_hook(HookInstallationContext ctx) {
    return [ctx](int file_id, int line, std::size_t depth) {
        return ctx.execution_engine->should_break(file_id, line, depth);
    };
}

auto make_pause_hook(HookInstallationContext ctx) {
    return [ctx]() -> bool {
        return ctx.execution_engine->wait_for_resume();
    };
}

auto make_exception_hook(HookInstallationContext ctx) {
    return [ctx](const std::string& msg, bool is_caught) -> bool {
        return ctx.execution_engine->on_exception(msg, is_caught);
    };
}

auto make_task_spawn_hook(HookInstallationContext ctx) {
    return [ctx](VM& task_vm, int task_id) {
        auto task_state = std::make_shared<ThreadState>();
        task_state->thread_id = task_id;
        task_state->name = std::format("Task {}", task_id);
        task_state->vm = &task_vm;

        ctx.thread_state_manager->add_thread(task_state);

        tl_debug_thread_id = task_id;

        ctx.event_callback(std::string{kEventThread},
                           make_thread_event_body(kThreadReasonStarted, task_id));
    };
}

auto make_task_exit_hook(HookInstallationContext ctx) {
    return [ctx](int task_id) {
        ctx.thread_state_manager->remove_thread(task_id);

        ctx.event_callback(std::string{kEventThread},
                           make_thread_event_body(kThreadReasonExited, task_id));
    };
}

} // namespace

void install_debug_hooks(VM& vm, const HookInstallationContext& ctx) {
    vm.set_debug_hook(make_line_hook(ctx));
    vm.set_pause_callback(make_pause_hook(ctx));
    vm.set_exception_hook(make_exception_hook(ctx));
    vm.set_task_spawn_hook(make_task_spawn_hook(ctx));
    vm.set_task_exit_hook(make_task_exit_hook(ctx));
}

} // namespace luma::dap
