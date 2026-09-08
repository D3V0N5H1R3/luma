#include <optional>

#include "dap_execution_handler.hpp"
#include "dap_helpers.hpp"
#include "dap_response_builders.hpp"
#include "dap_types.hpp"
#include "debug_session.hpp"

namespace luma::dap {

// ═══════════════════════════════════════════════════════════
// DapExecutionHandler
// ═══════════════════════════════════════════════════════════

// ⚠ CONTRACT: The execution engine MUST emit a "stopped" event via the
// session's event callback once stepping/continuing completes.  The stopped
// event is NOT emitted here — it is the execution engine's responsibility.
// See: DebugSession event callback set up in launch_with_config().
// Missing this event causes the DAP client to hang waiting for "stopped".
HandlerResult DapExecutionHandler::execute_thread_action(const JsonValue& args,
                                                         ExecutionAction action) {
    auto& session = require_session(ctx_.session);

    const int thread_id = extract_thread_id(args);
    const bool all_threads = (action == ExecutionAction::Continue && thread_id == 0);

    // Emit continued event BEFORE resuming to prevent ordering race:
    // if the thread hits a breakpoint immediately, the stopped event
    // must arrive after the continued event.
    ctx_.protocol_handler.send_event(std::string{kEventContinued},
                                     make_continued_event_body(thread_id, all_threads));

    ctx_.invalidate_watches();

    auto try_execute = [](auto action_fn) -> std::optional<HandlerResult> {
        auto result = action_fn();
        if (!result) {
            return HandlerResult::error(result.error_message);
        }
        return std::nullopt;
    };

    std::optional<HandlerResult> err;
    switch (action) {
        case ExecutionAction::Continue:
            err = try_execute([&] { return session.continue_execution(thread_id); });
            break;
        case ExecutionAction::StepOver:
            err = try_execute([&] { return session.step_over(thread_id); });
            break;
        case ExecutionAction::StepIn:
            err = try_execute([&] { return session.step_into(thread_id); });
            break;
        case ExecutionAction::StepOut:
            err = try_execute([&] { return session.step_out(thread_id); });
            break;
    }

    if (err) {
        return *err;
    }

    if (action == ExecutionAction::Continue) {
        JsonValue::ObjectType body;
        body["allThreadsContinued"] = JsonValue(all_threads);
        return HandlerResult::ok(JsonValue(std::move(body)));
    }

    return HandlerResult::ok();
}

HandlerResult DapExecutionHandler::handle_continue(const JsonValue& args) {
    return execute_thread_action(args, ExecutionAction::Continue);
}

HandlerResult DapExecutionHandler::handle_next(const JsonValue& args) {
    return execute_thread_action(args, ExecutionAction::StepOver);
}

HandlerResult DapExecutionHandler::handle_step_in(const JsonValue& args) {
    return execute_thread_action(args, ExecutionAction::StepIn);
}

HandlerResult DapExecutionHandler::handle_step_out(const JsonValue& args) {
    return execute_thread_action(args, ExecutionAction::StepOut);
}

HandlerResult DapExecutionHandler::handle_pause(const JsonValue& args) {
    auto& session = require_session(ctx_.session);

    const int thread_id = extract_thread_id(args);
    auto result = session.pause(thread_id);

    if (!result) {
        return HandlerResult::error(result.error_message);
    }

    return HandlerResult::ok();
}

// ─── Custom extensions ───

HandlerResult DapExecutionHandler::handle_concurrency_state() {
    JsonValue::ObjectType body;
    JsonValue::ArrayType tasks;
    JsonValue::ArrayType channels;

    if (ctx_.has_session()) {
        // Get thread list — each thread beyond thread 1 is a task.
        auto threads = ctx_.session->get_threads();
        for (const auto& [tid, name] : threads) {
            JsonValue::ObjectType task;
            task["id"] = JsonValue(tid);
            task["name"] = JsonValue(name);
            task["state"] = JsonValue(std::string("running"));
            tasks.emplace_back(std::move(task));
        }
    }

    body["tasks"] = JsonValue(std::move(tasks));
    body["channels"] = JsonValue(std::move(channels));
    return HandlerResult::ok(JsonValue(std::move(body)));
}

} // namespace luma::dap
