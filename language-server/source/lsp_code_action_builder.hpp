#ifndef LUMA_LSP_CODE_ACTION_BUILDER_HPP
#define LUMA_LSP_CODE_ACTION_BUILDER_HPP

#include <string>
#include <string_view>
#include <vector>

#include "lsp_types.hpp"

namespace luma::lsp {

// Fluent builder for CodeAction construction.
// Avoids repetitive struct-member assignment when building code actions
// and supports fields (isPreferred, multiple diagnostics) that the plain
// CodeAction struct does not carry.
//
// Usage:
//   actions.push_back(CodeActionBuilder()
//       .set_title("Add 'mutable' to 'x'")
//       .set_kind("quickfix")
//       .set_diagnostics({diag})
//       .add_edit(uri, Range{{1,0},{1,0}}, "mutable ")
//       .set_preferred(true)
//       .build());
class CodeActionBuilder {
public:
    CodeActionBuilder& set_title(std::string_view title) {
        title_ = std::string(title);
        return *this;
    }

    CodeActionBuilder& set_kind(std::string_view kind) {
        kind_ = std::string(kind);
        return *this;
    }

    CodeActionBuilder& add_edit(std::string_view uri, const Range& range,
                                std::string_view new_text) {
        edits_[std::string(uri)].push_back(TextEdit{range, std::string(new_text)});
        return *this;
    }

    CodeActionBuilder& set_diagnostics(std::vector<Diagnostic> diags) {
        diagnostics_ = std::move(diags);
        return *this;
    }

    CodeActionBuilder& set_preferred(bool preferred) {
        is_preferred_ = preferred;
        return *this;
    }

    [[nodiscard]] bool has_edits() const {
        return !edits_.empty();
    }

    // Produce the JSON representation expected by the LSP client.
    [[nodiscard]] JsonValue build() const {
        WorkspaceEdit ws_edit;
        ws_edit.changes = edits_;

        JsonValue::ObjectType obj{
            {"title", JsonValue(title_)},
            {"kind", JsonValue(kind_)},
            {"edit", serialise_workspace_edit(ws_edit)},
        };

        if (!diagnostics_.empty()) {
            JsonValue::ArrayType arr;
            arr.reserve(diagnostics_.size());
            for (const auto& d : diagnostics_) {
                arr.push_back(serialise_diagnostic(d));
            }
            obj.emplace("diagnostics", JsonValue(std::move(arr)));
        }

        if (is_preferred_) {
            obj.emplace("isPreferred", JsonValue(true));
        }

        return JsonValue(std::move(obj));
    }

private:
    std::string title_;
    std::string kind_;
    std::map<std::string, std::vector<TextEdit>> edits_;
    std::vector<Diagnostic> diagnostics_;
    bool is_preferred_{false};
};

} // namespace luma::lsp

#endif // LUMA_LSP_CODE_ACTION_BUILDER_HPP
