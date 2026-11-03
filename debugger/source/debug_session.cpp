#include "debug_session.hpp"

#include <filesystem>

#include "analysis/source/source_manager.hpp"
#include "dap_error_handler.hpp"
#include "debug_execution_engine.hpp"
#include "debugger_messages.hpp"
#include "diagnostic_log.hpp"
#include "expression_evaluator.hpp"
#include "runtime/vm/vm.hpp"
#include "runtime/vm/vm_introspection.hpp"
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
    expression_evaluator_ = std::make_unique<ExpressionEvaluator>(
        [this](std::shared_ptr<Value> val, std::string evaluate_name) {
            return variable_inspector_.alloc_ref(ValueRef{
                .value = std::move(val), .depth = 0, .evaluate_name = std::move(evaluate_name)});
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
    // The engine's own destructor terminates too; doing it here first stops the
    // execution thread while every other component is still alive.
    try {
        execution_engine_->terminate();
    } catch (const std::exception& e) {
        write_adapter_diagnostic(std::string{"DAP: error terminating session: "} + e.what());
    }
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
    return execution_engine_->launch(program_path, config.stop_on_entry, args, cwd,
                                     config.no_debug);
}

void DebugSession::configuration_done() {
    execution_engine_->configuration_done();
}

void DebugSession::terminate(bool emit_exit_events) {
    execution_engine_->terminate(emit_exit_events);
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

// --- Execution control delegation ---

ExecutionResult DebugSession::continue_execution(int thread_id) {
    return execution_engine_->continue_execution(thread_id);
}

ExecutionResult DebugSession::step_over(int thread_id) {
    return execution_engine_->step_over(thread_id);
}

ExecutionResult DebugSession::step_into(int thread_id) {
    return execution_engine_->step_into(thread_id);
}

ExecutionResult DebugSession::step_out(int thread_id) {
    return execution_engine_->step_out(thread_id);
}

ExecutionResult DebugSession::pause(int thread_id) {
    return execution_engine_->pause(thread_id);
}

HookInstallationContext DebugSession::make_hook_context() {
    return HookInstallationContext{.execution_engine = execution_engine_.get(),
                                   .thread_state_manager = &thread_state_manager_,
                                   .breakpoint_manager = &breakpoint_manager_,
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
    const auto evaluation_failed = [&expression] {
        Variable result;
        result.name = expression;
        result.value = std::string{messages::expression::evaluation_failed};
        result.type = "unknown";
        return result;
    };

    std::shared_ptr<ThreadState> state;

    // Out-of-range index: the evaluator then uses the innermost frame.
    int frame_index = -1;

    if (frame_id == 0) {
        // DAP: no frameId means "global scope" — evaluate against the main
        // thread's current (innermost) frame.
        state = thread_state_manager_.get_thread(k_main_thread_id);
    } else {
        // A frameId that no longer resolves (from an earlier stop) must fail
        // rather than be reinterpreted as a raw VM frame index, which would
        // silently evaluate in an unrelated frame.
        const auto mapping = variable_inspector_.resolve_frame(frame_id);

        if (!mapping) {
            return evaluation_failed();
        }

        state = thread_state_manager_.get_thread(mapping->thread_id);
        frame_index = mapping->frame_index;
    }

    if (!state) {
        return evaluation_failed();
    }

    // Hold the thread's lock for the whole evaluation: terminate() and task
    // exit null ThreadState::vm under this lock before destroying the VM, so
    // the captured pointer stays valid until evaluate() returns.
    const auto lock = thread_state_manager_.lock_state(*state);

    if (!state->is_paused || state->vm == nullptr) {
        return evaluation_failed();
    }

    return expression_evaluator_->evaluate(state->vm, frame_index, expression, context);
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

std::string DebugSession::last_exception_message() const {
    return execution_engine_->last_exception_message();
}

bool DebugSession::last_exception_is_caught() const {
    return execution_engine_->last_exception_is_caught();
}

std::optional<std::pair<std::string, bool>> DebugSession::exception_info(int thread_id) const {
    const auto state = thread_state_manager_.get_thread(thread_id);
    if (!state) {
        return std::nullopt;
    }

    const auto lock = thread_state_manager_.lock_state(*state);
    if (state->exception_message.empty()) {
        return std::nullopt;
    }

    return std::pair{state->exception_message, state->exception_caught};
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
