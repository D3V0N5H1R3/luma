#ifndef LUMA_DAP_BREAKPOINT_VALIDATOR_HPP
#define LUMA_DAP_BREAKPOINT_VALIDATOR_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Centralised breakpoint validation
// ─────────────────────────────────────────────────────────────────────────────
// Provides a single location for all breakpoint-related validation logic:
//   • Hit condition syntax validation and evaluation
//   • Condition expression compilation checks
//   • Log message template compilation checks
//   • Aggregated field validation that updates DAP Breakpoint responses
//
// Used by both BreakpointManager (response building) and DapServer handlers
// (set-time validation), eliminating the previous duplication.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>

#include "compiled_breakpoint.hpp"
#include "dap_types.hpp"

namespace luma::dap {

// ─── Breakpoint expression types ───

// Classifies a breakpoint expression for dispatch to the correct validator.
enum class BreakpointExpressionType {
    condition,
    hit_condition,
    log_message
};

// A breakpoint expression paired with its type, used to drive unified
// validation across condition, hit condition, and log message fields.
struct BreakpointExpression {
    std::string source;
    BreakpointExpressionType type;
};

// Validate a single breakpoint expression through the appropriate path
// (condition compilation, hit condition parsing, or log message compilation).
// Returns an empty string on success, or a human-readable error message.
[[nodiscard]] std::string validate_breakpoint_expression(const BreakpointExpression& expr,
                                                         int breakpoint_id,
                                                         CompiledBreakpointCache& cache);

// Collect the non-empty breakpoint expressions from a DAP Breakpoint.
[[nodiscard]] std::vector<BreakpointExpression>
collect_breakpoint_expressions(const Breakpoint& bp);

// ─── Hit condition validation ───

// Validate a hit condition string (e.g. ">= 5", "% 3", "10").
// Returns an empty string on success, or a human-readable error message.
[[nodiscard]] std::string validate_hit_condition(const std::string& hit_condition);

// Evaluate a hit condition against a hit count.
// Returns true if the condition is satisfied (the breakpoint should fire).
[[nodiscard]] bool evaluate_hit_condition(const std::string& hit_condition, int times_hit);

// ─── Breakpoint response helpers ───

// Append a hit-condition warning to a Breakpoint's message field if the
// hit condition is invalid.  Safe to call with an empty hit_condition.
void append_hit_condition_warning(Breakpoint& bp, const std::string& hit_condition);

// ─── Full field validation ───

// Validate all expression fields on a Breakpoint (condition, hit_condition,
// log_message) by compiling them through the cache.  On failure, sets
// bp.verified = false and populates bp.message / bp.reason with diagnostics.
//
// Call this at set-time so the editor can display compilation errors
// immediately rather than waiting for the breakpoint to be hit.
void validate_breakpoint_fields(Breakpoint& bp, CompiledBreakpointCache& cache);

} // namespace luma::dap

#endif // LUMA_DAP_BREAKPOINT_VALIDATOR_HPP
