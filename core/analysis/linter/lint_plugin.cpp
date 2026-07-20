#include "analysis/linter/lint_plugin.hpp"

#include <algorithm>
#include <utility>

namespace luma {

// ─────────── LintPlugin default implementations ───────────

void LintPlugin::check_expression(const Expression& /*expr*/,
                                  std::vector<LintFinding>& /*findings*/) const {
    // No-op by default — plugins override only the hooks they need.
}

void LintPlugin::check_statement(const Statement& /*stmt*/,
                                 std::vector<LintFinding>& /*findings*/) const {
    // No-op by default.
}

void LintPlugin::check_declaration(const Declaration& /*decl*/,
                                   std::vector<LintFinding>& /*findings*/) const {
    // No-op by default.
}

// ─────────── LintPluginRegistry ───────────

void LintPluginRegistry::register_plugin(std::unique_ptr<LintPlugin> plugin) {
    entries_.push_back({.plugin = std::move(plugin), .enabled = true});
    cache_dirty_ = true;
}

std::size_t LintPluginRegistry::size() const {
    return entries_.size();
}

std::vector<LintPluginRegistry::PluginEntry>::const_iterator
LintPluginRegistry::find_entry(std::string_view id) const {
    return std::ranges::find_if(
        entries_, [&](const PluginEntry& entry) { return entry.plugin->rule_info().id == id; });
}

std::vector<LintPluginRegistry::PluginEntry>::iterator
LintPluginRegistry::find_entry(std::string_view id) {
    // Delegate to the const overload so the lookup predicate lives in one place,
    // then recover the mutable iterator (valid for a contiguous vector).
    const auto it = std::as_const(*this).find_entry(id);
    return entries_.begin() + (it - entries_.cbegin());
}

const LintPlugin* LintPluginRegistry::find_by_id(std::string_view id) const {
    const auto it = find_entry(id);
    return it != entries_.end() ? it->plugin.get() : nullptr;
}

bool LintPluginRegistry::set_enabled(std::string_view id, bool enabled) {
    const auto it = find_entry(id);

    if (it == entries_.end()) {
        return false;
    }

    it->enabled = enabled;
    cache_dirty_ = true;
    return true;
}

bool LintPluginRegistry::is_enabled(std::string_view id) const {
    const auto it = find_entry(id);
    return it != entries_.end() && it->enabled;
}

std::span<const LintPlugin* const> LintPluginRegistry::enabled_plugins() const {
    if (cache_dirty_) {
        rebuild_enabled_cache();
    }
    return enabled_cache_;
}

void LintPluginRegistry::rebuild_enabled_cache() const {
    enabled_cache_.clear();
    enabled_cache_.reserve(entries_.size());

    for (const auto& entry : entries_) {
        if (entry.enabled) {
            enabled_cache_.push_back(entry.plugin.get());
        }
    }

    cache_dirty_ = false;
}

} // namespace luma
