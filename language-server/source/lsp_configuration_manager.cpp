#include "lsp_configuration_manager.hpp"

#include <format>

namespace luma::lsp {

// ─── Config access ───

LspConfig& ConfigurationManager::config() noexcept {
    return config_;
}

const LspConfig& ConfigurationManager::config() const noexcept {
    return config_;
}

// ─── Client capabilities ───

bool ConfigurationManager::snippet_support() const noexcept {
    return snippet_support_;
}

// Extracts the snippetSupport flag from deeply nested client capabilities.
// Uses the member API: get() returns a null JsonValue for missing keys,
// and get_or<bool>() safely returns the default for non-object values,
// so the chain naturally handles any absent intermediate level.
[[nodiscard]] static bool extract_snippet_support(const JsonValue& params) {
    const auto& caps = params.get("capabilities");
    const auto& td = caps.get("textDocument");
    const auto& comp = td.get("completion");
    const auto& item = comp.get("completionItem");
    return item.get_or<bool>("snippetSupport", false);
}

void ConfigurationManager::detect_client_capabilities(const JsonValue& params,
                                                      const LogCallback& log) {
    try {
        snippet_support_ = extract_snippet_support(params);
    } catch (const std::exception& e) {
        // Capability negotiation is best-effort — malformed or
        // unexpected JSON in the client capabilities is non-fatal.
        // The server falls back to conservative defaults (e.g. no
        // snippet support) when detection fails.
        if (log) {
            log(std::format("Warning: capability detection failed: {}", e.what()));
        }
    }

    if (log) {
        log(std::format("Snippet support: {}", snippet_support_ ? "yes" : "no"));
    }
}

// ─── Settings ───

void ConfigurationManager::apply_settings(const JsonValue& params, const LogCallback& log) {
    if (!params.is_object() || !params.has("settings")) {
        return;
    }

    try {
        const auto& settings = params["settings"];
        if (!settings.is_object()) {
            return;
        }

        config_.apply_lsp_settings(settings);

        if (log) {
            const auto snap = config_.get();
            log(std::format("Configuration updated: inlayHints={}, codeLens={}, debounce={}ms",
                            snap->inlay_hints_enabled, snap->code_lens_enabled,
                            snap->analysis_debounce_ms));
        }
    } catch (const std::exception& e) {
        if (log) {
            log(std::format("Error processing configuration: {}", e.what()));
        }
    }
}

} // namespace luma::lsp
