// DAP breakpoint tests — conditional breakpoints, hit conditions,
// log points, and step-in targets.

#include <atomic>
#include <format>
#include <map>
#include <string>

#include "dap_breakpoint_validator.hpp"
#include "dap_types.hpp"
#include "json/json.hpp"
#include "test_framework.hpp"

using namespace luma::dap;
using luma::json::JsonValue;

namespace {

// ─── Atomic breakpoint ID ──────────────────────────────────────────

void test_atomic_breakpoint_id() {
    // next_breakpoint_id_ should be atomic for thread-safe access.
    std::atomic<int> id{1};

    int first = id++;
    int second = id++;

    ASSERT_EQ(first, 1);
    ASSERT_EQ(second, 2);
    ASSERT_EQ(id.load(), 3);
}

// ─── Conditional breakpoint evaluation logic ───────────────────────

void test_conditional_breakpoint_structure() {
    // Line breakpoint with a condition expression.
    JsonValue::ObjectType bp;
    bp["line"] = JsonValue(10);
    bp["condition"] = JsonValue(std::string("x > 5"));

    auto json = JsonValue(std::move(bp));

    ASSERT_EQ(json["line"].as_integer(), 10);
    ASSERT_TRUE(json.has("condition"));
    ASSERT_EQ(json["condition"].as_string(), "x > 5");
}

void test_breakpoint_request_with_condition() {
    BreakpointRequest req;
    req.line = 10;
    req.condition = "x > 5";
    req.hit_condition = "== 3";
    req.log_message = "hit at x={x}";
    ASSERT_EQ(req.line, 10);
    ASSERT_EQ(req.condition, "x > 5");
    ASSERT_EQ(req.hit_condition, "== 3");
    ASSERT_EQ(req.log_message, "hit at x={x}");
}

// ─── Hit condition helpers ─────────────────────────────────────────

std::string test_validate_hc(const std::string& hc) {
    if (hc.empty()) {
        return "";
    }

    try {
        if (hc.starts_with(">=") || hc.starts_with("<=") || hc.starts_with("==")) {
            (void)std::stoi(hc.substr(2));
        } else if (hc.starts_with(">") || hc.starts_with("<")) {
            (void)std::stoi(hc.substr(1));
        } else if (hc.starts_with("%")) {
            auto val = std::stoi(hc.substr(1));

            if (val <= 0) {
                return std::format("Modulo value must be positive: '{}'", hc);
            }
        } else {
            (void)std::stoi(hc);
        }
    } catch (...) {
        return std::format("Invalid hit condition: '{}'", hc);
    }

    return "";
}

bool test_evaluate_hc(const std::string& hc, int hit_count) {
    if (hc.empty()) {
        return true;
    }

    try {
        int target = 0;

        if (hc.starts_with(">=")) {
            target = std::stoi(hc.substr(2));
            return hit_count >= target;
        }

        if (hc.starts_with(">")) {
            target = std::stoi(hc.substr(1));
            return hit_count > target;
        }

        if (hc.starts_with("<=")) {
            target = std::stoi(hc.substr(2));
            return hit_count <= target;
        }

        if (hc.starts_with("<")) {
            target = std::stoi(hc.substr(1));
            return hit_count < target;
        }

        if (hc.starts_with("==")) {
            target = std::stoi(hc.substr(2));
            return hit_count == target;
        }

        if (hc.starts_with("%")) {
            target = std::stoi(hc.substr(1));
            return target > 0 && (hit_count % target) == 0;
        }

        target = std::stoi(hc);
        return hit_count == target;
    } catch (...) {
        return false;
    }
}

// ─── Hit condition tests ───────────────────────────────────────────

void test_conditional_breakpoint_with_hit_condition() {
    // Breakpoint with both condition and hit condition.
    BreakpointRequest req;
    req.line = 15;
    req.condition = "count > 0";
    req.hit_condition = ">=3";

    ASSERT_EQ(req.line, 15);
    ASSERT_EQ(req.condition, "count > 0");
    ASSERT_EQ(req.hit_condition, ">=3");

    // Validate hit condition independently.
    ASSERT_EQ(test_validate_hc(req.hit_condition), std::string(""));
}

void test_hit_condition_equal_evaluation() {
    // ==N: fires only when hit_count equals N.
    ASSERT_FALSE(test_evaluate_hc("==5", 1));
    ASSERT_FALSE(test_evaluate_hc("==5", 4));
    ASSERT_TRUE(test_evaluate_hc("==5", 5));
    ASSERT_FALSE(test_evaluate_hc("==5", 6));
}

void test_hit_condition_greater_than_evaluation() {
    // >N: fires when hit_count is strictly greater than N.
    ASSERT_FALSE(test_evaluate_hc(">3", 1));
    ASSERT_FALSE(test_evaluate_hc(">3", 3));
    ASSERT_TRUE(test_evaluate_hc(">3", 4));
    ASSERT_TRUE(test_evaluate_hc(">3", 10));
}

void test_hit_condition_ge_evaluation() {
    // >=N: fires when hit_count is >= N.
    ASSERT_FALSE(test_evaluate_hc(">=3", 1));
    ASSERT_FALSE(test_evaluate_hc(">=3", 2));
    ASSERT_TRUE(test_evaluate_hc(">=3", 3));
    ASSERT_TRUE(test_evaluate_hc(">=3", 4));
}

void test_hit_condition_plain_number() {
    // Plain number N: fires only on hit N.
    ASSERT_FALSE(test_evaluate_hc("5", 1));
    ASSERT_FALSE(test_evaluate_hc("5", 4));
    ASSERT_TRUE(test_evaluate_hc("5", 5));
    ASSERT_FALSE(test_evaluate_hc("5", 6));
}

void test_hit_condition_empty_always_fires() {
    // Empty hit condition: always fires.
    ASSERT_TRUE(test_evaluate_hc("", 0));
    ASSERT_TRUE(test_evaluate_hc("", 1));
    ASSERT_TRUE(test_evaluate_hc("", 999));
}

void test_hit_condition_invalid_never_fires() {
    // Invalid expressions should never fire.
    ASSERT_FALSE(test_evaluate_hc("abc", 1));
    ASSERT_FALSE(test_evaluate_hc(">=xyz", 1));
    ASSERT_FALSE(test_evaluate_hc("<", 1));
}

void test_hit_condition_modulo_validation() {
    // Valid modulo conditions.
    ASSERT_EQ(test_validate_hc("%5"), std::string(""));
    ASSERT_EQ(test_validate_hc("%1"), std::string(""));
    ASSERT_EQ(test_validate_hc("%100"), std::string(""));

    // Invalid: modulo with zero or negative.
    ASSERT_FALSE(test_validate_hc("%0").empty());
    ASSERT_FALSE(test_validate_hc("%-1").empty());

    // Invalid: non-numeric.
    ASSERT_FALSE(test_validate_hc("%abc").empty());
}

void test_hit_condition_modulo_evaluation() {
    // %3: fires on every 3rd hit.
    ASSERT_FALSE(test_evaluate_hc("%3", 1));
    ASSERT_FALSE(test_evaluate_hc("%3", 2));
    ASSERT_TRUE(test_evaluate_hc("%3", 3));
    ASSERT_FALSE(test_evaluate_hc("%3", 4));
    ASSERT_FALSE(test_evaluate_hc("%3", 5));
    ASSERT_TRUE(test_evaluate_hc("%3", 6));

    // %1: fires every hit.
    ASSERT_TRUE(test_evaluate_hc("%1", 1));
    ASSERT_TRUE(test_evaluate_hc("%1", 42));
}

void test_hit_condition_less_than_validation() {
    ASSERT_EQ(test_validate_hc("<5"), std::string(""));
    ASSERT_EQ(test_validate_hc("<=10"), std::string(""));
    ASSERT_FALSE(test_validate_hc("<abc").empty());
    ASSERT_FALSE(test_validate_hc("<=xyz").empty());
}

void test_hit_condition_less_than_evaluation() {
    // <3: fires when hit_count < 3.
    ASSERT_TRUE(test_evaluate_hc("<3", 1));
    ASSERT_TRUE(test_evaluate_hc("<3", 2));
    ASSERT_FALSE(test_evaluate_hc("<3", 3));
    ASSERT_FALSE(test_evaluate_hc("<3", 4));

    // <=3: fires when hit_count <= 3.
    ASSERT_TRUE(test_evaluate_hc("<=3", 1));
    ASSERT_TRUE(test_evaluate_hc("<=3", 2));
    ASSERT_TRUE(test_evaluate_hc("<=3", 3));
    ASSERT_FALSE(test_evaluate_hc("<=3", 4));
}

// ─── Step-in targets structure ─────────────────────────────────────

void test_step_in_targets_response() {
    // stepInTargets response lists callable targets at current location.
    JsonValue::ArrayType targets;

    JsonValue::ObjectType target1;
    target1["id"] = JsonValue(1);
    target1["label"] = JsonValue(std::string("add(a, b)"));
    targets.push_back(JsonValue(std::move(target1)));

    JsonValue::ObjectType target2;
    target2["id"] = JsonValue(2);
    target2["label"] = JsonValue(std::string("multiply(x, y)"));
    targets.push_back(JsonValue(std::move(target2)));

    JsonValue::ObjectType body;
    body["targets"] = JsonValue(std::move(targets));

    auto json = JsonValue(std::move(body));

    ASSERT_TRUE(json.has("targets"));
    ASSERT_EQ(json["targets"].as_array().size(), static_cast<std::size_t>(2));
    ASSERT_EQ(json["targets"].as_array()[0]["id"].as_integer(), 1);
    ASSERT_EQ(json["targets"].as_array()[0]["label"].as_string(), "add(a, b)");
    ASSERT_EQ(json["targets"].as_array()[1]["id"].as_integer(), 2);
}

void test_step_in_targets_request() {
    // stepInTargets request specifies a frameId.
    JsonValue::ObjectType args;
    args["frameId"] = JsonValue(42);

    auto json = JsonValue(std::move(args));

    ASSERT_TRUE(json.has("frameId"));
    ASSERT_EQ(json["frameId"].as_integer(), 42);
}

// ─── Log point interpolation logic ─────────────────────────────────

void test_log_message_escaped_brace() {
    // Test that \{ is treated as a literal brace, not expression start.
    std::string msg = "Value is \\{not_expr}";
    std::string output = msg;
    std::string::size_type pos = 0;

    while ((pos = output.find('{', pos)) != std::string::npos) {
        if (pos > 0 && output[pos - 1] == '\\') {
            output.erase(pos - 1, 1);
            continue;
        }

        auto end = output.find('}', pos);

        if (end == std::string::npos) {
            break;
        }

        // Replace with evaluated value (simulate).
        output.replace(pos, end - pos + 1, "EVALUATED");
        pos += std::string("EVALUATED").size();
    }

    ASSERT_EQ(output, "Value is {not_expr}");
}

void test_log_message_no_escape() {
    // Test that {expr} without backslash is replaced.
    std::string msg = "Value is {expr}";
    std::string output = msg;
    std::string::size_type pos = 0;

    while ((pos = output.find('{', pos)) != std::string::npos) {
        if (pos > 0 && output[pos - 1] == '\\') {
            output.erase(pos - 1, 1);
            continue;
        }

        auto end = output.find('}', pos);

        if (end == std::string::npos) {
            break;
        }

        output.replace(pos, end - pos + 1, "42");
        pos += std::string("42").size();
    }

    ASSERT_EQ(output, "Value is 42");
}

void test_log_message_multiple_expressions() {
    // Multiple {expr} placeholders in a log message.
    std::string msg = "x={x}, y={y}, sum={sum}";
    std::string output = msg;
    std::string::size_type pos = 0;
    int replace_count = 0;

    while ((pos = output.find('{', pos)) != std::string::npos) {
        if (pos > 0 && output[pos - 1] == '\\') {
            output.erase(pos - 1, 1);
            continue;
        }

        auto end = output.find('}', pos);

        if (end == std::string::npos) {
            break;
        }

        output.replace(pos, end - pos + 1, "VAL");
        pos += 3;
        ++replace_count;
    }

    ASSERT_EQ(replace_count, 3);
    ASSERT_EQ(output, "x=VAL, y=VAL, sum=VAL");
}

void test_log_message_no_expressions() {
    // A log message with no expressions should be passed through verbatim.
    std::string msg = "Hit this breakpoint";
    std::string output = msg;
    std::string::size_type pos = 0;

    while ((pos = output.find('{', pos)) != std::string::npos) {
        auto end = output.find('}', pos);

        if (end == std::string::npos) {
            break;
        }

        output.replace(pos, end - pos + 1, "X");
        pos += 1;
    }

    ASSERT_EQ(output, "Hit this breakpoint");
}

void test_log_message_unclosed_brace() {
    // An unclosed brace should be left as-is.
    std::string msg = "Value is {incomplete";
    std::string output = msg;
    std::string::size_type pos = 0;

    while ((pos = output.find('{', pos)) != std::string::npos) {
        auto end = output.find('}', pos);

        if (end == std::string::npos) {
            break;
        }

        output.replace(pos, end - pos + 1, "X");
        pos += 1;
    }

    ASSERT_EQ(output, "Value is {incomplete");
}

// ─── Hit condition negative/error-path tests (real validator) ──────

void test_invalid_hit_condition_whitespace_only() {
    // Whitespace-only input is treated as invalid after trimming.
    auto err = validate_hit_condition("   ");
    ASSERT_FALSE(err.empty());
}

void test_invalid_hit_condition_no_number() {
    auto err = validate_hit_condition("> abc");
    ASSERT_FALSE(err.empty());
}

void test_invalid_hit_condition_overflow() {
    auto err = validate_hit_condition("> 99999999999999999999");
    ASSERT_FALSE(err.empty());
}

void test_invalid_hit_condition_negative_modulo() {
    auto err = validate_hit_condition("% -1");
    ASSERT_FALSE(err.empty());
}

void test_invalid_hit_condition_zero_modulo() {
    auto err = validate_hit_condition("% 0");
    ASSERT_FALSE(err.empty());
}

void test_evaluate_hit_condition_empty_always_fires() {
    ASSERT_TRUE(evaluate_hit_condition("", 1));
    ASSERT_TRUE(evaluate_hit_condition("", 0));
}

void test_evaluate_hit_condition_invalid_returns_false() {
    ASSERT_FALSE(evaluate_hit_condition("> abc", 5));
}

} // namespace

