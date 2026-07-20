#ifndef LUMA_LSP_CONFIG_HPP
#define LUMA_LSP_CONFIG_HPP

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "json/json_helpers.hpp"

namespace luma::lsp {

using luma::json::JsonValue;

// Immutable configuration snapshot. All fields are plain (non-atomic)
// because a snapshot is never mutated after construction — readers
// always obtain a complete, consistent copy via LspConfig::get().
struct ConfigSnapshot {
    bool inlay_hints_enabled{true};
    bool code_lens_enabled{true};
    int analysis_debounce_ms{50};
    int analysis_timeout_ms{10000}; // 10 s default
};

// Centralised configuration for the LSP server.
//
// Uses a copy-on-write snapshot so that readers (analysis worker, request
// handlers) always observe a fully consistent set of values — there is no
// window in which some fields come from the old config and others from the
// new one.  Writers atomically publish a new snapshot under update_mutex_;
// readers call get() which briefly acquires update_mutex_ to copy the
// shared_ptr, then access snapshot fields lock-free on the returned pointer.
struct LspConfig {
    // Return the current config snapshot. Callers may hold the returned
    // shared_ptr across long operations; they will see a consistent view
    // even if the config is updated concurrently.
    [[nodiscard]] std::shared_ptr<const ConfigSnapshot> get() const {
        const std::lock_guard lock(update_mutex_);
        return snapshot_;
    }

    // Apply settings from an LSP didChangeConfiguration payload.
    // Expects the "luma" settings section (or the root settings object).
    void apply_lsp_settings(const JsonValue& settings) {
        if (!settings.is_object()) {
            return;
        }

        const auto& section = settings.has("luma") ? settings["luma"] : settings;
        if (!section.is_object()) {
            return;
        }

        apply_section(section);
    }

    // Apply settings from a luma.json project config file.
    void apply_project_config(const JsonValue& config) {
        if (!config.is_object()) {
            return;
        }

        apply_section(config);
    }

private:
    mutable std::mutex update_mutex_;
    std::shared_ptr<const ConfigSnapshot> snapshot_{std::make_shared<ConfigSnapshot>()};

    // Shared implementation for reading configuration fields from a JSON object.
    // Builds a new snapshot from the current one and publishes it atomically.
    void apply_section(const JsonValue& section) {
        const std::lock_guard lock(update_mutex_);

        // Copy current values as the starting point so that fields not
        // mentioned in the incoming JSON retain their previous values.
        ConfigSnapshot next{*snapshot_};

        if (const auto& ih = section.get("inlayHints"); ih.is_object()) {
            next.inlay_hints_enabled = ih.get_or<bool>("enabled", next.inlay_hints_enabled);
        }

        if (const auto& cl = section.get("codeLens"); cl.is_object()) {
            next.code_lens_enabled = cl.get_or<bool>("enabled", next.code_lens_enabled);
        }

        if (auto ms = luma::json::try_extract_field<int>(section, "analysisDebounceMs");
            ms && *ms >= 0 && *ms <= 5000) {
            next.analysis_debounce_ms = *ms;
        }

        if (auto ms = luma::json::try_extract_field<int>(section, "analysisTimeoutMs");
            ms && *ms >= 100 && *ms <= 60000) {
            next.analysis_timeout_ms = *ms;
        }

        snapshot_ = std::make_shared<const ConfigSnapshot>(std::move(next));
    }
};

// ──────────── Code action kind constants ────────────
// LSP CodeActionKind string identifiers, centralised to avoid repetition.
namespace code_action_kind {

inline constexpr std::string_view k_quickfix = "quickfix";
inline constexpr std::string_view k_refactor_rewrite = "refactor.rewrite";
inline constexpr std::string_view k_refactor_extract_variable = "refactor.extract.variable";
inline constexpr std::string_view k_refactor_extract_function = "refactor.extract.function";
inline constexpr std::string_view k_refactor_inline = "refactor.inline";

} // namespace code_action_kind

} // namespace luma::lsp

#endif // LUMA_LSP_CONFIG_HPP
