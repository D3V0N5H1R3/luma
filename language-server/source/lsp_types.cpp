#include "lsp_types.hpp"

#include "json/json.hpp"
#include "lsp_params.hpp"
#include "lsp_string_utils.hpp"
#include "protocol/uri_utils.hpp"

namespace luma::lsp {

// ─────────────────────── TextDocumentPosition ───────────────────────

std::optional<TextDocumentPosition> TextDocumentPosition::from_params(const JsonValue& params) {
    // The shared parser already yields a TextDocumentPosition.
    return params::TextDocumentPositionParams::from_json(params);
}

JsonValue serialise_position(const Position& pos) {
    return JsonValue(JsonValue::ObjectType{
        {"line", JsonValue(pos.line)},
        {"character", JsonValue(pos.character)},
    });
}

JsonValue serialise_range(const Range& range) {
    return JsonValue(JsonValue::ObjectType{
        {"start", serialise_position(range.start)},
        {"end", serialise_position(range.end)},
    });
}

JsonValue serialise_location(const Location& loc) {
    return JsonValue(JsonValue::ObjectType{
        {"uri", JsonValue(loc.uri)},
        {"range", serialise_range(loc.range)},
    });
}

JsonValue serialise_diagnostic(const Diagnostic& diag) {
    JsonValue::ObjectType obj{
        {"range", serialise_range(diag.range)},
        {"severity", JsonValue(diag.severity)},
        {"source", JsonValue(diag.source)},
        {"message", JsonValue(diag.message)},
    };

    if (!diag.code.empty()) {
        obj.emplace("code", JsonValue(diag.code));

        if (!diag.code_description.empty()) {
            obj.emplace("codeDescription", JsonValue(JsonValue::ObjectType{
                                               {"href", JsonValue(diag.code_description)},
                                           }));
        }
    }

    if (!diag.related_information.empty()) {
        JsonValue::ArrayType related;
        for (const auto& ri : diag.related_information) {
            related.emplace_back(JsonValue::ObjectType{
                {"location", serialise_location(ri.location)},
                {"message", JsonValue(ri.message)},
            });
        }
        obj.emplace("relatedInformation", JsonValue(std::move(related)));
    }

    if (!diag.tags.empty()) {
        JsonValue::ArrayType tag_arr;
        for (const auto tag : diag.tags) {
            tag_arr.emplace_back(tag);
        }
        obj.emplace("tags", JsonValue(std::move(tag_arr)));
    }

    return JsonValue(std::move(obj));
}

JsonValue serialise_completion_item(const CompletionItem& item) {
    JsonValue::ObjectType obj{
        {"label", JsonValue(item.label)},
        {"kind", JsonValue(item.kind)},
    };

    if (!item.detail.empty()) {
        obj.emplace("detail", JsonValue(item.detail));
    }

    if (!item.documentation.empty()) {
        obj.emplace("documentation", JsonValue(item.documentation));
    }

    if (!item.insert_text.empty()) {
        obj.emplace("insertText", JsonValue(item.insert_text));
        obj.emplace("insertTextFormat", JsonValue(item.insert_text_format));
    }

    if (!item.data.empty()) {
        obj.emplace("data", JsonValue(item.data));
    }

    if (!item.sort_text.empty()) {
        obj.emplace("sortText", JsonValue(item.sort_text));
    }

    if (!item.filter_text.empty()) {
        obj.emplace("filterText", JsonValue(item.filter_text));
    }

    return JsonValue(std::move(obj));
}

JsonValue serialise_markup_content(const MarkupContent& content) {
    return JsonValue(JsonValue::ObjectType{
        {"kind", JsonValue(content.kind)},
        {"value", JsonValue(content.value)},
    });
}

JsonValue serialise_document_symbol(const DocumentSymbol& sym) {
    JsonValue::ObjectType obj{
        {"name", JsonValue(sym.name)},
        {"kind", JsonValue(to_lsp_symbol_kind(sym.kind))},
        {"range", serialise_range(sym.range)},
        {"selectionRange", serialise_range(sym.selection_range)},
    };

    if (!sym.children.empty()) {
        JsonValue::ArrayType children;

        for (const auto& child : sym.children) {
            children.push_back(serialise_document_symbol(child));
        }

        obj.emplace("children", JsonValue(std::move(children)));
    }

    return JsonValue(std::move(obj));
}

JsonValue serialise_workspace_edit(const WorkspaceEdit& edit) {
    JsonValue::ObjectType changes_obj;

    for (const auto& [uri, edits] : edit.changes) {
        JsonValue::ArrayType edit_array;

        for (const auto& te : edits) {
            edit_array.emplace_back(JsonValue::ObjectType{
                {"range", serialise_range(te.range)},
                {"newText", JsonValue(te.new_text)},
            });
        }

        changes_obj.emplace(uri, JsonValue(std::move(edit_array)));
    }

    return JsonValue(JsonValue::ObjectType{
        {"changes", JsonValue(std::move(changes_obj))},
    });
}

JsonValue serialise_code_action(const CodeAction& action) {
    JsonValue::ObjectType obj{
        {"title", JsonValue(action.title)},
        {"kind", JsonValue(action.kind)},
        {"edit", serialise_workspace_edit(action.edit)},
    };

    if (action.diagnostic.has_value()) {
        JsonValue::ArrayType diags;
        diags.push_back(serialise_diagnostic(*action.diagnostic));
        obj.emplace("diagnostics", JsonValue(std::move(diags)));
    }

    return JsonValue(std::move(obj));
}

// ─────────────────────── LSP Response Builders ───────────────────────

namespace lsp_builders {

JsonValue position(int line, int character) {
    return JsonValue(JsonValue::ObjectType{
        {"line", JsonValue(line)},
        {"character", JsonValue(character)},
    });
}

JsonValue range(int start_line, int start_char, int end_line, int end_char) {
    return JsonValue(JsonValue::ObjectType{
        {"start", position(start_line, start_char)},
        {"end", position(end_line, end_char)},
    });
}

JsonValue markup_content(std::string_view value, std::string_view kind) {
    return JsonValue(JsonValue::ObjectType{
        {"kind", JsonValue(std::string(kind))},
        {"value", JsonValue(std::string(value))},
    });
}

JsonValue hover(std::string_view markdown_content, int start_line, int start_char, int end_line,
                int end_char) {
    return JsonValue(JsonValue::ObjectType{
        {"contents", markup_content(markdown_content)},
        {"range", range(start_line, start_char, end_line, end_char)},
    });
}

JsonValue hover(std::string_view markdown_content) {
    return JsonValue(JsonValue::ObjectType{
        {"contents", markup_content(markdown_content)},
    });
}

JsonValue location(std::string_view uri, int start_line, int start_char, int end_line,
                   int end_char) {
    return JsonValue(JsonValue::ObjectType{
        {"uri", JsonValue(std::string(uri))},
        {"range", range(start_line, start_char, end_line, end_char)},
    });
}

JsonValue completion_item(std::string_view label, int kind, std::string_view detail,
                          std::string_view insert_text, int insert_text_format) {
    JsonValue::ObjectType obj{
        {"label", JsonValue(std::string(label))},
        {"kind", JsonValue(kind)},
    };

    if (!detail.empty()) {
        obj.emplace("detail", JsonValue(std::string(detail)));
    }

    if (!insert_text.empty()) {
        obj.emplace("insertText", JsonValue(std::string(insert_text)));
        obj.emplace("insertTextFormat", JsonValue(insert_text_format));
    }

    return JsonValue(std::move(obj));
}

JsonValue diagnostic(int start_line, int start_char, int end_line, int end_char, int severity,
                     std::string_view message, std::string_view source) {
    return JsonValue(JsonValue::ObjectType{
        {"range", range(start_line, start_char, end_line, end_char)},
        {"severity", JsonValue(severity)},
        {"source", JsonValue(std::string(source))},
        {"message", JsonValue(std::string(message))},
    });
}

JsonValue folding_range(int start_line, int end_line, std::string_view kind) {
    return JsonValue(JsonValue::ObjectType{
        {"startLine", JsonValue(static_cast<int64_t>(start_line))},
        {"endLine", JsonValue(static_cast<int64_t>(end_line))},
        {"kind", JsonValue(std::string(kind))},
    });
}

JsonValue inlay_hint(int line, int character, std::string_view label, int kind, bool padding_left,
                     bool padding_right) {
    return JsonValue(JsonValue::ObjectType{
        {"position", position(line, character)},
        {"label", JsonValue(std::string(label))},
        {"kind", JsonValue(static_cast<int64_t>(kind))},
        {"paddingLeft", JsonValue(padding_left)},
        {"paddingRight", JsonValue(padding_right)},
    });
}

JsonValue semantic_tokens_response(JsonValue::ArrayType data, std::string_view result_id) {
    JsonValue::ObjectType obj;
    if (!result_id.empty()) {
        obj.emplace("resultId", JsonValue(std::string(result_id)));
    }
    obj.emplace("data", JsonValue(std::move(data)));
    return JsonValue(std::move(obj));
}

JsonValue semantic_tokens_delta_response(std::string_view result_id, JsonValue::ArrayType edits) {
    return JsonValue(JsonValue::ObjectType{
        {"resultId", JsonValue(std::string(result_id))},
        {"edits", JsonValue(std::move(edits))},
    });
}

JsonValue semantic_token_edit(int64_t start, int64_t delete_count, JsonValue::ArrayType data) {
    return JsonValue(JsonValue::ObjectType{
        {"start", JsonValue(start)},
        {"deleteCount", JsonValue(delete_count)},
        {"data", JsonValue(std::move(data))},
    });
}

JsonValue hierarchy_item(std::string_view name, SymbolKind kind, std::string_view uri,
                         const Range& range, std::string_view data) {
    JsonValue::ObjectType obj{
        {"name", JsonValue(std::string(name))},
        {"kind", JsonValue(static_cast<int64_t>(to_lsp_symbol_kind(kind)))},
        {"uri", JsonValue(std::string(uri))},
        {"range", serialise_range(range)},
        {"selectionRange", serialise_range(range)},
    };
    if (!data.empty()) {
        obj.emplace("data", JsonValue(std::string(data)));
    }
    return JsonValue(std::move(obj));
}

} // namespace lsp_builders

} // namespace luma::lsp
