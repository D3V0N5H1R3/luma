#include <filesystem>
#include <format>
#include <memory>
#include <utility>

#include "analysis/source/source_manager.hpp"
#include "breakpoint_manager.hpp"
#include "dap_types.hpp"
#include "debug_execution_engine.hpp"
#include "debug_session.hpp"
#include "expression_compiler.hpp"
#include "expression_evaluator.hpp"
#include "runtime/stdlib/common/stdlib_registry.hpp"
#include "runtime/vm/vm.hpp"
#include "source_manager_locator.hpp"
#include "thread_state_manager.hpp"
#include "variable_inspector.hpp"
#include "vm_debug_adapter.hpp"

namespace luma::dap {

namespace {

// Required file extension for Luma source programs.
constexpr std::string_view k_luma_file_extension = ".luma";

} // namespace

// ─── Launch configuration ───

std::string DebugExecutionEngine::validate_launch_config(const std::string& program_path,
                                                         const std::string& cwd) {
    program_path_ = std::filesystem::absolute(program_path).string();
    source_manager_ = std::make_unique<SourceManager>();

    if (!program_path_.ends_with(k_luma_file_extension)) {
        return "File must have .luma extension";
    }

    if (!cwd.empty()) {
        std::error_code ec;
        const auto cwd_path = std::filesystem::path(cwd);
        if (!std::filesystem::exists(cwd_path, ec) || ec) {
            return std::format("Working directory does not exist: {}", cwd);
        }
        if (!std::filesystem::is_directory(cwd_path, ec) || ec) {
            return std::format("Working directory is not a directory: {}", cwd);
        }
    }

    return "";
}

void DebugExecutionEngine::start_execution_thread(bool stop_on_entry,
                                                  const std::vector<std::string>& args,
                                                  const std::string& cwd, bool no_debug) {
    auto main_state = std::make_shared<ThreadState>();
    main_state->thread_id = k_main_thread_id;
    main_state->name = "Main Thread";
    main_state->pending.stop_on_entry = stop_on_entry;

    thread_mgr_.clear();
    thread_mgr_.add_thread(main_state);

    // Publish the stop token *before* the execution thread exists.  Thread
    // creation then orders this write before every read of the token on the
    // execution thread and on task threads (should_break / wait_for_resume),
    // so the token is never read while it is being assigned.
    execution_stop_source_ = std::stop_source{};
    execution_stop_token_ = execution_stop_source_.get_token();
    program_cancel_token_ = std::make_shared<CancellationToken>();

    execution_thread_ =
        std::jthread([this, fns = compiled_functions_, top = compiled_top_level_, main_state,
                      program_args = args, working_dir = cwd, no_debug] {
            run_execution(fns, top, main_state, program_args, working_dir, no_debug);
        });
}

std::string DebugExecutionEngine::launch(const std::string& program_path, bool stop_on_entry,
                                         const std::vector<std::string>& args,
                                         const std::string& cwd, bool no_debug) {
    auto config_error = validate_launch_config(program_path, cwd);

    if (!config_error.empty()) {
        return config_error;
    }

    try {
        std::vector<std::string> detailed_errors;
        std::string compile_error;
        auto compiled = compile_program_pipeline(*source_manager_, program_path_, compile_error,
                                                 &detailed_errors);

        if (!compiled.has_value()) {
            for (const auto& err : detailed_errors) {
                output_callback_(std::string{k_output_stderr}, err + "\n");
            }

            return compile_error + " (see output for details)";
        }

        // Start the execution thread.
        state_ = SessionState::Running;
        is_config_done_ = false;

        compiled_functions_ =
            std::make_shared<std::vector<CompiledFunction>>(std::move(compiled->functions));
        compiled_top_level_ = std::make_shared<CompiledFunction>(std::move(compiled->top_level));

        // Configure session-level components.
        source_locator_ = std::make_unique<SourceManagerLocator>(source_manager_.get());
        bp_mgr_.set_source_locator(source_locator_.get());
        bp_mgr_.set_compiled_program(compiled_functions_, compiled_top_level_);
        bp_mgr_.preload_canonical_paths();
        bp_mgr_.set_diagnostic_callback([this](const std::string& message) {
            output_callback_(std::string{k_output_console}, message + "\n");
        });
        expr_eval_.set_compiled_program(compiled_functions_, compiled_top_level_);

        start_execution_thread(stop_on_entry, args, cwd, no_debug);

        return "";
    } catch (const std::exception& e) {
        return std::format("Failed to launch: {}", e.what());
    }
}

void DebugExecutionEngine::terminate(bool emit_exit_events) {
    suppress_exit_events_.store(!emit_exit_events, std::memory_order_relaxed);
    execution_stop_source_.request_stop();

    // Unblock a program waiting in an await, task_scope join, or channel wait
    // (and cancel its tasks); the stop request alone is only seen on a line
    // change, which such a program never reaches.  Natives that block on the
    // OS without polling a token (sockets, processes, HTTP) are not covered.
    if (program_cancel_token_) {
        program_cancel_token_->cancel();
    }

    {
        const auto lock = lock_config();
        is_config_done_ = true;
        config_cv_.notify_all();
    }

    thread_mgr_.force_unpause_all();

    if (execution_thread_.joinable()) {
        execution_thread_.join();
    }

    // Null out all vm pointers before destroying the VM to prevent
    // dangling pointer dereferences from late-arriving DAP requests.
    thread_mgr_.null_all_vms();

    // Invalidate all variable/frame references before destroying the VM.
    // After invalidate_refs(), VariableReferenceRegistry's generation counter
    // advances so every outstanding FrameMapping (which holds a non-owning VM*)
    // becomes stale and will not be dereferenced by late DAP requests.
    //
    // Safety contract: null_all_vms() (above) clears the per-thread VM* fields,
    // and invalidate_refs() marks all FrameMapping entries as stale.  Both must
    // happen before vm_.reset() destroys the VM.  If the ordering changes, the
    // stale VM* inside FrameMapping becomes a dangling-pointer risk.
    var_inspector_.invalidate_refs();
    vm_adapter_.reset();
    vm_.reset();
    state_ = SessionState::Terminated;
}

void DebugExecutionEngine::configuration_done() {
    const auto lock = lock_config();
    is_config_done_ = true;
    config_cv_.notify_all();
}

bool DebugExecutionEngine::wait_for_configuration() {
    auto lock = lock_config_unique();
    config_cv_.wait(lock.underlying(),
                    [this] { return is_config_done_ || execution_stop_token_.stop_requested(); });

    if (execution_stop_token_.stop_requested()) {
        state_ = SessionState::Terminated;
        return false;
    }

    return true;
}

// ─── State queries ───

std::string DebugExecutionEngine::last_exception_message() const {
    const auto lock = lock_exception();
    return last_exception_message_;
}

bool DebugExecutionEngine::last_exception_is_caught() const {
    const auto lock = lock_exception();
    return last_exception_is_caught_;
}

// ─── Lock helpers ───

OrderedLockGuard<DapLockId> DebugExecutionEngine::lock_config() const {
    return OrderedLockGuard<DapLockId>(config_mutex_, DapLockId::Config);
}

OrderedUniqueLock<DapLockId> DebugExecutionEngine::lock_config_unique() const {
    return OrderedUniqueLock<DapLockId>(config_mutex_, DapLockId::Config);
}

OrderedLockGuard<DapLockId> DebugExecutionEngine::lock_exception() const {
    return OrderedLockGuard<DapLockId>(exception_mutex_, DapLockId::Exception);
}

} // namespace luma::dap
