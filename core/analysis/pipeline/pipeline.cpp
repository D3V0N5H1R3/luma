#include "analysis/pipeline/pipeline.hpp"

#include <chrono>
#include <format>
#include <string>
#include <unordered_set>

#include "analysis/diagnostics/diagnostic.hpp"
#include "common/string_hash.hpp"

namespace luma {

namespace {

// Build a location-less error diagnostic.  Internal pipeline errors are
// structural (a mis-ordered pass or a thrown exception), so they carry no
// source span.
Diagnostic make_error_diagnostic(std::string_view message) {
    return diag::error(std::string{message}).build();
}

// Returns true when every predecessor pass required by `pass` has completed.
// On the first missing dependency, emits an internal-error diagnostic into
// `result` and returns false.  This is the run-time safety net behind the
// build-time Builder::validate_dependencies() check.
bool dependencies_satisfied(const Pass& pass, const StringSet& completed_passes,
                            PipelineResult& result) {
    for (const auto& req : pass.required_passes()) {
        if (!completed_passes.contains(req)) {
            result.diagnostics.push_back(make_error_diagnostic(std::format(
                "internal error: pass '{}' requires '{}' which has not run", pass.name(), req)));

            return false;
        }
    }

    return true;
}

} // namespace

// Each pass appends its own diagnostics (errors and warnings) to the shared
// PipelineResult; run() gathers them in execution order.  How a given pass
// produces those diagnostics is that pass's concern, not the pipeline's.

Pipeline::Pipeline(std::vector<std::unique_ptr<Pass>> passes) : passes_{std::move(passes)} {}

PipelineResult Pipeline::run(Program& program) {
    PipelineResult result;
    bool failed{false};
    StringSet completed_passes;

    for (const auto& pass : passes_) {
        if (failed && !pass->run_after_failure()) {
            continue;
        }

        // Validate that all required predecessor passes have completed.
        if (!dependencies_satisfied(*pass, completed_passes, result)) {
            failed = true;
            continue;
        }

        const auto start = std::chrono::steady_clock::now();

        try {
            if (!pass->run(program, result)) {
                failed = true;
            }
            // Record the pass as having run whether or not it reported user
            // errors: its analysis/annotations remain available to dependent
            // passes (the linter, for instance, runs after a failed
            // type-check).  Only a pass that THREW (caught below) counts as
            // not-run, so a genuine ordering bug still trips the dependency
            // check rather than emitting a spurious "internal error" on every
            // program that fails type-checking.
            completed_passes.insert(pass->name());
        } catch (const std::exception& ex) {
            // Safety net for unexpected internal errors (e.g. std::logic_error, OOM).
            // If this fires, it indicates a bug in a pipeline pass.
            result.diagnostics.push_back(
                make_error_diagnostic(std::format("internal error: {}", ex.what())));
            failed = true;
        }

        const auto end = std::chrono::steady_clock::now();
        result.timings.push_back({
            .name = pass->name(),
            .duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start),
        });
    }

    return result;
}

} // namespace luma