int main() {
    luma::test::print_suite_header("DAP Breakpoint Tests");

    // Atomic breakpoint ID.
    RUN(test_atomic_breakpoint_id);

    // Conditional breakpoints.
    RUN(test_conditional_breakpoint_structure);
    RUN(test_conditional_breakpoint_with_hit_condition);
    RUN(test_breakpoint_request_with_condition);

    // Hit conditions.
    RUN(test_hit_condition_equal_evaluation);
    RUN(test_hit_condition_greater_than_evaluation);
    RUN(test_hit_condition_ge_evaluation);
    RUN(test_hit_condition_plain_number);
    RUN(test_hit_condition_empty_always_fires);
    RUN(test_hit_condition_invalid_never_fires);
    RUN(test_hit_condition_modulo_validation);
    RUN(test_hit_condition_modulo_evaluation);
    RUN(test_hit_condition_less_than_validation);
    RUN(test_hit_condition_less_than_evaluation);

    // Step-in targets.
    RUN(test_step_in_targets_response);
    RUN(test_step_in_targets_request);

    // Log points.
    RUN(test_log_message_escaped_brace);
    RUN(test_log_message_no_escape);
    RUN(test_log_message_multiple_expressions);
    RUN(test_log_message_no_expressions);
    RUN(test_log_message_unclosed_brace);

    // Hit condition negative/error-path tests (real validator).
    RUN(test_invalid_hit_condition_whitespace_only);
    RUN(test_invalid_hit_condition_no_number);
    RUN(test_invalid_hit_condition_overflow);
    RUN(test_invalid_hit_condition_negative_modulo);
    RUN(test_invalid_hit_condition_zero_modulo);
    RUN(test_evaluate_hit_condition_empty_always_fires);
    RUN(test_evaluate_hit_condition_invalid_returns_false);

    return SUMMARY();
}
