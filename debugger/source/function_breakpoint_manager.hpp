#ifndef LUMA_DAP_FUNCTION_BREAKPOINT_MANAGER_HPP
#define LUMA_DAP_FUNCTION_BREAKPOINT_MANAGER_HPP

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "breakpoint_shared_context.hpp"
#include "dap_types.hpp"

namespace luma::dap {

// ═══════════════════════════════════════════════════════════
// FunctionBreakpointManager — manages function breakpoints.
//
// All public methods acquire ctx_->mutex internally.
// ═══════════════════════════════════════════════════════════

class FunctionBreakpointManager {
public:
    explicit FunctionBreakpointManager(BreakpointSharedContext* ctx) : ctx_(ctx) {}

    // Set (or replace) function breakpoints.
    [[nodiscard]] std::vector<Breakpoint>
    set_function_breakpoints(const std::vector<BreakpointRequest>& requests);

    // Resolve unverified function breakpoints against compiled function names.
    void resolve_function_breakpoints();

    // Check whether a function breakpoint exists at the given location.
    // Increments the hit counter and returns a snapshot if found.
    // Must be called under ctx_->mutex.
    [[nodiscard]] std::optional<BreakpointSnapshot> find_matching_breakpoint(int file_id, int line,
                                                                             bool record_hit) const;

    // Returns true if any verified function breakpoints exist for the given file_id.
    // Must be called under ctx_->mutex.
    [[nodiscard]] bool has_breakpoints_for_file_id(int file_id) const;

    // Returns true if the function breakpoint spatial index is non-empty.
    // Must be called under ctx_->mutex.
    [[nodiscard]] bool has_any_breakpoints() const;

private:
    struct FunctionBreakpointInfo {
        int id{0};
        std::string condition;
        std::string hit_condition;
        std::string log_message;
        // Mutable because find_matching_breakpoint() is a const method that
        // increments the hit counter.  Always accessed under ctx_->mutex.
        mutable int times_hit{0};
        std::string name;
        int file_id{0};
        int line{0};
        bool verified{false};
    };

    struct FunctionLocation {
        int file_id{0};
        int line{0};
    };

    [[nodiscard]] std::optional<FunctionLocation>
    find_function_location(const std::string& name) const;

    [[nodiscard]] FunctionBreakpointInfo
    create_function_breakpoint_info(const BreakpointRequest& req);

    [[nodiscard]] Breakpoint build_function_breakpoint_response(const FunctionBreakpointInfo& info,
                                                                const BreakpointRequest& req) const;

    BreakpointSharedContext* ctx_;

    // Function breakpoints.
    std::vector<FunctionBreakpointInfo> function_breakpoints_; // GUARDED_BY(ctx_->mutex)

    // Function breakpoint index: (file_id, line) → index into function_breakpoints_.
    std::map<std::pair<int, int>, std::size_t> function_bp_index_; // GUARDED_BY(ctx_->mutex)
};

} // namespace luma::dap

#endif // LUMA_DAP_FUNCTION_BREAKPOINT_MANAGER_HPP
