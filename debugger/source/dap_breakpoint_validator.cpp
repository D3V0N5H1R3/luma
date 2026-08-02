#include "dap_breakpoint_validator.hpp"

#include <array>
#include <format>
#include <string_view>
#include <vector>

namespace luma::dap {

namespace {

// Trim leading and trailing whitespace from a string.
[[nodiscard]] std::string trim(std::string_view s) {
    const auto start = s.find_first_not_of(' ');

    if (start == std::string_view::npos) {
        return {};
    }

    const auto end = s.find_last_not_of(' ');
    return std::string(s.substr(start, end - start + 1));
}

// ─── Hit condition operator table ───
// Ordered longest-prefix-first so ">=" matches before ">", etc.

struct HitConditionOp {
    std::string_view prefix;
    bool (*evaluate)(int times_hit, int target);
};

constexpr std::array<HitConditionOp, 6> k_hit_condition_ops = {{
    {.prefix = ">=", .evaluate = +[](int h, int t) -> bool { return h >= t; }},
    {.prefix = "<=", .evaluate = +[](int h, int t) -> bool { return h <= t; }},
    {.prefix = "==", .evaluate = +[](int h, int t) -> bool { return h == t; }},
    {.prefix = ">", .evaluate = +[](int h, int t) -> bool { return h > t; }},
    {.prefix = "<", .evaluate = +[](int h, int t) -> bool { return h < t; }},
    {.prefix = "%", .evaluate = +[](int h, int t) -> bool { return t > 0 && (h % t) == 0; }},
}};

struct ParsedHitCondition {
    const HitConditionOp* op{nullptr}; // nullptr for bare number (implicit ==).
    int value{0};
};

// Parse a trimmed hit condition into operator + value.
// Throws std::invalid_argument or std::out_of_range on parse failure.
ParsedHitCondition parse_hit_condition(std::string_view trimmed) {
    for (const auto& entry : k_hit_condition_ops) {
        if (trimmed.starts_with(entry.prefix)) {
            return {.op = &entry,
                    .value = std::stoi(std::string(trimmed.substr(entry.prefix.size())))};
        }
    }

    // Bare number — implicit ==.
    return {.op = nullptr, .value = std::stoi(std::string(trimmed))};
}

// ─── Consolidated hit condition parsing ───

enum class HitConditionStatus {
    ok,
    empty,
    not_a_number,
    out_of_range,
};

struct HitConditionResult {
    HitConditionStatus status{HitConditionStatus::empty};
    ParsedHitCondition parsed;
};

// Trim, validate, and parse a hit condition string in one place.
// Handles empty/whitespace input and catches parse exceptions.
[[nodiscard]] HitConditionResult try_parse_hit_condition(const std::string& hc) {
    if (hc.empty()) {
        return {.status = HitConditionStatus::empty, .parsed = {}};
    }

    const auto trimmed = trim(hc);

    if (trimmed.empty()) {
        return {.status = HitConditionStatus::empty, .parsed = {}};
    }

    try {
        return {.status = HitConditionStatus::ok, .parsed = parse_hit_condition(trimmed)};
    } catch (const std::out_of_range&) {
        return {.status = HitConditionStatus::out_of_range, .parsed = {}};
    } catch (const std::invalid_argument&) {
        return {.status = HitConditionStatus::not_a_number, .parsed = {}};
    }
}

} // namespace

// ─── Hit condition helpers ───

std::string validate_hit_condition(const std::string& hc) {
    const auto [status, parsed] = try_parse_hit_condition(hc);

    switch (status) {
        case HitConditionStatus::ok:
            if (parsed.op != nullptr && parsed.op->prefix == "%" && parsed.value <= 0) {
                return std::format("Modulo value must be positive: '{}'", hc);
            }

            return "";
        case HitConditionStatus::empty:
            if (!hc.empty()) {
                return std::format("Invalid hit condition: '{}'", hc);
            }

            return "";
        case HitConditionStatus::not_a_number:
            return std::format("Invalid hit condition (not a number): '{}'", hc);
        case HitConditionStatus::out_of_range:
            return std::format("Hit condition value out of range: '{}'", hc);
    }

    return "";
}

bool evaluate_hit_condition(const std::string& hc, int times_hit) {
    const auto [status, parsed] = try_parse_hit_condition(hc);

    if (status != HitConditionStatus::ok) {
        return status == HitConditionStatus::empty;
    }

    if (parsed.op != nullptr) {
        return parsed.op->evaluate(times_hit, parsed.value);
    }

    // Bare number — implicit ==.
    return times_hit == parsed.value;
}

// ─── Breakpoint response helpers ───

void append_hit_condition_warning(Breakpoint& bp, const std::string& hit_condition) {
    if (hit_condition.empty()) {
        return;
    }

    auto err = validate_hit_condition(hit_condition);

    if (!err.empty()) {
        if (!bp.message.empty()) {
            bp.message += "; ";
        }

        bp.message += err;
    }
}

// ─── Unified expression validation ───

std::string validate_breakpoint_expression(const BreakpointExpression& expr, int breakpoint_id,
                                           CompiledBreakpointCache& cache) {
    switch (expr.type) {
        case BreakpointExpressionType::condition: {
            const auto& cond = cache.compile_condition(breakpoint_id, expr.source);
            return cond.compile_error;
        }
        case BreakpointExpressionType::hit_condition:
            return validate_hit_condition(expr.source);
        case BreakpointExpressionType::log_message: {
            const auto& log = cache.compile_log_message(breakpoint_id, expr.source);
            return log.compile_error;
        }
    }

    return "";
}

std::vector<BreakpointExpression> collect_breakpoint_expressions(const Breakpoint& bp) {
    std::vector<BreakpointExpression> expressions;

    if (!bp.condition.empty()) {
        expressions.push_back(
            {.source = bp.condition, .type = BreakpointExpressionType::condition});
    }

    if (!bp.hit_condition.empty()) {
        expressions.push_back(
            {.source = bp.hit_condition, .type = BreakpointExpressionType::hit_condition});
    }

    if (!bp.log_message.empty()) {
        expressions.push_back(
            {.source = bp.log_message, .type = BreakpointExpressionType::log_message});
    }

    return expressions;
}

// ─── Label for error messages ───

namespace {

[[nodiscard]] std::string expression_type_label(BreakpointExpressionType type) {
    switch (type) {
        case BreakpointExpressionType::condition:
            return "Condition";
        case BreakpointExpressionType::hit_condition:
            return "Hit condition";
        case BreakpointExpressionType::log_message:
            return "Log message";
    }

    return "Expression";
}

} // namespace

// ─── Full field validation ───

void validate_breakpoint_fields(Breakpoint& bp, CompiledBreakpointCache& cache) {
    const auto expressions = collect_breakpoint_expressions(bp);

    for (const auto& expr : expressions) {
        auto error = validate_breakpoint_expression(expr, bp.id, cache);

        if (error.empty()) {
            continue;
        }

        bp.verified = false;

        const auto label = expression_type_label(expr.type);
        bp.reason = label + " validation failed: " + error;

        if (!bp.message.empty()) {
            bp.message += "; ";
        }

        bp.message += label + " error: " + error;
    }
}

} // namespace luma::dap
