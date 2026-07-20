#ifndef LUMA_DAP_LINE_BREAKPOINT_MANAGER_HPP
#define LUMA_DAP_LINE_BREAKPOINT_MANAGER_HPP

#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "breakpoint_shared_context.hpp"
#include "dap_types.hpp"

namespace luma::dap {

// ═══════════════════════════════════════════════════════════
// LineBreakpointManager — manages source-line breakpoints.
//
// All public methods acquire ctx_->mutex internally.
// ═══════════════════════════════════════════════════════════

class LineBreakpointManager {
public:
    explicit LineBreakpointManager(BreakpointSharedContext* ctx) : ctx_(ctx) {}

    // Set (or replace) breakpoints for a source file.
    [[nodiscard]] std::vector<Breakpoint>
    set_breakpoints(const std::string& path, const std::vector<BreakpointRequest>& requests);

    // Resolve path-based breakpoints that were set before the program was compiled.
    void resolve_pending_breakpoints();

    // Returns paths with pending breakpoints that could not be resolved to a file ID.
    // Call after resolve_pending_breakpoints() to detect unresolved entries.
    // Must NOT be called under ctx_->mutex.
    [[nodiscard]] std::vector<std::string> get_unresolved_paths() const;

    // Check whether a line breakpoint exists at the given location.
    // Increments the hit counter and returns a snapshot if found.
    // Must be called under ctx_->mutex.
    [[nodiscard]] std::optional<BreakpointSnapshot> find_matching_breakpoint(int file_id, int line,
                                                                             bool record_hit) const;

    // Returns true if any line breakpoints exist for the given file_id.
    // Must be called under ctx_->mutex.
    [[nodiscard]] bool has_breakpoints_for_file_id(int file_id) const;

    // Returns true if at least one line breakpoint is set anywhere.
    // Must be called under ctx_->mutex.
    [[nodiscard]] bool has_any_breakpoints() const;

    // Returns executable line locations within a range for a source file.
    [[nodiscard]] std::vector<int> get_breakpoint_locations(const std::string& path, int start_line,
                                                            int end_line) const;

private:
    struct LineBreakpointInfo {
        int id{0};
        std::string condition;
        std::string hit_condition;
        std::string log_message;
        // Mutable because find_matching_breakpoint() is a const method that
        // increments the hit counter.  Always accessed under ctx_->mutex.
        mutable int times_hit{0};
        int line{0};
    };

    [[nodiscard]] static int snap_line(int requested, const std::set<int>& executable);

    [[nodiscard]] static Breakpoint build_breakpoint_response(int bp_id, bool verified, int line,
                                                              int requested_line,
                                                              const std::string& abs_path,
                                                              const BreakpointRequest& req);

    void preserve_hit_counts(const std::map<int, LineBreakpointInfo>& old_breakpoints,
                             LineBreakpointInfo& info, const BreakpointRequest& req);

    [[nodiscard]] std::vector<Breakpoint>
    build_line_breakpoint_responses(const std::string& abs_path,
                                    const std::vector<BreakpointRequest>& requests, int file_id,
                                    const std::set<int>& executable) const;

    [[nodiscard]] LineBreakpointInfo create_line_breakpoint_info(const BreakpointRequest& req,
                                                                 int snapped_line) const;

    BreakpointSharedContext* ctx_;

    // Line breakpoints: file_id → (line → info).
    std::unordered_map<int, std::map<int, LineBreakpointInfo>>
        line_breakpoints_; // GUARDED_BY(ctx_->mutex)

    // Path-based breakpoints (before file_id resolution).
    std::unordered_map<std::string, std::vector<BreakpointRequest>>
        path_breakpoints_; // GUARDED_BY(ctx_->mutex)
};

} // namespace luma::dap

#endif // LUMA_DAP_LINE_BREAKPOINT_MANAGER_HPP
