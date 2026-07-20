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
#include "hot_reloader.hpp"
#include "i_filesystem_monitor.hpp"
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

void DebugExecutionEngine::setup_hot_reloader() {
    hot_reloader_ =
        std::make_unique<HotReloader>([this](const std::filesystem::path& changed_file) {
            const auto filename = changed_file.filename().string();

            output_callback_(std::string{kOutputConsole},
                             std::format("Hot reload: source changed — {}\n", filename));

            event_callback_(std::string{kEventInvalidated},
                            JsonValue(JsonValue::ObjectType{
                                {"areas", JsonValue(JsonValue::ArrayType{
                                              JsonValue(std::string("stacks")),
                                              JsonValue(std::string("threads")),
                                              JsonValue(std::string("variables")),
                                          })},
                            }));
        });

    source_locator_->for_each_file([&](int /*fid*/, const SourceFile* file) {
        // Best-effort watch — filesystem errors are logged inside watch()
        // but do not prevent the debug session from starting.
        (void)hot_reloader_->watch(file->path);
    });
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

    execution_thread_ =
        std::jthread([this, fns = compiled_functions_, top = compiled_top_level_, main_state,
                      program_args = args, working_dir = cwd, no_debug](std::stop_token stoken) {
            execution_stop_token_ = std::move(stoken);
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
                output_callback_(std::string{kOutputStderr}, err + "\n");
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
        expr_eval_.set_compiled_program(compiled_functions_, compiled_top_level_);

        setup_hot_reloader();
        start_execution_thread(stop_on_entry, args, cwd, no_debug);

        return "";
    } catch (const std::exception& e) {
        return std::format("Failed to launch: {}", e.what());
    }
}

void DebugExecutionEngine::terminate() {
    execution_thread_.request_stop();

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

IVMControl* DebugExecutionEngine::vm_control() const {
    return vm_adapter_.get();
}

IVMIntrospection* DebugExecutionEngine::vm_introspection() const {
    return vm_adapter_.get();
}

int DebugExecutionEngine::check_for_source_changes() {
    if (!hot_reloader_) {
        return 0;
    }

    return hot_reloader_->check_for_changes();
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
