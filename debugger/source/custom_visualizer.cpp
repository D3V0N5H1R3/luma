#include "custom_visualizer.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

#include "diagnostic_log.hpp"
#include "json/json_helpers.hpp"

namespace luma::dap {

using luma::json::JsonValue;
using luma::json::try_extract_field;

namespace {

// Convert a glob-style pattern to a regex string.
// Only '*' is treated as a wildcard; all regex metacharacters are escaped.
std::string glob_to_regex(std::string_view pattern) {
    std::string result;

    for (const char ch : pattern) {
        if (ch == '*') {
            result += ".*";
        } else if (ch == '?' || ch == '.' || ch == '(' || ch == ')' || ch == '[' || ch == ']' ||
                   ch == '{' || ch == '}' || ch == '+' || ch == '^' || ch == '$' || ch == '|' ||
                   ch == '\\') {
            result += '\\';
            result += ch;
        } else {
            result += ch;
        }
    }

    return result;
}

// Parse visualizer rules from a JSON array, compiling glob patterns to regex.
std::vector<CustomVisualizer::CompiledRule>
parse_visualizer_rules(const JsonValue& root,
                       const std::function<void(std::string_view)>& log_error) {
    std::vector<CustomVisualizer::CompiledRule> rules;

    for (const auto& entry : root.as_array()) {
        if (!entry.is_object()) {
            continue;
        }

        VisualizerRule rule;

        auto type_pattern = try_extract_field<std::string>(entry, "typePattern");
        if (!type_pattern) {
            continue; // typePattern is required.
        }
        rule.type_pattern = std::move(*type_pattern);

        rule.display_template = entry.get_or<std::string>("displayTemplate", "");
        rule.summary_template = entry.get_or<std::string>("summaryTemplate", "");

        try {
            auto regex_str = glob_to_regex(rule.type_pattern);
            std::regex compiled(regex_str, std::regex_constants::nosubs);
            rules.push_back({.rule = std::move(rule), .pattern = std::move(compiled)});
        } catch (const std::regex_error& e) {
            log_error("invalid regex pattern for glob '" + rule.type_pattern + "': " + e.what());
            continue;
        }
    }

    return rules;
}

} // anonymous namespace

void CustomVisualizer::set_diagnostic_callback(DiagnosticCallback cb) {
    diagnostic_callback_ = std::move(cb);
}

void CustomVisualizer::log_error(std::string_view msg) const {
    report_or_log(diagnostic_callback_, std::string(msg), "custom_visualizer: ");
}

void CustomVisualizer::load(const std::string& config_path) {
    const std::ifstream file(config_path);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open visualizer config: " + config_path);
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    auto root = JsonValue::parse(content);

    if (!root.is_array()) {
        throw std::runtime_error("Visualizer config must be a JSON array");
    }

    auto new_rules = parse_visualizer_rules(root, [this](std::string_view msg) { log_error(msg); });

    {
        const std::scoped_lock lock(cache_mutex_);
        compiled_rules_ = std::move(new_rules);
        match_cache_.clear();
    }
}

std::optional<VisualizerRule> CustomVisualizer::find_rule(std::string_view type_name) const {
    const std::string key(type_name);

    const std::scoped_lock lock(cache_mutex_);

    if (const auto it = match_cache_.find(key); it != match_cache_.end()) {
        return it->second;
    }

    std::optional<VisualizerRule> result;

    for (const auto& entry : compiled_rules_) {
        if (std::regex_match(type_name.begin(), type_name.end(), entry.pattern)) {
            result = entry.rule;
            break;
        }
    }

    match_cache_.emplace(key, result);
    return result;
}

} // namespace luma::dap
