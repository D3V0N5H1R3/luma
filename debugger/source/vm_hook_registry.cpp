#include "vm_hook_registry.hpp"

#include <format>
#include <mutex>

#include "breakpoint_manager.hpp"
#include "dap_helpers.hpp"
#include "dap_response_builders.hpp"
#include "debug_execution_engine.hpp"
#include "expression_evaluator.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_introspection.hpp"
#include "thread_state_manager.hpp"
#include "time_travel.hpp"

namespace luma::dap {

namespace {

auto make_line_hook(HookInstallationContext ctx) {
    return [ctx](int file_id, int line, std::size_t depth) {
        if (*ctx.time_travel_recorder) {
            // Record against the VM executing on *this* thread, not a captured
            // reference. The hook is propagated into child task VMs, so a
            // captured VM& would let a task worker thread snapshot the main
            // VM's stack concurrently — a data race the recorder mutex does not
            // cover. Resolving via current_thread() mirrors the data-breakpoint
            // hook and keeps each thread recording its own stack.
            auto state = ctx.thread_state_manager->current_thread();
            if (state) {
                const auto lock = ctx.thread_state_manager->lock_state(*state);
                if (state->vm != nullptr) {
                    (*ctx.time_travel_recorder)->on_line(*state->vm, file_id, line, depth);
                }
            }
        }
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

auto make_data_breakpoint_hook(const HookInstallationContext& ctx) {
    return [ctx](const std::string& name) -> bool {
        auto eval_condition = [ctx](const std::string& condition) -> std::string {
            auto state = ctx.thread_state_manager->current_thread();

            if (!state || !state->vm) {
                return "";
            }

            const VMIntrospector intro(*state->vm);
            const int top_frame = top_frame_index(intro.frame_count());

            try {
                auto result = ctx.expression_evaluator->evaluate(state->vm, top_frame, condition);
                return result.value;
            } catch (...) {
                // Evaluation failure → treat condition as unmet (don't break).
                return "";
            }
        };

        if (ctx.breakpoint_manager->check_data_breakpoint(name, eval_condition)) {
            auto state = ctx.thread_state_manager->current_thread();
            if (state) {
                const auto lock = ctx.thread_state_manager->lock_state(*state);
                state->pending.data_breakpoint = true;
                state->pending.data_breakpoint_name = name;
            }
            return true;
        }
        return false;
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
    vm.set_data_breakpoint_hook(make_data_breakpoint_hook(ctx));
    vm.set_task_spawn_hook(make_task_spawn_hook(ctx));
    vm.set_task_exit_hook(make_task_exit_hook(ctx));
}

} // namespace luma::dap
