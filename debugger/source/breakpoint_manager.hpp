#ifndef LUMA_DAP_BREAKPOINT_MANAGER_HPP
#define LUMA_DAP_BREAKPOINT_MANAGER_HPP

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "breakpoint_shared_context.hpp"
#include "dap_types.hpp"
#include "data_breakpoint_manager.hpp"
#include "exception_breakpoint_settings.hpp"
#include "function_breakpoint_manager.hpp"
#include "line_breakpoint_manager.hpp"

namespace luma {
struct CompiledFunction;
} // namespace luma

namespace luma::dap {

// ═══════════════════════════════════════════════════════════
// BreakpointManager — facade that delegates to focused
// sub-managers for each breakpoint category.
//
// The public API is unchanged.  Internally, breakpoint logic
// is split across:
//   • LineBreakpointManager      — source-line breakpoints
//   • FunctionBreakpointManager  — function-name breakpoints
//   • DataBreakpointManager      — variable-watch breakpoints
//   • ExceptionBreakpointSettings — caught/uncaught flags
//
// All sub-managers share a BreakpointSharedContext that holds
// the single mutex, caches, compiled program references, and
// the breakpoint ID allocator.
//
// ─── Synchronisation model ───
// Thread safety is provided by a single mutex in the shared
// context.  Exception filter flags are std::atomic<bool> so
// the VM's hot-path can query them without acquiring the mutex.
//
// The mutex is a leaf-level lock — independent of the
// OrderedLockGuard hierarchy (ThreadStates → PerThread → Config).
// ═══════════════════════════════════════════════════════════

class BreakpointManager {
public:
    using OutputFn = std::function<void(const std::string& category, const std::string& text)>;

    BreakpointManager();

    // ─── Configuration ───

    void set_source_locator(ISourceLocator* locator);
    void set_compiled_program(std::shared_ptr<std::vector<CompiledFunction>> fns,
                              std::shared_ptr<CompiledFunction> top_level);

    // ─── Line breakpoints ───

    [[nodiscard]] std::vector<Breakpoint>
    set_breakpoints(const std::string& path, const std::vector<BreakpointRequest>& requests);

    // ─── Function breakpoints ───

    [[nodiscard]] std::vector<Breakpoint>
    set_function_breakpoints(const std::vector<BreakpointRequest>& requests);

    // ─── Exception breakpoint filters ───

    void set_exception_breakpoints(const std::vector<std::string>& filters);

    [[nodiscard]] bool break_on_caught() const {
        return exception_settings_.break_on_caught.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool break_on_uncaught() const {
        return exception_settings_.break_on_uncaught.load(std::memory_order_acquire);
    }

    // ─── Condition evaluation callback ───

    using ConditionEvaluatorFn = std::function<std::string(const std::string& expression)>;

    // ─── Data breakpoints ───

    void set_data_breakpoint(const std::string& variable_name, const std::string& access_type,
                             const std::string& condition);
    void clear_data_breakpoints();

    [[nodiscard]] bool check_data_breakpoint(const std::string& variable_name,
                                             const ConditionEvaluatorFn& eval_condition) const;

    // ─── Cache pre-population ───

    // Pre-populate the canonical path cache with all source paths known to
    // the source locator.  Call once after set_source_locator() and
    // set_compiled_program() to avoid expensive per-lookup system calls
    // to weakly_canonical() on the hot path.
    void preload_canonical_paths();

    // ─── Diagnostic callback ───

    // Optional callback type for resolution diagnostics.
    using DiagnosticFn = std::function<void(const std::string& message)>;

    // Set an optional diagnostic callback called when breakpoints cannot be resolved.
    // Must be called before resolve_pending_breakpoints() to take effect.
    void set_diagnostic_callback(DiagnosticFn cb);

    // ─── Resolution (after compilation) ───

    // Resolve path-based breakpoints against the compiled source map.
    // PRECONDITION: set_compiled_program() must have been called.
    // PRECONDITION: All path-based breakpoints must have been set via set_breakpoints().
    void resolve_pending_breakpoints();

    // Resolve function-name breakpoints against the compiled function list.
    // PRECONDITION: set_compiled_program() must have been called.
    void resolve_function_breakpoints();

    // ─── Runtime check ───

    struct BreakpointCheckResult {
        bool should_break{false};
        int hit_breakpoint_id{0};
        std::string log_message;
    };

    [[nodiscard]] BreakpointCheckResult
    check_breakpoint(int file_id, int line, const ConditionEvaluatorFn& eval_condition) const;

    // ─── Queries ───

    [[nodiscard]] bool has_active_breakpoints() const {
        return breakpoints_active_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool has_breakpoints_in_file(const std::string& source_path) const;
    [[nodiscard]] bool has_breakpoints_for_file_id(int file_id) const;
    [[nodiscard]] std::vector<int> get_breakpoint_locations(const std::string& path, int start_line,
                                                            int end_line) const;

private:
    void update_breakpoints_active_flag();

    // Shared context owned by the facade, passed by pointer to sub-managers.
    BreakpointSharedContext ctx_;

    LineBreakpointManager line_mgr_;
    FunctionBreakpointManager func_mgr_;
    DataBreakpointManager data_mgr_;
    ExceptionBreakpointSettings exception_settings_;

    // Cached flag — true when at least one breakpoint is set.
    std::atomic<bool> breakpoints_active_{false};

    DiagnosticFn diagnostic_fn_;
};

} // namespace luma::dap

#endif // LUMA_DAP_BREAKPOINT_MANAGER_HPP
