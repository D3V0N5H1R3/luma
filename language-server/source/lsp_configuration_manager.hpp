#ifndef LUMA_LSP_CONFIGURATION_MANAGER_HPP
#define LUMA_LSP_CONFIGURATION_MANAGER_HPP

// ═══════════════════════════════════════════════════════════
// ConfigurationManager — LSP settings and client capabilities.
//
// Extracted from LspServer to give configuration state a
// single owner.  LspServer delegates configuration queries
// and updates to this helper via composition.
// ═══════════════════════════════════════════════════════════

#include <format>
#include <functional>
#include <string>

#include "json/json.hpp"
#include "lsp_config.hpp"

namespace luma::lsp {

class ConfigurationManager {
public:
    using LogCallback = std::function<void(const std::string&)>;

    // ─── Config access ───

    [[nodiscard]] LspConfig& config() noexcept;

    [[nodiscard]] const LspConfig& config() const noexcept;

    // ─── Client capabilities ───

    [[nodiscard]] bool snippet_support() const noexcept;

    // Extract client capabilities from the initialize params.
    void detect_client_capabilities(const JsonValue& params, const LogCallback& log = {});

    // ─── Settings ───

    // Handle workspace/didChangeConfiguration notification.
    void apply_settings(const JsonValue& params, const LogCallback& log = {});

private:
    LspConfig config_;
    bool snippet_support_{false};
};

} // namespace luma::lsp

#endif // LUMA_LSP_CONFIGURATION_MANAGER_HPP
