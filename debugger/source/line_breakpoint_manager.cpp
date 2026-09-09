#include "line_breakpoint_manager.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <limits>

#include "dap_breakpoint_validator.hpp"

namespace luma::dap {

// ─── Line-specific helpers ───

int LineBreakpointManager::snap_line(int requested, const std::set<int>& executable) {
    if (executable.empty() || executable.contains(requested)) {
        return requested;
    }

    auto it = executable.lower_bound(requested);

    if (it != executable.end()) {
        return *it;
    }

    return *executable.rbegin();
}

Breakpoint LineBreakpointManager::build_breakpoint_response(int bp_id, bool verified, int line,
                                                            int requested_line,
                                                            const std::string& abs_path,
                                                            const BreakpointRequest& req) {
    auto breakpoint =
        build_base_breakpoint_response(bp_id, verified, line, abs_path, req.hit_condition);
    breakpoint.condition = req.condition;
    breakpoint.hit_condition = req.hit_condition;
    breakpoint.log_message = req.log_message;

    if (verified && line != requested_line) {
        breakpoint.message = std::format("Breakpoint moved to executable line {}", line);
    } else if (!verified) {
        breakpoint.message = "Breakpoint will be verified when program launches";
    }

    return breakpoint;
}

void LineBreakpointManager::preserve_hit_counts(
    std::map<int, std::vector<LineBreakpointInfo>>& old_breakpoints, LineBreakpointInfo& info,
    const BreakpointRequest& req) {
    auto old_it = old_breakpoints.find(info.line);

    if (old_it != old_breakpoints.end()) {
        auto& bucket = old_it->second;

        // Find a prior breakpoint at this line with identical condition fields
        // and adopt its id + hit count.  Consume the match so a second, byte-
        // identical request at the same line gets a fresh id rather than
        // aliasing the same counter.
        for (auto it = bucket.begin(); it != bucket.end(); ++it) {
            if (it->condition == req.condition && it->hit_condition == req.hit_condition &&
                it->log_message == req.log_message) {
                info.id = it->id;
                info.times_hit = it->times_hit;
                bucket.erase(it);
                return;
            }
        }
    }

    info.id = ctx_->next_breakpoint_id++;
    info.times_hit = 0;
}

LineBreakpointManager::LineBreakpointInfo
LineBreakpointManager::create_line_breakpoint_info(const BreakpointRequest& req,
                                                   int snapped_line) const {
    LineBreakpointInfo info;
    info.line = snapped_line;
    info.condition = req.condition;
    info.hit_condition = req.hit_condition;
    info.log_message = req.log_message;
    return info;
}

// ─── Line breakpoints ───

std::vector<Breakpoint>
LineBreakpointManager::set_breakpoints(const std::string& path,
                                       const std::vector<BreakpointRequest>& requests) {
    const auto abs_path = std::filesystem::absolute(path).string();

    const std::scoped_lock lock(ctx_->mutex);

    path_breakpoints_[abs_path] = requests;

    const int file_id = ctx_->find_file_id(abs_path);

    if (file_id >= 0) {
        const auto& executable = ctx_->collect_executable_lines(file_id);

        auto old_breakpoints = std::move(line_breakpoints_[file_id]);
        auto& new_breakpoints = line_breakpoints_[file_id];
        new_breakpoints.clear();

        std::vector<Breakpoint> responses;
        responses.reserve(requests.size());

        for (const auto& req : requests) {
            const int snapped = executable.empty() ? req.line : snap_line(req.line, executable);

            auto info = create_line_breakpoint_info(req, snapped);
            preserve_hit_counts(old_breakpoints, info, req);

            // Build the response from the just-assigned id so two requests that
            // snap to the same line each report their own breakpoint, then store
            // both (one-to-many per line) rather than overwriting.
            responses.push_back(
                build_breakpoint_response(info.id, true, snapped, req.line, abs_path, req));
            new_breakpoints[snapped].push_back(std::move(info));
        }

        return responses;
    }

    std::vector<Breakpoint> result;

    result.reserve(requests.size());
    for (const auto& req : requests) {
        result.push_back(build_breakpoint_response(ctx_->next_breakpoint_id++, false, req.line,
                                                   req.line, abs_path, req));
    }

    return result;
}

// ─── Resolution ───

void LineBreakpointManager::resolve_pending_breakpoints() {
    const std::scoped_lock lock(ctx_->mutex);

    for (const auto& [path, reqs] : path_breakpoints_) {
        const auto abs_path = std::filesystem::absolute(path).string();
        const int file_id = ctx_->find_file_id(abs_path);

        if (file_id >= 0) {
            const auto& executable = ctx_->collect_executable_lines(file_id);
            auto& existing = line_breakpoints_[file_id];

            for (const auto& req : reqs) {
                const int snapped = executable.empty() ? req.line : snap_line(req.line, executable);

                auto& bucket = existing[snapped];

                // Skip only if an identical breakpoint is already bound at this
                // line (e.g. a prior resolve or a post-launch setBreakpoints);
                // distinct breakpoints that snap to the same line coexist.
                const bool already_bound =
                    std::ranges::any_of(bucket, [&](const LineBreakpointInfo& bp) {
                        return bp.condition == req.condition &&
                               bp.hit_condition == req.hit_condition &&
                               bp.log_message == req.log_message;
                    });

                if (already_bound) {
                    continue;
                }

                auto info = create_line_breakpoint_info(req, snapped);
                info.id = ctx_->next_breakpoint_id++;
                info.times_hit = 0;
                bucket.push_back(std::move(info));
            }
        }
    }
}

std::vector<std::string> LineBreakpointManager::get_unresolved_paths() const {
    const std::scoped_lock lock(ctx_->mutex);
    std::vector<std::string> result;

    for (const auto& [path, reqs] : path_breakpoints_) {
        if (reqs.empty()) {
            continue;
        }

        const auto abs_path = std::filesystem::absolute(path).string();

        if (ctx_->find_file_id(abs_path) < 0) {
            result.push_back(path);
        }
    }

    return result;
}

// ─── Runtime lookup ───

std::vector<BreakpointSnapshot> LineBreakpointManager::find_matching_breakpoints(int file_id,
                                                                                 int line) const {
    std::vector<BreakpointSnapshot> matches;

    auto breakpoint_it = line_breakpoints_.find(file_id);

    if (breakpoint_it != line_breakpoints_.end()) {
        auto line_it = breakpoint_it->second.find(line);

        if (line_it != breakpoint_it->second.end()) {
            matches.reserve(line_it->second.size());

            for (const auto& info : line_it->second) {
                matches.push_back(make_breakpoint_snapshot(info, /*record_hit=*/false));
            }
        }
    }

    return matches;
}

std::optional<int> LineBreakpointManager::record_breakpoint_hit(int file_id, int line,
                                                                int bp_id) const {
    auto breakpoint_it = line_breakpoints_.find(file_id);

    if (breakpoint_it == line_breakpoints_.end()) {
        return std::nullopt;
    }

    auto line_it = breakpoint_it->second.find(line);

    if (line_it == breakpoint_it->second.end()) {
        return std::nullopt;
    }

    for (const auto& info : line_it->second) {
        if (info.id == bp_id) {
            return make_breakpoint_snapshot(info, /*record_hit=*/true).times_hit;
        }
    }

    return std::nullopt;
}

// ─── Queries ───

bool LineBreakpointManager::has_breakpoints_for_file_id(int file_id) const {
    auto it = line_breakpoints_.find(file_id);
    return it != line_breakpoints_.end() && !it->second.empty();
}

bool LineBreakpointManager::has_any_breakpoints() const {
    for (const auto& [file_id, line_map] : line_breakpoints_) {
        if (!line_map.empty()) {
            return true;
        }
    }

    return false;
}

std::vector<int> LineBreakpointManager::get_breakpoint_locations(const std::string& path,
                                                                 int start_line,
                                                                 int end_line) const {
    const auto abs_path = std::filesystem::absolute(path).string();

    const std::scoped_lock lock(ctx_->mutex);
    const int file_id = ctx_->find_file_id(abs_path);

    if (file_id < 0) {
        return {};
    }

    const auto& executable = ctx_->collect_executable_lines(file_id);
    std::vector<int> result;

    for (const int line : executable) {
        if (line >= start_line && line <= end_line) {
            result.push_back(line);
        }
    }

    return result;
}

} // namespace luma::dap
