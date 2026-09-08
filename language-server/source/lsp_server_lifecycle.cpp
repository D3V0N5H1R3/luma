#include <format>
#include <string>

#include "common/version.hpp"
#include "lsp_analysis_pipeline.hpp"
#include "lsp_capabilities.hpp"
#include "lsp_response_helpers.hpp"
#include "lsp_server.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_workspace_handler.hpp"
#include "protocol/uri_utils.hpp"

namespace luma::lsp {

using luma::protocol::uri_to_path;

namespace {

// ─── Capability builders ─────────────────────────────────────────────

// The server advertises the LSP default "utf-16" position encoding.  Luma's
// internal columns are codepoint-based, and the LSP boundary converts them to
// and from UTF-16 (see PositionEncoder), so this is exact even on lines that
// contain supplementary-plane characters (emoji and other astral code points
// that occupy two UTF-16 code units).  We deliberately do NOT negotiate
// "utf-32": the entire conversion layer is UTF-16-only, so reporting utf-32
// would ship UTF-16 numbers under a utf-32 label and corrupt every position for
// such clients.  utf-16 is universally supported and already correct.
[[nodiscard]] JsonValue build_server_capabilities(const std::string& position_encoding) {
    return CapabilitiesBuilder()
        .text_document_sync(true, 2, true)
        .hover()
        .completion({".", ">"}, true)
        .signature_help({"(", ","})
        .definition()
        .references()
        .type_definition()
        .implementation()
        .document_symbol()
        .rename(true)
        .code_action()
        .document_link(false)
        .folding_range()
        .semantic_tokens({"namespace", "type", "function", "variable", "parameter", "keyword",
                          "string", "number", "operator", "decorator"},
                         {"definition", "readonly"}, false, false)
        .document_formatting()
        .execute_command({"luma.showReferences"})
        .position_encoding(position_encoding)
        .build();
}

} // namespace

// ═══════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════

JsonValue LspServer::handle_initialize(const JsonValue& params) {
    transport_wrapper_.log_message("Received initialize");

    // Delegate capability detection to the configuration manager.
    configuration_.detect_client_capabilities(
        params, [this](const std::string& msg) { transport_wrapper_.log_message(msg); });
    if (!configuration_.position_encoding_supported()) {
        throw InvalidParamsError(
            "The Luma language server currently supports only the utf-16 position encoding");
    }

    // Extract workspace root folders for background indexing.
    try {
        if (params.is_object()) {
            // Prefer workspaceFolders (multi-root workspaces).
            if (const auto& folders_val = params.get("workspaceFolders"); folders_val.is_array()) {
                for (const auto& folder : folders_val.as_array()) {
                    auto folder_uri = folder.get_or<std::string>("uri", "");
                    if (!folder_uri.empty()) {
                        const auto folder_path = uri_to_path(folder_uri);
                        if (folder_path.has_value()) {
                            workspace_.add_root(*folder_path);
                        }
                    }
                }
            }
            // Fall back to rootUri (single-root workspaces).
            auto root_uri = params.get_or<std::string>("rootUri", "");
            if (!workspace_.has_roots() && !root_uri.empty()) {
                const auto root_path = uri_to_path(root_uri);
                if (root_path.has_value()) {
                    workspace_.add_root(*root_path);
                }
            }
        }
    } catch (const std::exception& e) {
        // Best-effort initialization; continue with defaults.
        transport_wrapper_.log_message(
            std::format("Warning: workspace initialization failed: {}", e.what()));
    }

    if (workspace_.has_roots()) {
        for (const auto& root : workspace_.roots()) {
            transport_wrapper_.log_message(std::format("Workspace root: {}", root));
        }
    }

    return JsonValue(JsonValue::ObjectType{
        {"capabilities", build_server_capabilities(configuration_.position_encoding())},
        {"serverInfo", JsonValue(JsonValue::ObjectType{
                           {"name", JsonValue("luma-lsp")},
                           {"version", JsonValue(std::string(luma_version))},
                       })},
    });
}

void LspServer::handle_initialized() {
    initialized_ = true;
    transport_wrapper_.log_message("Client initialized");

    // Discover and apply a luma.json project configuration from the workspace
    // roots, if present.
    if (workspace_.has_roots()) {
        workspace_.discover_project_config(configuration_.config(), [this](const std::string& msg) {
            transport_wrapper_.log_message(msg);
        });
    }
}

JsonValue LspServer::handle_shutdown() {
    transport_wrapper_.log_message("Shutdown requested");
    shutdown_requested_ = true;

    // Stop the analysis worker.
    running_.store(false);
    analysis_pipeline_->notify();

    return {}; // null
}

} // namespace luma::lsp
