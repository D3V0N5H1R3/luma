#include "line_breakpoint_manager.hpp"

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
    const std::map<int, LineBreakpointInfo>& old_breakpoints, LineBreakpointInfo& info,
    const BreakpointRequest& req) {
    auto old_it = old_breakpoints.find(info.line);

    if (old_it != old_breakpoints.end() && old_it->second.condition == req.condition &&
        old_it->second.hit_condition == req.hit_condition &&
        old_it->second.log_message == req.log_message) {
        info.id = old_it->second.id;
        info.times_hit = old_it->second.times_hit;
    } else {
        info.id = ctx_->next_breakpoint_id++;
        info.times_hit = 0;
    }
}

std::vector<Breakpoint> LineBreakpointManager::build_line_breakpoint_responses(
    const std::string& abs_path, const std::vector<BreakpointRequest>& requests, int file_id,
    const std::set<int>& executable) const {
    std::vector<Breakpoint> responses;
    const auto& file_bps = line_breakpoints_.at(file_id);

    for (const auto& req : requests) {
        const int snapped = executable.empty() ? req.line : snap_line(req.line, executable);
        auto it = file_bps.find(snapped);

        if (it != file_bps.end()) {
            responses.push_back(
                build_breakpoint_response(it->second.id, true, snapped, req.line, abs_path, req));
        }
    }

    return responses;
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
        line_breakpoints_[file_id].clear();

        for (const auto& req : requests) {
            const int snapped = executable.empty() ? req.line : snap_line(req.line, executable);

            auto info = create_line_breakpoint_info(req, snapped);
            preserve_hit_counts(old_breakpoints, info, req);
            line_breakpoints_[file_id][snapped] = info;
        }

        return build_line_breakpoint_responses(abs_path, requests, file_id, executable);
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

                if (existing.contains(snapped)) {
                    continue;
                }

                auto info = create_line_breakpoint_info(req, snapped);
                info.id = ctx_->next_breakpoint_id++;
                info.times_hit = 0;
                existing[snapped] = info;
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

std::optional<BreakpointSnapshot>
LineBreakpointManager::find_matching_breakpoint(int file_id, int line, bool record_hit) const {
    auto breakpoint_it = line_breakpoints_.find(file_id);

    if (breakpoint_it != line_breakpoints_.end()) {
        auto line_it = breakpoint_it->second.find(line);

        if (line_it != breakpoint_it->second.end()) {
            return make_breakpoint_snapshot(line_it->second, record_hit);
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
