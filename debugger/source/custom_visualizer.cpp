#include "custom_visualizer.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "diagnostic_log.hpp"
#include "json/json_helpers.hpp"

namespace luma::dap {

using luma::json::JsonValue;
using luma::json::try_extract_field;

namespace {

// Linear-time glob match supporting only '*' as a wildcard.
// Avoids regex compilation and eliminates backtracking risk (ReDoS).
bool glob_match(std::string_view pattern, std::string_view text) {
    std::size_t px = 0;
    std::size_t tx = 0;
    std::size_t star_px = std::string_view::npos;
    std::size_t star_tx = 0;

    while (tx < text.size()) {
        if (px < pattern.size() && pattern[px] == '*') {
            star_px = px++;
            star_tx = tx;
        } else if (px < pattern.size() && pattern[px] == text[tx]) {
            ++px;
            ++tx;
        } else if (star_px != std::string_view::npos) {
            px = star_px + 1;
            tx = ++star_tx;
        } else {
            return false;
        }
    }

    while (px < pattern.size() && pattern[px] == '*') {
        ++px;
    }

    return px == pattern.size();
}

// Parse visualizer rules from a JSON array.
std::vector<CustomVisualizer::CompiledRule>
parse_visualizer_rules(const JsonValue& root,
                       [[maybe_unused]] const std::function<void(std::string_view)>& log_error) {
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

        rules.push_back({.rule = std::move(rule)});
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
        if (glob_match(entry.rule.type_pattern, type_name)) {
            result = entry.rule;
            break;
        }
    }

    match_cache_.emplace(key, result);
    return result;
}

} // namespace luma::dap
