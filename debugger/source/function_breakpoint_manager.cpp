#include "function_breakpoint_manager.hpp"

#include <filesystem>
#include <format>
#include <limits>

#include "analysis/source/source_manager.hpp"
#include "dap_breakpoint_validator.hpp"
#include "runtime/compiler/chunk.hpp"

namespace luma::dap {

// ─── Helpers ───

std::optional<FunctionBreakpointManager::FunctionLocation>
FunctionBreakpointManager::find_function_location(const std::string& name) const {
    if (!ctx_->compiled_functions) {
        return std::nullopt;
    }

    for (const auto& compiled_function : *ctx_->compiled_functions) {
        if (compiled_function.name != name) {
            continue;
        }

        const auto& source_map = compiled_function.chunk().source_map;

        if (source_map.empty()) {
            continue;
        }

        return FunctionLocation{.file_id = source_map.begin()->second.file_id,
                                .line = source_map.begin()->second.line};
    }

    return std::nullopt;
}

// ─── Function breakpoints ───

FunctionBreakpointManager::FunctionBreakpointInfo
FunctionBreakpointManager::create_function_breakpoint_info(const BreakpointRequest& req) {
    FunctionBreakpointInfo info;
    info.id = ctx_->next_breakpoint_id++;
    info.name = req.name;
    info.condition = req.condition;
    info.hit_condition = req.hit_condition;
    info.log_message = req.log_message;
    info.times_hit = 0;

    auto location = find_function_location(req.name);

    if (location) {
        info.file_id = location->file_id;
        info.line = location->line;
        info.verified = true;
    }

    return info;
}

Breakpoint
FunctionBreakpointManager::build_function_breakpoint_response(const FunctionBreakpointInfo& info,
                                                              const BreakpointRequest& req) const {
    std::string source_path;

    if (info.verified && ctx_->source_locator != nullptr) {
        const auto* file = ctx_->source_locator->get_file(info.file_id);

        if (file != nullptr) {
            source_path = std::filesystem::absolute(file->path).string();
        }
    }

    Breakpoint breakpoint = build_base_breakpoint_response(info.id, info.verified, info.line,
                                                           source_path, req.hit_condition);

    if (!info.verified) {
        breakpoint.message = std::format("Function '{}' not found", req.name);
    }

    return breakpoint;
}

std::vector<Breakpoint> FunctionBreakpointManager::set_function_breakpoints(
    const std::vector<BreakpointRequest>& requests) {
    const std::scoped_lock lock(ctx_->mutex);

    std::vector<Breakpoint> result;
    std::vector<FunctionBreakpointInfo> new_function_breakpoints;
    std::map<std::pair<int, int>, std::size_t> new_index;

    for (const auto& req : requests) {
        auto info = create_function_breakpoint_info(req);
        const auto index = new_function_breakpoints.size();
        result.push_back(build_function_breakpoint_response(info, req));

        if (info.verified) {
            new_index[{info.file_id, info.line}] = index;
        }

        new_function_breakpoints.push_back(std::move(info));
    }

    function_breakpoints_ = std::move(new_function_breakpoints);
    function_bp_index_ = std::move(new_index);

    return result;
}

// ─── Function breakpoint resolution ───

void FunctionBreakpointManager::resolve_function_breakpoints() {
    if (!ctx_->compiled_functions) {
        return;
    }

    const std::scoped_lock lock(ctx_->mutex);

    for (std::size_t i = 0; i < function_breakpoints_.size(); ++i) {
        auto& func_breakpoint = function_breakpoints_[i];

        if (func_breakpoint.verified) {
            continue;
        }

        auto location = find_function_location(func_breakpoint.name);

        if (location) {
            func_breakpoint.file_id = location->file_id;
            func_breakpoint.line = location->line;
            func_breakpoint.verified = true;
            function_bp_index_[{func_breakpoint.file_id, func_breakpoint.line}] = i;
        }
    }
}

// ─── Runtime lookup ───

std::optional<BreakpointSnapshot>
FunctionBreakpointManager::find_matching_breakpoint(int file_id, int line, bool record_hit) const {
    auto func_breakpoint_it = function_bp_index_.find({file_id, line});

    if (func_breakpoint_it != function_bp_index_.end()) {
        return make_breakpoint_snapshot(function_breakpoints_[func_breakpoint_it->second],
                                        record_hit);
    }

    return std::nullopt;
}

// ─── Queries ───

bool FunctionBreakpointManager::has_breakpoints_for_file_id(int file_id) const {
    return std::ranges::any_of(function_breakpoints_, [file_id](const auto& bp) {
        return bp.verified && bp.file_id == file_id;
    });
}

bool FunctionBreakpointManager::has_any_breakpoints() const {
    return !function_bp_index_.empty();
}

} // namespace luma::dap
