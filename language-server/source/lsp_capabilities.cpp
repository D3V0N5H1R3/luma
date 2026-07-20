#include "lsp_capabilities.hpp"

namespace luma::lsp {

using json::JsonValue;

namespace {
[[nodiscard]] JsonValue::ArrayType to_json_array(const std::vector<std::string>& items) {
    JsonValue::ArrayType result;
    result.reserve(items.size());
    for (const auto& item : items) {
        result.emplace_back(item);
    }
    return result;
}
} // namespace

CapabilitiesBuilder& CapabilitiesBuilder::text_document_sync(bool open_close, int change_kind,
                                                             bool save) {
    JsonValue::ObjectType sync{
        {"openClose", JsonValue(open_close)},
        {"change", JsonValue(static_cast<int64_t>(change_kind))},
    };
    if (save) {
        // Advertising save makes the client send textDocument/didSave, which the
        // include-dependency refresh (handle_did_save) needs to re-analyze files
        // that include the saved one. The saved text is not required — dependents
        // re-read the file from disk — so includeText is false.
        sync["save"] = JsonValue(JsonValue::ObjectType{{"includeText", JsonValue(false)}});
    }
    capabilities_["textDocumentSync"] = JsonValue(std::move(sync));
    return *this;
}

CapabilitiesBuilder& CapabilitiesBuilder::enable(std::string_view key) {
    capabilities_[std::string(key)] = JsonValue(true);
    return *this;
}

CapabilitiesBuilder& CapabilitiesBuilder::hover() {
    return enable("hoverProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::completion(const std::vector<std::string>& trigger_chars,
                                                     bool resolve_provider) {
    capabilities_["completionProvider"] = JsonValue(JsonValue::ObjectType{
        {"triggerCharacters", JsonValue(to_json_array(trigger_chars))},
        {"resolveProvider", JsonValue(resolve_provider)},
    });
    return *this;
}

CapabilitiesBuilder&
CapabilitiesBuilder::signature_help(const std::vector<std::string>& trigger_chars) {
    capabilities_["signatureHelpProvider"] = JsonValue(JsonValue::ObjectType{
        {"triggerCharacters", JsonValue(to_json_array(trigger_chars))},
    });
    return *this;
}

CapabilitiesBuilder& CapabilitiesBuilder::definition() {
    return enable("definitionProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::references() {
    return enable("referencesProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::document_highlight() {
    return enable("documentHighlightProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::type_definition() {
    return enable("typeDefinitionProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::implementation() {
    return enable("implementationProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::document_symbol() {
    return enable("documentSymbolProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::workspace_symbol() {
    return enable("workspaceSymbolProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::rename(bool prepare_support) {
    if (prepare_support) {
        capabilities_["renameProvider"] = JsonValue(JsonValue::ObjectType{
            {"prepareProvider", JsonValue(true)},
        });
    } else {
        capabilities_["renameProvider"] = JsonValue(true);
    }
    return *this;
}

CapabilitiesBuilder& CapabilitiesBuilder::code_action() {
    return enable("codeActionProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::linked_editing_range() {
    return enable("linkedEditingRangeProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::call_hierarchy() {
    return enable("callHierarchyProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::type_hierarchy() {
    return enable("typeHierarchyProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::selection_range() {
    return enable("selectionRangeProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::document_link(bool resolve_provider) {
    capabilities_["documentLinkProvider"] = JsonValue(JsonValue::ObjectType{
        {"resolveProvider", JsonValue(resolve_provider)},
    });
    return *this;
}

CapabilitiesBuilder& CapabilitiesBuilder::folding_range() {
    return enable("foldingRangeProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::inlay_hint() {
    return enable("inlayHintProvider");
}

CapabilitiesBuilder&
CapabilitiesBuilder::semantic_tokens(const std::vector<std::string>& token_types,
                                     const std::vector<std::string>& token_modifiers,
                                     bool full_delta, bool range) {
    JsonValue::ObjectType opts{
        {"legend", JsonValue(JsonValue::ObjectType{
                       {"tokenTypes", JsonValue(to_json_array(token_types))},
                       {"tokenModifiers", JsonValue(to_json_array(token_modifiers))},
                   })},
        {"full", JsonValue(JsonValue::ObjectType{
                     {"delta", JsonValue(full_delta)},
                 })},
        {"range", JsonValue(range)},
    };

    capabilities_["semanticTokensProvider"] = JsonValue(std::move(opts));
    return *this;
}

CapabilitiesBuilder& CapabilitiesBuilder::code_lens(bool resolve_provider) {
    capabilities_["codeLensProvider"] = JsonValue(JsonValue::ObjectType{
        {"resolveProvider", JsonValue(resolve_provider)},
    });
    return *this;
}

CapabilitiesBuilder& CapabilitiesBuilder::document_formatting() {
    return enable("documentFormattingProvider");
}

CapabilitiesBuilder& CapabilitiesBuilder::document_range_formatting() {
    return enable("documentRangeFormattingProvider");
}

CapabilitiesBuilder&
CapabilitiesBuilder::execute_command(const std::vector<std::string>& commands) {
    capabilities_["executeCommandProvider"] = JsonValue(JsonValue::ObjectType{
        {"commands", JsonValue(to_json_array(commands))},
    });
    return *this;
}

CapabilitiesBuilder& CapabilitiesBuilder::position_encoding(const std::string& encoding) {
    capabilities_["positionEncoding"] = JsonValue(encoding);
    return *this;
}

JsonValue CapabilitiesBuilder::build() const {
    return JsonValue(capabilities_);
}

} // namespace luma::lsp
