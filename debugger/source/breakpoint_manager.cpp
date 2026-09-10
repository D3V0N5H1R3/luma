#include "breakpoint_manager.hpp"

#include <filesystem>

#include "analysis/source/source_manager.hpp"
#include "dap_breakpoint_validator.hpp"
#include "dap_types.hpp"
#include "runtime/compiler/chunk.hpp"

namespace luma::dap {

// ─── Construction ───

BreakpointManager::BreakpointManager() : line_mgr_(&ctx_) {}

// ─── Configuration ───

void BreakpointManager::set_source_locator(ISourceLocator* locator) {
    ctx_.source_locator = locator;
}

void BreakpointManager::set_compiled_program(std::shared_ptr<std::vector<CompiledFunction>> fns,
                                             std::shared_ptr<CompiledFunction> top_level) {
    const std::scoped_lock lock(ctx_.mutex);
    ctx_.compiled_functions = std::move(fns);
    ctx_.compiled_top_level = std::move(top_level);
    ctx_.source_map_cache.clear();
}

// ─── Diagnostic callback ───

void BreakpointManager::set_diagnostic_callback(DiagnosticFn cb) {
    diagnostic_fn_ = std::move(cb);
}

// ─── Delegation to sub-managers ───

std::vector<Breakpoint>
BreakpointManager::set_breakpoints(const std::string& path,
                                   const std::vector<BreakpointRequest>& requests) {
    auto result = line_mgr_.set_breakpoints(path, requests);
    update_breakpoints_active_flag();
    return result;
}

void BreakpointManager::set_exception_breakpoints(const std::vector<std::string>& filters) {
    exception_settings_.set_exception_breakpoints(filters);
}

// ─── Cache pre-population ───

void BreakpointManager::preload_canonical_paths() {
    if (ctx_.source_locator == nullptr) {
        return;
    }

    const std::scoped_lock lock(ctx_.mutex);

    ctx_.source_locator->for_each_file([&](int fid, const SourceFile* file) {
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(file->path, ec);

        if (!ec) {
            ctx_.canonical_path_cache.try_emplace(canonical.string(), fid);
        }
    });
}

// ─── Resolution after compilation ───

void BreakpointManager::resolve_pending_breakpoints() {
    line_mgr_.resolve_pending_breakpoints();

    if (diagnostic_fn_) {
        for (const auto& path : line_mgr_.get_unresolved_paths()) {
            diagnostic_fn_("Breakpoint in '" + path +
                           "' could not be resolved: file not found in compiled program");
        }
    }

    update_breakpoints_active_flag();
}

// ─── Runtime check ───

BreakpointManager::BreakpointCheckResult
BreakpointManager::check_breakpoint(int file_id, int line,
                                    const ConditionEvaluatorFn& eval_condition) const {
    // Phase 1: snapshot every breakpoint bound to this line (distinct requested
    // lines can snap to the same executable line) *without* recording a hit — a
    // conditional breakpoint only counts as hit when its condition holds.
    std::vector<BreakpointSnapshot> matches;

    {
        const std::scoped_lock lock(ctx_.mutex);

        matches = line_mgr_.find_matching_breakpoints(file_id, line);
    }

    // Evaluate each breakpoint in turn; the first one that fully qualifies
    // (condition holds AND hit condition satisfied) determines the stop / log.
    for (const auto& match : matches) {
        // Phase 2: evaluate the condition against the live frame, outside the
        // leaf lock.  A false condition is not a hit and must not advance the
        // counter, so this runs before the hit is recorded.
        if (!match.condition.empty() && eval_condition) {
            if (eval_condition(match.condition) != "true") {
                continue;
            }
        }

        // Phase 3: record the qualifying hit for this specific breakpoint and
        // read back its updated count.
        std::optional<int> times_hit;

        {
            const std::scoped_lock lock(ctx_.mutex);

            times_hit = line_mgr_.record_breakpoint_hit(file_id, line, match.id);
        }

        // The breakpoint may have been removed by a concurrent setBreakpoints
        // call between phase 1 and phase 3; if so, skip it.
        if (!times_hit) {
            continue;
        }

        // Phase 4: gate on the hit condition using the condition-true hit count.
        if (!match.hit_condition.empty()) {
            if (!evaluate_hit_condition(match.hit_condition, *times_hit)) {
                continue;
            }
        }

        BreakpointCheckResult result;

        if (!match.log_message.empty()) {
            result.log_message = match.log_message;
            result.hit_breakpoint_id = match.id;
            return result;
        }

        result.should_break = true;
        result.hit_breakpoint_id = match.id;
        return result;
    }

    return {};
}

// ─── Queries ───

bool BreakpointManager::has_breakpoints_in_file(const std::string& source_path) const {
    const auto abs_path = std::filesystem::absolute(source_path).string();
    const std::scoped_lock lock(ctx_.mutex);
    const int file_id = ctx_.find_file_id(abs_path);

    if (file_id < 0) {
        return false;
    }

    return line_mgr_.has_breakpoints_for_file_id(file_id);
}

bool BreakpointManager::has_breakpoints_for_file_id(int file_id) const {
    const std::scoped_lock lock(ctx_.mutex);
    return line_mgr_.has_breakpoints_for_file_id(file_id);
}

std::vector<int> BreakpointManager::get_breakpoint_locations(const std::string& path,
                                                             int start_line, int end_line) const {
    return line_mgr_.get_breakpoint_locations(path, start_line, end_line);
}

// ─── Active flag ───

void BreakpointManager::update_breakpoints_active_flag() {
    const std::scoped_lock lock(ctx_.mutex);

    const bool active = line_mgr_.has_any_breakpoints();

    breakpoints_active_.store(active, std::memory_order_release);
}

} // namespace luma::dap
