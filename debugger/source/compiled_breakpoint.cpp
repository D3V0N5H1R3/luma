#include "compiled_breakpoint.hpp"

#include <shared_mutex>

#include "expression_compiler.hpp"

namespace luma::dap {

// ─── Compilation ───

std::optional<CompiledFunction>
CompiledBreakpointCache::compile_expression(const std::string& expr, std::string& error_out) {
    return compile_expression_direct(expr, error_out);
}

const CompiledCondition& CompiledBreakpointCache::compile_condition(int breakpoint_id,
                                                                    const std::string& expression) {
    const std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    auto it = conditions_.find(breakpoint_id);
    if (it != conditions_.end() && it->second.source_expression == expression) {
        return it->second;
    }

    CompiledCondition cond;
    cond.source_expression = expression;

    std::string error;
    auto compiled = compile_expression(expression, error);
    if (compiled) {
        cond.compiled_function = std::make_shared<const CompiledFunction>(std::move(*compiled));
        cond.is_valid = true;
    } else {
        cond.is_valid = false;
        cond.compile_error = std::move(error);
    }

    auto [inserted, _] = conditions_.insert_or_assign(breakpoint_id, std::move(cond));
    return inserted->second;
}

// ─── Log template helpers ───

std::vector<LogTemplatePart> parse_log_template(std::string_view template_text) {
    std::vector<LogTemplatePart> parts;
    std::string literal;
    std::size_t pos = 0;

    const auto flush_literal = [&parts, &literal] {
        if (!literal.empty()) {
            parts.push_back(LogTemplatePart{.is_expression = false, .text = std::move(literal)});
            literal.clear();
        }
    };

    while (pos < template_text.size()) {
        const char c = template_text[pos];

        if (c == '\\' && pos + 1 < template_text.size() && template_text[pos + 1] == '{') {
            literal += '{';
            pos += 2;
            continue;
        }

        if (c != '{') {
            literal += c;
            ++pos;
            continue;
        }

        const auto close = template_text.find('}', pos + 1);

        if (close == std::string_view::npos) {
            literal.append(template_text.substr(pos));
            break;
        }

        flush_literal();
        parts.push_back(
            LogTemplatePart{.is_expression = true,
                            .text = std::string{template_text.substr(pos + 1, close - pos - 1)}});
        pos = close + 1;
    }

    flush_literal();
    return parts;
}

std::vector<CompiledLogMessage::Segment>
CompiledBreakpointCache::parse_log_template_segments(const std::string& template_text) {
    std::vector<CompiledLogMessage::Segment> segments;

    // Expression segments store the raw expression text in `literal` until
    // compile_log_message() compiles them.
    for (auto& part : parse_log_template(template_text)) {
        CompiledLogMessage::Segment seg;
        seg.is_expression = part.is_expression;
        seg.literal = std::move(part.text);
        segments.push_back(std::move(seg));
    }

    return segments;
}

bool CompiledBreakpointCache::compile_template_expression(const std::string& expr,
                                                          CompiledLogMessage::Segment& seg,
                                                          CompiledLogMessage& msg) {
    std::string error;
    auto compiled = compile_expression(expr, error);

    if (compiled) {
        seg.compiled_expr = std::make_shared<const CompiledFunction>(std::move(*compiled));
        return true;
    }

    msg.is_valid = false;
    msg.compile_error = "Error in expression '" + expr + "': " + error;
    return false;
}

// ─── Compilation ───

const CompiledLogMessage&
CompiledBreakpointCache::compile_log_message(int breakpoint_id, const std::string& template_text) {
    const std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    auto it = log_messages_.find(breakpoint_id);
    if (it != log_messages_.end() && it->second.template_text == template_text) {
        return it->second;
    }

    CompiledLogMessage msg;
    msg.template_text = template_text;
    msg.is_valid = true;

    msg.segments = parse_log_template_segments(template_text);

    for (auto& seg : msg.segments) {
        if (seg.is_expression) {
            auto expr = std::move(seg.literal);
            seg.literal.clear();
            compile_template_expression(expr, seg, msg);
        }
    }

    auto [inserted, _] = log_messages_.insert_or_assign(breakpoint_id, std::move(msg));
    return inserted->second;
}

// ─── Cache Management ───

void CompiledBreakpointCache::invalidate(int breakpoint_id) {
    const std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    conditions_.erase(breakpoint_id);
    log_messages_.erase(breakpoint_id);
}

void CompiledBreakpointCache::invalidate_all() {
    const std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    conditions_.clear();
    log_messages_.clear();
}

bool CompiledBreakpointCache::has_condition(int breakpoint_id) const {
    const std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    return conditions_.contains(breakpoint_id);
}

} // namespace luma::dap
