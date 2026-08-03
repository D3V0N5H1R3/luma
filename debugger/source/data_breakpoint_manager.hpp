#ifndef LUMA_DAP_DATA_BREAKPOINT_MANAGER_HPP
#define LUMA_DAP_DATA_BREAKPOINT_MANAGER_HPP

#include <functional>
#include <string>

#include "breakpoint_shared_context.hpp"
#include "common/string_hash.hpp"

namespace luma::dap {

// ═══════════════════════════════════════════════════════════
// DataBreakpointManager — manages data (watchpoint) breakpoints.
//
// All public methods acquire ctx_->mutex internally.
//
// ─── Feature status ───
// Data breakpoints watch for writes to named variables.
//
// Limitations (current implementation):
//   - Matches by variable name only, not by memory address.
//   - The variable name must be known at set time and match exactly.
//   - Only "write" and "readWrite" access types are recognised.
//   - Data breakpoints are checked at every VM source-line hook —
//     the variable_name is compared against the variable being written
//     in the data hook callback, not a memory watchpoint.
//   - No support for compound conditions or chained data breakpoints.
// ═══════════════════════════════════════════════════════════

class DataBreakpointManager {
public:
    using ConditionEvaluatorFn = std::function<std::string(const std::string& expression)>;

    explicit DataBreakpointManager(BreakpointSharedContext* ctx) : ctx_(ctx) {}

    void set_data_breakpoint(const std::string& variable_name, const std::string& access_type,
                             const std::string& condition);

    void clear_data_breakpoints();

    // Check whether a variable write should trigger a data breakpoint.
    [[nodiscard]] bool check_data_breakpoint(const std::string& variable_name,
                                             const ConditionEvaluatorFn& eval_condition) const;

    // Returns true if any data breakpoints exist.
    // Must be called under ctx_->mutex.
    [[nodiscard]] bool has_any_breakpoints() const;

private:
    struct DataBreakpointInfo {
        std::string variable_name;
        std::string access_type; // "write" or "readWrite"
        std::string condition;
    };

    BreakpointSharedContext* ctx_;

    // Data breakpoints: variable_name → info (O(1) lookup).
    StringMap<DataBreakpointInfo> data_breakpoints_; // GUARDED_BY(ctx_->mutex)
};

} // namespace luma::dap

#endif // LUMA_DAP_DATA_BREAKPOINT_MANAGER_HPP
