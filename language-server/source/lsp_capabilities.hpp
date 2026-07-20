#ifndef LUMA_LSP_CAPABILITIES_HPP
#define LUMA_LSP_CAPABILITIES_HPP

#include <string>
#include <vector>

#include "json/json.hpp"

namespace luma::lsp {

// Builder for constructing the LSP server capabilities JSON response.
//
// Provides a fluent interface for declaring supported features, replacing
// manual JSON object construction in handle_initialize().
class CapabilitiesBuilder {
public:
    /// Set text document sync options (open/close + change notification kind).
    CapabilitiesBuilder& text_document_sync(bool open_close, int change_kind, bool save = false);

    /// Enable hover support.
    CapabilitiesBuilder& hover();

    /// Enable completion with trigger characters and resolve support.
    CapabilitiesBuilder& completion(const std::vector<std::string>& trigger_chars,
                                    bool resolve_provider = false);

    /// Enable signature help with trigger characters.
    CapabilitiesBuilder& signature_help(const std::vector<std::string>& trigger_chars);

    /// Enable go-to-definition.
    CapabilitiesBuilder& definition();

    /// Enable find references.
    CapabilitiesBuilder& references();

    /// Enable document highlight.
    CapabilitiesBuilder& document_highlight();

    /// Enable type definition.
    CapabilitiesBuilder& type_definition();

    /// Enable implementation.
    CapabilitiesBuilder& implementation();

    /// Enable document symbol.
    CapabilitiesBuilder& document_symbol();

    /// Enable workspace symbol.
    CapabilitiesBuilder& workspace_symbol();

    /// Enable rename with optional prepare support.
    CapabilitiesBuilder& rename(bool prepare_support = true);

    /// Enable code actions.
    CapabilitiesBuilder& code_action();

    /// Enable linked editing range.
    CapabilitiesBuilder& linked_editing_range();

    /// Enable call hierarchy.
    CapabilitiesBuilder& call_hierarchy();

    /// Enable type hierarchy.
    CapabilitiesBuilder& type_hierarchy();

    /// Enable selection range.
    CapabilitiesBuilder& selection_range();

    /// Enable document link with optional resolve.
    CapabilitiesBuilder& document_link(bool resolve_provider = false);

    /// Enable folding range.
    CapabilitiesBuilder& folding_range();

    /// Enable inlay hints.
    CapabilitiesBuilder& inlay_hint();

    /// Enable semantic tokens (full, delta, range) with the given legend.
    CapabilitiesBuilder& semantic_tokens(const std::vector<std::string>& token_types,
                                         const std::vector<std::string>& token_modifiers,
                                         bool full_delta = true, bool range = true);

    /// Enable code lens with optional resolve.
    CapabilitiesBuilder& code_lens(bool resolve_provider = false);

    /// Enable document formatting.
    CapabilitiesBuilder& document_formatting();

    /// Enable document range formatting.
    CapabilitiesBuilder& document_range_formatting();

    /// Enable execute command with the given command IDs.
    CapabilitiesBuilder& execute_command(const std::vector<std::string>& commands);

    /// Set position encoding.
    CapabilitiesBuilder& position_encoding(const std::string& encoding);

    /// Build the final capabilities JSON object.
    [[nodiscard]] json::JsonValue build() const;

private:
    // Set a boolean capability to true.
    CapabilitiesBuilder& enable(std::string_view key);

    json::JsonValue::ObjectType capabilities_;
};

} // namespace luma::lsp

#endif // LUMA_LSP_CAPABILITIES_HPP
