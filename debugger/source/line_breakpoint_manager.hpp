#ifndef LUMA_DAP_LINE_BREAKPOINT_MANAGER_HPP
#define LUMA_DAP_LINE_BREAKPOINT_MANAGER_HPP

#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "breakpoint_shared_context.hpp"
#include "common/string_hash.hpp"
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

    // Check whether any line breakpoints exist at the given location and return
    // a snapshot of each (identity + condition/hit-condition/log fields) WITHOUT
    // recording a hit.  Multiple breakpoints can share one executable line when
    // distinct requested lines snap to it, so this returns all of them; the
    // caller evaluates each condition and records the qualifying hit via
    // record_breakpoint_hit().
    // Must be called under ctx_->mutex.
    [[nodiscard]] std::vector<BreakpointSnapshot> find_matching_breakpoints(int file_id,
                                                                            int line) const;

    // Record a hit for the breakpoint with `bp_id` at (file_id, line) and return
    // its updated (saturating) hit count, or std::nullopt if no such breakpoint
    // exists (e.g. removed by a concurrent setBreakpoints).
    // Must be called under ctx_->mutex.
    [[nodiscard]] std::optional<int> record_breakpoint_hit(int file_id, int line, int bp_id) const;

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
        // Mutable because record_breakpoint_hit() is a const method that
        // increments the hit counter.  Always accessed under ctx_->mutex.
        mutable int times_hit{0};
        int line{0};
    };

    [[nodiscard]] static int snap_line(int requested, const std::set<int>& executable);

    [[nodiscard]] static Breakpoint build_breakpoint_response(int bp_id, bool verified, int line,
                                                              int requested_line,
                                                              const std::string& abs_path,
                                                              const BreakpointRequest& req);

    void preserve_hit_counts(std::map<int, std::vector<LineBreakpointInfo>>& old_breakpoints,
                             LineBreakpointInfo& info, const BreakpointRequest& req);

    [[nodiscard]] LineBreakpointInfo create_line_breakpoint_info(const BreakpointRequest& req,
                                                                 int snapped_line) const;

    BreakpointSharedContext* ctx_;

    // Line breakpoints: file_id → (line → breakpoints on that line).  A line
    // holds a vector rather than a single entry because distinct requested
    // lines can snap to the same executable line; each retains its own
    // condition, hit condition, log message, and hit counter.
    std::unordered_map<int, std::map<int, std::vector<LineBreakpointInfo>>>
        line_breakpoints_; // GUARDED_BY(ctx_->mutex)

    // Path-based breakpoints (before file_id resolution).
    StringMap<std::vector<BreakpointRequest>> path_breakpoints_; // GUARDED_BY(ctx_->mutex)
};

} // namespace luma::dap

#endif // LUMA_DAP_LINE_BREAKPOINT_MANAGER_HPP
