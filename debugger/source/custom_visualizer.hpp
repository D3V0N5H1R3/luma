#ifndef LUMA_DAP_CUSTOM_VISUALIZER_HPP
#define LUMA_DAP_CUSTOM_VISUALIZER_HPP

#include <functional>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "common/string_hash.hpp"

namespace luma::dap {

// A single visualizer rule loaded from a JSON config file.
// Matches a type name/pattern and provides display/summary templates.
struct VisualizerRule {
    std::string
        type_pattern; // Glob pattern matched against the type name (only '*' is a wildcard).
    std::string display_template; // When a rule matches, this string replaces the displayed value
    // verbatim.  Placeholder expansion (e.g. "{size}") is NOT supported.
    std::string summary_template; // Reserved: parsed from config but not currently applied.
};

// Callback used to report non-fatal diagnostic messages (e.g. invalid patterns).
using DiagnosticCallback = std::function<void(std::string_view)>;

// Loads custom value visualizer rules from a JSON config file
// and matches them against type names at display time.
class CustomVisualizer {
public:
    struct CompiledRule {
        VisualizerRule rule;
        std::regex pattern;
    };

    // Set a callback for non-fatal diagnostic messages.  If not set,
    // diagnostics are written to stderr as a fallback.
    void set_diagnostic_callback(DiagnosticCallback cb);

    // Load rules from a JSON config file.  The file must contain a
    // JSON array of objects with keys: typePattern, displayTemplate,
    // summaryTemplate.  Throws std::runtime_error on I/O failure.
    // Patterns are pre-compiled to regexes at load time.
    void load(const std::string& config_path);

    // Find the first rule whose type_pattern matches the given type name.
    // Returns std::nullopt if no rule matches.
    [[nodiscard]] std::optional<VisualizerRule> find_rule(std::string_view type_name) const;

    // Returns true if any rules have been loaded.
    [[nodiscard]] bool has_rules() const noexcept {
        return !compiled_rules_.empty();
    }

    // Clear cached match results.  Called automatically when rules are reloaded.
    void clear_cache() const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        match_cache_.clear();
    }

private:
    void log_error(std::string_view msg) const;

    DiagnosticCallback diagnostic_callback_;
    std::vector<CompiledRule> compiled_rules_;

    // Protects match_cache_ which may be accessed from multiple threads
    // (DAP request thread + evaluation hooks).
    mutable std::mutex cache_mutex_;

    // Cache of type-name → matched rule.  Keyed by type name string.
    // The same small set of type names (e.g. "integer", "string", "array<integer>")
    // is queried repeatedly during variable inspection, so caching avoids redundant
    // regex matching against the (typically < 20) compiled patterns.
    mutable StringMap<std::optional<VisualizerRule>> match_cache_;
};

} // namespace luma::dap

#endif // LUMA_DAP_CUSTOM_VISUALIZER_HPP
