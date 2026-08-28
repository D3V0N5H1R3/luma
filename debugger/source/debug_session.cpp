#include "debug_session.hpp"

#include <filesystem>
#include <limits>

#include "analysis/source/source_manager.hpp"
#include "dap_error_handler.hpp"
#include "debug_execution_engine.hpp"
#include "expression_evaluator.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_introspection.hpp"
#include "time_travel.hpp"
#include "vm_hook_registry.hpp"

namespace luma::dap {

// --- Lifecycle ---

DebugSession::DebugSession(EventCallback event_cb, OutputCallback output_cb)
    : event_callback_(std::move(event_cb)),
      output_callback_(std::move(output_cb)),
      variable_inspector_(event_callback_) {
    initialize_components();
}

void DebugSession::initialize_components() {
    expression_evaluator_ =
        std::make_unique<ExpressionEvaluator>([this](std::shared_ptr<Value> val) {
            return variable_inspector_.alloc_ref(ValueRef{.value = std::move(val)});
        });

    execution_engine_ = std::make_unique<DebugExecutionEngine>(
        SessionComponents{.session = *this,
                          .thread_mgr = thread_state_manager_,
                          .bp_mgr = breakpoint_manager_,
                          .var_inspector = variable_inspector_,
                          .expr_eval = *expression_evaluator_},
        ExecutionCallbacks{.event_cb = event_callback_, .output_cb = output_callback_});
}

DebugSession::~DebugSession() noexcept {
    execution_engine_->terminate();
}

// --- Thread resolver ---

VariableInspector::ThreadResolver DebugSession::make_thread_resolver() const {
    return [this](int thread_id) -> std::shared_ptr<ThreadState> {
        return thread_state_manager_.get_thread(thread_id);
    };
}

// --- Lifecycle delegation ---

std::string DebugSession::launch(const std::string& program_path, const DebugSessionConfig& config,
                                 const std::vector<std::string>& args, const std::string& cwd) {
    if (config.time_travel) {
        enable_time_travel();
    }

    return execution_engine_->launch(program_path, config.stop_on_entry, args, cwd,
                                     config.no_debug);
}

void DebugSession::configuration_done() {
    execution_engine_->configuration_done();
}

void DebugSession::terminate() {
    execution_engine_->terminate();
}

// --- Breakpoint delegation ---

std::vector<Breakpoint>
DebugSession::set_breakpoints(const std::string& path,
                              const std::vector<BreakpointRequest>& breakpoints) {
    auto result = breakpoint_manager_.set_breakpoints(path, breakpoints);
    thread_state_manager_.signal_all_vms_pause_check();
    return result;
}

void DebugSession::set_exception_breakpoints(const std::vector<std::string>& filters) {
    breakpoint_manager_.set_exception_breakpoints(filters);
}

std::vector<Breakpoint>
DebugSession::set_function_breakpoints(const std::vector<BreakpointRequest>& breakpoints) {
    auto result = breakpoint_manager_.set_function_breakpoints(breakpoints);
    thread_state_manager_.signal_all_vms_pause_check();
    return result;
}

void DebugSession::set_data_breakpoint(const std::string& variable_name,
                                       const std::string& access_type,
                                       const std::string& condition) {
    breakpoint_manager_.set_data_breakpoint(variable_name, access_type, condition);
}

void DebugSession::clear_data_breakpoints() {
    breakpoint_manager_.clear_data_breakpoints();
}

// --- Execution control delegation ---

ExecutionResult DebugSession::continue_execution(int thread_id) {
    step_back_cursor_ = 0;
    return execution_engine_->continue_execution(thread_id);
}

ExecutionResult DebugSession::step_over(int thread_id) {
    step_back_cursor_ = 0;
    return execution_engine_->step_over(thread_id);
}

ExecutionResult DebugSession::step_into(int thread_id) {
    step_back_cursor_ = 0;
    return execution_engine_->step_into(thread_id);
}

ExecutionResult DebugSession::step_out(int thread_id) {
    step_back_cursor_ = 0;
    return execution_engine_->step_out(thread_id);
}

ExecutionResult DebugSession::pause(int thread_id) {
    return execution_engine_->pause(thread_id);
}

// --- Time-travel debugging ---

void DebugSession::enable_time_travel() {
    enable_time_travel(TimeTravelConfig{});
}

void DebugSession::enable_time_travel(TimeTravelConfig config) {
    time_travel_recorder_ = std::make_unique<TimeTravelRecorder>(config);
}

ExecutionResult DebugSession::restore_from_snapshot(int thread_id, std::size_t steps_back,
                                                    bool clamp_to_front) {
    if (!time_travel_recorder_) {
        return ExecutionResult::error(
            "Time-travel debugging is not enabled. Set \"timeTravel\": true in your launch "
            "configuration.");
    }

    const auto snapshot = time_travel_recorder_->step_back(steps_back, clamp_to_front);

    if (!snapshot) {
        return ExecutionResult::error("No previous state available");
    }

    auto state = thread_state_manager_.get_thread(thread_id);

    if (!state) {
        return ExecutionResult::error(error_messages::unknown_thread_id(thread_id));
    }

    {
        const auto lock = thread_state_manager_.lock_state(*state);

        if (state->vm == nullptr) {
            return ExecutionResult::error("Thread has no active VM");
        }

        ReplayEngine::restore_snapshot(*state->vm, *snapshot);
    }

    // Restoring overwrote the VM value stack, so cached variable references and
    // frame mappings are now stale.  Flush them (and emit `invalidated`) outside
    // the per-thread lock, mirroring the forward resume path
    // (prepare_for_execution_resume) which invalidates before touching threads.
    variable_inspector_.invalidate_refs();
    return ExecutionResult::ok();
}

ExecutionResult DebugSession::step_back(int thread_id) {
    // Walk one snapshot further back than the previous step_back.  The cursor
    // is reset to 0 by any forward resume, so a fresh stop starts from the
    // latest snapshot again.
    const std::size_t steps = step_back_cursor_ + 1;
    auto result = restore_from_snapshot(thread_id, steps);

    if (result) {
        step_back_cursor_ = steps;
    }

    return result;
}

ExecutionResult DebugSession::reverse_continue(int thread_id) {
    // Rewind to the earliest retained snapshot (start of recorded history).
    // clamp_to_front=true: reaching the start of history is success, not an
    // error, unlike a plain step_back overshoot.
    auto result = restore_from_snapshot(thread_id, std::numeric_limits<std::size_t>::max(), true);

    if (result && time_travel_recorder_) {
        // Pin the cursor beyond the oldest snapshot so a following step_back
        // stays clamped at the front rather than jumping forward.
        step_back_cursor_ = time_travel_recorder_->snapshot_count();
    }

    return result;
}

const TimeTravelRecorder* DebugSession::time_travel() const {
    return time_travel_recorder_.get();
}

HookInstallationContext DebugSession::make_hook_context() {
    return HookInstallationContext{.time_travel_recorder = &time_travel_recorder_,
                                   .execution_engine = execution_engine_.get(),
                                   .thread_state_manager = &thread_state_manager_,
                                   .breakpoint_manager = &breakpoint_manager_,
                                   .expression_evaluator = expression_evaluator_.get(),
                                   .event_callback = event_callback_};
}

// --- State inspection ---

std::vector<StackFrame> DebugSession::get_stack_trace(int thread_id) const {
    auto state = thread_state_manager_.get_thread(thread_id);

    if (!state) {
        return {};
    }

    const auto lock = thread_state_manager_.lock_state(*state);
    std::vector<StackFrame> result;

    // DAP semantics: only a STOPPED thread has a stable stack to report.  A
    // free-running task VM concurrently mutates its call-frame vector (growth
    // reallocation, pops on OP_RETURN, FunctionValue teardown on task exit), so
    // reading it here would race the execution thread and can crash the adapter
    // (SIGSEGV).  Report no frames for a thread that is not paused; is_paused is
    // guarded by the ThreadState lock acquired above.
    if (!state->is_paused) {
        return result;
    }

    VM* target_vm = state->vm;

    if (target_vm == nullptr) {
        return result;
    }

    const VMIntrospector intro(*target_vm);
    auto locations = intro.stack_trace();

    if (locations.empty() || intro.frame_count() == 0) {
        return result;
    }

    auto* sm = execution_engine_->source_manager();

    for (int i = 0; i < static_cast<int>(locations.size()); ++i) {
        const auto& loc = locations[static_cast<std::size_t>(i)];

        const int vm_frame_index = static_cast<int>(intro.frame_count()) - 1 - i;

        const int frame_id =
            variable_inspector_.register_frame(thread_id, vm_frame_index, target_vm);

        StackFrame frame;
        frame.id = frame_id;
        frame.name = loc.function_name.empty() ? "<top-level>" : loc.function_name;
        frame.line = loc.line;
        frame.column = loc.column;

        if (loc.function_name.empty()) {
            frame.presentation_hint = "subtle";
        }

        if (sm != nullptr) {
            const auto* file = sm->get_file(loc.file_id);

            if (file != nullptr) {
                frame.source = make_source(*file);
            }
        }

        result.push_back(std::move(frame));
    }

    return result;
}

std::vector<Scope> DebugSession::get_scopes(int frame_id) const {
    return variable_inspector_.get_scopes(frame_id, make_thread_resolver());
}

std::vector<Variable> DebugSession::get_variables(int reference, int start, int count,
                                                  const std::string& filter) const {
    return variable_inspector_.get_variables(reference, start, count, filter,
                                             make_thread_resolver());
}

std::pair<int, int> DebugSession::get_variable_counts(int reference) const {
    return variable_inspector_.get_variable_counts(reference, make_thread_resolver());
}

Variable DebugSession::evaluate(int frame_id, const std::string& expression,
                                EvaluationContext context) const {
    auto mapping = variable_inspector_.resolve_frame(frame_id);

    VM* target_vm = nullptr;
    int actual_index = frame_id;

    if (mapping) {
        auto state = thread_state_manager_.get_thread(mapping->thread_id);

        if (state) {
            // Hold the lock for the entire evaluate() call so that target_vm
            // cannot be destroyed between capture and use (TOCTOU race).
            const auto lock = thread_state_manager_.lock_state(*state);
            target_vm = state->vm;
            actual_index = mapping->frame_index;
            return expression_evaluator_->evaluate(target_vm, actual_index, expression, context);
        }

        actual_index = mapping->frame_index;
    } else {
        // No frame mapping (e.g. a stale or zero frame_id): fall back to the
        // main thread. Lock the main thread's state for the same reason as
        // the mapping-hit path above — reading state->vm under its lock
        // ensures target_vm cannot be destroyed between capture and use
        // (TOCTOU race), since terminate() nulls it under this same lock
        // (ThreadStateManager::null_all_vms) before destroying the VM.
        auto main_state = thread_state_manager_.get_thread(k_main_thread_id);

        if (main_state) {
            const auto lock = thread_state_manager_.lock_state(*main_state);
            target_vm = main_state->vm;
            return expression_evaluator_->evaluate(target_vm, actual_index, expression, context);
        }
    }

    return expression_evaluator_->evaluate(target_vm, actual_index, expression, context);
}

Variable DebugSession::set_variable(int variables_reference, const std::string& name,
                                    const std::string& value) const {
    return variable_inspector_.set_variable(variables_reference, name, value,
                                            make_thread_resolver());
}

std::vector<std::pair<std::string, std::string>>
DebugSession::get_completions(int frame_id, const std::string& text) const {
    return variable_inspector_.get_completions(frame_id, text, make_thread_resolver());
}

std::vector<int> DebugSession::get_breakpoint_locations(const std::string& path, int start_line,
                                                        int end_line) const {
    return breakpoint_manager_.get_breakpoint_locations(path, start_line, end_line);
}

// --- Thread listing ---

std::vector<std::pair<int, std::string>> DebugSession::get_threads() const {
    return thread_state_manager_.get_threads();
}

bool DebugSession::is_thread_valid(int thread_id) const {
    return thread_state_manager_.is_thread_valid(thread_id);
}

// --- Query ---

bool DebugSession::is_running() const {
    return execution_engine_->is_running();
}

int DebugSession::check_for_source_changes() {
    return execution_engine_->check_for_source_changes();
}

std::string DebugSession::last_exception_message() const {
    return execution_engine_->last_exception_message();
}

bool DebugSession::last_exception_is_caught() const {
    return execution_engine_->last_exception_is_caught();
}

// --- Source helpers ---

Source DebugSession::make_source(const SourceFile& file) {
    Source src;
    src.path = std::filesystem::absolute(file.path).string();
    src.name = std::filesystem::path(file.path).filename().string();
    return src;
}

void DebugSession::for_each_source_file(const std::function<bool(const SourceFile&)>& fn) const {
    auto* sm = execution_engine_->source_manager();

    if (sm == nullptr) {
        return;
    }

    for (int fid = 1;; ++fid) {
        const auto* file = sm->get_file(fid);

        if (file == nullptr) {
            break;
        }

        if (!fn(*file)) {
            break;
        }
    }
}

std::string DebugSession::get_source_content(const std::string& path) const {
    const auto abs_path = std::filesystem::absolute(path).string();
    std::error_code ec;
    const auto canonical_target = std::filesystem::weakly_canonical(abs_path, ec);

    std::string content;

    for_each_source_file([&](const SourceFile& file) {
        std::error_code ec2;
        const auto canonical_file = std::filesystem::weakly_canonical(file.path, ec2);

        if (!ec && !ec2 && canonical_file == canonical_target) {
            content = file.text;
            return false;
        }

        return true;
    });

    return content;
}

// --- Loaded sources ---

std::vector<Source> DebugSession::get_loaded_sources() const {
    std::vector<Source> result;

    for_each_source_file([&](const SourceFile& file) {
        result.push_back(make_source(file));
        return true;
    });

    return result;
}

} // namespace luma::dap
