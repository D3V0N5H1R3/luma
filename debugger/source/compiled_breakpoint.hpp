#ifndef LUMA_DAP_COMPILED_BREAKPOINT_HPP
#define LUMA_DAP_COMPILED_BREAKPOINT_HPP

// ─────────────────────────────────────────────────────────────────────────────
// Pre-Compiled Conditional Breakpoints
// ─────────────────────────────────────────────────────────────────────────────
// Compiles breakpoint condition expressions and log-message templates to
// bytecode once, at breakpoint-creation time, and caches the result keyed by
// breakpoint id.  The cache is used to *validate* breakpoint expressions when
// they are set — surfacing compile errors back to the editor — without
// re-parsing and re-compiling them on every change.
//
// The cache stores compiled bytecode only; it does not evaluate conditions.
// Runtime condition evaluation is performed separately by the debug execution
// engine on a scratch VM (see DebugExecutionEngine::evaluate_condition_safe),
// which can resolve the paused frame's local variables.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime/compiler/chunk.hpp"

namespace luma::dap {

// A pre-compiled breakpoint condition.
struct CompiledCondition {
    std::string source_expression;
    std::shared_ptr<const CompiledFunction> compiled_function;
    bool is_valid{false};
    std::string compile_error; // Non-empty if compilation failed.
};

// A pre-compiled log message template (for logpoints).
struct CompiledLogMessage {
    std::string template_text;

    // Each segment is either a literal string or a compiled expression.
    struct Segment {
        bool is_expression{false};
        std::string literal;
        std::shared_ptr<const CompiledFunction> compiled_expr;
    };

    std::vector<Segment> segments;
    bool is_valid{false};
    std::string compile_error;
};

class CompiledBreakpointCache {
public:
    CompiledBreakpointCache() = default;

    // Compile a condition expression and cache it.
    // Returns the compiled condition (which may have compile_error set).
    [[nodiscard]] const CompiledCondition& compile_condition(int breakpoint_id,
                                                             const std::string& expression);

    // Compile a log message template.
    [[nodiscard]] const CompiledLogMessage& compile_log_message(int breakpoint_id,
                                                                const std::string& template_text);

    // Invalidate a specific breakpoint's cached condition.
    void invalidate(int breakpoint_id);

    // Invalidate all cached conditions.
    void invalidate_all();

    // Check if a breakpoint has a cached condition.
    [[nodiscard]] bool has_condition(int breakpoint_id) const;

    // Get compilation statistics.
    [[nodiscard]] std::size_t cache_size() const noexcept {
        const std::shared_lock<std::shared_mutex> lock(cache_mutex_);
        return conditions_.size();
    }

private:
    // Compile an expression string into a CompiledFunction.
    [[nodiscard]] static std::optional<CompiledFunction> compile_expression(const std::string& expr,
                                                                            std::string& error_out);

    // Parse a log message template into literal and expression segments.
    // Expression segments store the raw expression text in the literal field.
    [[nodiscard]] static std::vector<CompiledLogMessage::Segment>
    parse_log_template_segments(const std::string& template_text);

    // Compile a single {expr} within a log template.  Returns true on success.
    static bool compile_template_expression(const std::string& expr,
                                            CompiledLogMessage::Segment& seg,
                                            CompiledLogMessage& msg);

    // Protects conditions_ and log_messages_.
    // compile_* and invalidate* are called from the DAP message thread;
    // has_condition/cache_size are read-only queries.
    // Writers (compile/invalidate) acquire a unique_lock; readers acquire a
    // shared_lock to allow concurrent reads.
    // Leaf-level lock — never held while acquiring any other mutex.
    mutable std::shared_mutex cache_mutex_; // GUARDED_BY: conditions_, log_messages_
    std::unordered_map<int, CompiledCondition> conditions_;    // GUARDED_BY(cache_mutex_)
    std::unordered_map<int, CompiledLogMessage> log_messages_; // GUARDED_BY(cache_mutex_)
};

} // namespace luma::dap

#endif // LUMA_DAP_COMPILED_BREAKPOINT_HPP
