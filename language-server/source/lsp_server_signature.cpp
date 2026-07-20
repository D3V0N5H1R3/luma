#include <algorithm>
#include <format>
#include <optional>
#include <string>

#include "json/json.hpp"
#include "lsp_completion_handler.hpp"
#include "lsp_constants.hpp"
#include "lsp_lexical_context.hpp"
#include "lsp_param_utils.hpp"
#include "symbols/qualified_name.hpp"

namespace luma::lsp {

using lexical::find_comment_start_on_line;
using lexical::skip_string_backward;
using util::split_param_list;

// ═══════════════════════════════════════════════════════════
// Signature help
// ═══════════════════════════════════════════════════════════

// Scan backwards from cursor to find an open call of the form
// "ModuleName.funcName(" or "funcName(" that has not yet been closed.
// Returns the function name (qualified for stdlib, plain for user functions)
// and the zero-based index of the active argument (incremented per ',').
struct CallContext {
    std::string qualified_name; // e.g. "Math.floor" or "greet"
    int active_param{0};
    bool is_piped{false}; // true if call is via pipe operator: value |> func(...)
};

namespace {

// ───────────────────────────────────────────────────────────
// Comment-skipping glue around the cursor, built on the shared
// lexical-context primitives (lsp_lexical_context.hpp).
// ───────────────────────────────────────────────────────────

// When crossing a newline backward, skip any comment on the line above
// so that comment content is not interpreted as code.
// `pos` points to the '\n' character; returns the position moved to the
// '#' on the preceding line (or the original `pos` when there is none).
[[nodiscard]] std::size_t skip_comment_on_preceding_line(const std::string& text, std::size_t pos,
                                                         std::size_t /*scan_start*/) {
    auto line_begin = text.rfind('\n', pos - 1);
    line_begin = (line_begin == std::string::npos) ? 0 : line_begin + 1;

    const std::size_t comment_pos = find_comment_start_on_line(text, line_begin, pos);

    return std::min(comment_pos, pos);
}

// Adjust `pos` backward past any comment on the current line.
// Used as a pre-scan before the main backward loop. Returns the updated position.
[[nodiscard]] std::size_t adjust_past_comment_on_current_line(const std::string& text,
                                                              std::size_t pos,
                                                              std::size_t scan_start) {
    std::size_t line_begin = pos;

    while (line_begin > scan_start && text[line_begin - 1] != '\n') {
        --line_begin;
    }

    const std::size_t comment_pos = find_comment_start_on_line(text, line_begin, pos);

    return std::min(comment_pos, pos);
}

// Extracts the module name preceding a dot at the given position.
// Returns empty string if no module prefix found.
[[nodiscard]] std::string extract_module_prefix(const std::string& text, std::size_t dot_pos,
                                                std::size_t limit) {
    const std::size_t mod_end = dot_pos;
    std::size_t mod_start = mod_end;

    while (mod_start > limit) {
        const auto mc = static_cast<unsigned char>(text[mod_start - 1]);

        if ((std::isalnum(mc) != 0) || mc == '_') {
            --mod_start;
        } else {
            break;
        }
    }

    if (mod_start < mod_end) {
        return text.substr(mod_start, mod_end - mod_start);
    }

    return {};
}

// ───────────────────────────────────────────────────────────
// Function name extraction from an opening parenthesis.
// ───────────────────────────────────────────────────────────

// Given that `paren_pos` points to an unmatched '(' at depth 0, scan
// backward to extract the function name, optional "Module." prefix,
// and detect pipe-operator calls.  Returns nullopt if no valid
// identifier precedes the parenthesis.
[[nodiscard]] std::optional<CallContext> extract_call_at_paren(const std::string& text,
                                                               std::size_t paren_pos,
                                                               std::size_t scan_start,
                                                               int active_param) {
    // Skip whitespace between function name and '('.
    std::size_t name_end{paren_pos};

    while (name_end > scan_start) {
        const auto ws = static_cast<unsigned char>(text[name_end - 1]);

        if (ws == ' ' || ws == '\t' || ws == '\r' || ws == '\n') {
            --name_end;
        } else {
            break;
        }
    }

    // Collect identifier characters.
    std::size_t id_end{name_end};

    while (id_end > scan_start) {
        const auto ic = static_cast<unsigned char>(text[id_end - 1]);

        if ((std::isalnum(ic) != 0) || ic == '_') {
            --id_end;
        } else {
            break;
        }
    }

    if (id_end == name_end) {
        return std::nullopt;
    }

    const std::string func_name = text.substr(id_end, name_end - id_end);

    // Check for "Module." prefix.
    std::string qualified{func_name};

    if (id_end >= 1 && text[id_end - 1] == '.') {
        const auto module_name = extract_module_prefix(text, id_end - 1, scan_start);

        if (!module_name.empty()) {
            qualified = module_name + "." + func_name;
        }
    }

    // Detect pipe operator: `value |> func(`.
    bool piped = false;
    {
        std::size_t check_pos = id_end;

        // If we had a module prefix, start from before the module name.
        if (id_end >= 1 && text[id_end - 1] == '.') {
            const auto mod_name = extract_module_prefix(text, id_end - 1, scan_start);

            if (!mod_name.empty()) {
                check_pos = id_end - 1 - mod_name.size();
            }
        }

        // Skip whitespace before the function name.
        while (check_pos > scan_start &&
               (text[check_pos - 1] == ' ' || text[check_pos - 1] == '\t' ||
                text[check_pos - 1] == '\n' || text[check_pos - 1] == '\r')) {
            --check_pos;
        }

        if (check_pos >= 2 && text[check_pos - 1] == '>' && text[check_pos - 2] == '|') {
            piped = true;
        }
    }

    return CallContext{
        .qualified_name = qualified, .active_param = active_param, .is_piped = piped};
}

// ───────────────────────────────────────────────────────────
// Main call-context finder
// ───────────────────────────────────────────────────────────

[[nodiscard]] std::optional<CallContext> find_call_context(const std::string& text,
                                                           std::size_t cursor_offset) {
    if (cursor_offset == 0) {
        return std::nullopt;
    }

    int depth{0};
    int commas_at_depth0{0};

    const std::size_t scan_start = cursor_offset > constants::limits::max_scan_chars
                                       ? cursor_offset - constants::limits::max_scan_chars
                                       : 0;

    std::size_t pos{cursor_offset};

    // Pre-scan: if the cursor is inside a comment (# to end-of-line),
    // move it back before the comment start so we don't count
    // parentheses/commas inside the comment.
    pos = adjust_past_comment_on_current_line(text, pos, scan_start);

    while (pos > scan_start) {
        --pos;

        const char c = text[pos];

        // When crossing a newline boundary, skip any comment on the
        // preceding line so its content is not interpreted as code.
        if (c == '\n' && pos > scan_start) {
            pos = skip_comment_on_preceding_line(text, pos, scan_start);
            continue;
        }

        // '#' on the cursor's own line — already handled by the
        // newline/pre-scan logic above.
        if (c == '#') {
            continue;
        }

        if (c == ')') {
            ++depth;
        } else if (c == '(') {
            if (depth > 0) {
                --depth;
            } else {
                return extract_call_at_paren(text, pos, scan_start, commas_at_depth0);
            }
        } else if (c == ',' && depth == 0) {
            ++commas_at_depth0;
        } else if (c == '"') {
            pos = skip_string_backward(text, pos, scan_start);
        }
        // Note: '\n' is intentionally not a stop condition.
        // Calls can span multiple lines.
    }

    return std::nullopt;
}

// Builds per-parameter objects with character-offset labels so editors can
// highlight the active parameter within the signature label. Falls back to a
// plain-text label when a parameter substring is not found in the label.
[[nodiscard]] JsonValue::ArrayType build_parameter_labels(const std::string& sig_label,
                                                          const std::string& params_sig) {
    JsonValue::ArrayType param_objects;
    if (params_sig.empty()) {
        return param_objects;
    }

    const auto param_list = split_param_list(params_sig);
    std::size_t search_from{0};
    for (const auto& param : param_list) {
        const auto pos_in_label = sig_label.find(param, search_from);
        if (pos_in_label != std::string::npos) {
            param_objects.emplace_back(JsonValue::ObjectType{
                {"label", JsonValue(JsonValue::ArrayType{
                              JsonValue(static_cast<int64_t>(pos_in_label)),
                              JsonValue(static_cast<int64_t>(pos_in_label + param.size())),
                          })},
            });
            search_from = pos_in_label + param.size();
        } else {
            // Fallback: use the parameter string as a plain text label.
            param_objects.emplace_back(JsonValue::ObjectType{
                {"label", JsonValue(param)},
            });
        }
    }
    return param_objects;
}

// Resolved signature text for a call: the full label plus its parameter list.
struct ResolvedSignature {
    std::string label;
    std::string params;
};

// Resolve a call's signature from stdlib metadata (O(1) index lookup), falling
// back to user-defined functions in the cached analysis. An empty label means
// the function is unknown.
[[nodiscard]] ResolvedSignature resolve_signature(const CallContext& call,
                                                  const StdlibRegistry& stdlib_registry,
                                                  const AnalysisResult* cached) {
    ResolvedSignature sig;

    if (is_qualified_name(call.qualified_name)) {
        const auto func_ref = stdlib_registry.find_function(call.qualified_name);
        if (func_ref) {
            const auto& func = *func_ref;
            sig.params = func.params_signature;
            sig.label = call.qualified_name + (sig.params.empty() ? "()" : sig.params) + " -> " +
                        func.return_type;
        }
    }

    if (sig.label.empty() && cached != nullptr) {
        const auto& user_funcs = cached->semantic.symbols.user_functions;
        if (const auto func_it = user_funcs.find(call.qualified_name);
            func_it != user_funcs.end()) {
            sig.label = func_it->second.signature;
            sig.params = func_it->second.params_signature;
        }
    }

    return sig;
}

} // namespace

JsonValue LspCompletionHandler::handle_signature_help(const JsonValue& params) {
    auto state = ctx_.acquire_read_lock();
    const auto doc_at_pos = ctx_.get_document_at_position(params, state.token());
    if (!doc_at_pos) {
        return {}; // null — invalid params or no document
    }

    const auto& uri = doc_at_pos->uri;
    const int line = doc_at_pos->line;
    const int character = doc_at_pos->character;
    const auto& text = *doc_at_pos->content;

    const std::size_t cursor_offset =
        ctx_.doc_store.position_to_offset(state.token(), uri, text, line, character);

    const auto ctx_opt = find_call_context(text, cursor_offset);

    if (!ctx_opt.has_value()) {
        return {}; // null — cursor not in a call
    }

    const auto& ctx = *ctx_opt;

    // Resolve the signature label and parameter list from stdlib or user code.
    const auto cached = ctx_.find_analysis(uri);
    const ResolvedSignature sig =
        resolve_signature(ctx, ctx_.stdlib_registry, cached ? &*cached : nullptr);

    if (sig.label.empty()) {
        return {}; // unknown function — no help
    }

    // Build per-parameter objects with character-offset labels so editors can
    // highlight the active parameter within the signature label.
    JsonValue::ArrayType param_objects = build_parameter_labels(sig.label, sig.params);

    // Build the SignatureInformation object.
    JsonValue::ObjectType sig_info{
        {"label", JsonValue(sig.label)},
    };

    if (!param_objects.empty()) {
        sig_info.emplace("parameters", JsonValue(std::move(param_objects)));
    }

    // When a function is called via pipe operator (`value |> func(arg2, arg3)`),
    // the first parameter is implicitly the piped value, so the active parameter
    // seen by the user is shifted by +1.
    const int active_param = ctx.is_piped ? ctx.active_param + 1 : ctx.active_param;

    return JsonValue(JsonValue::ObjectType{
        {"activeParameter", JsonValue(active_param)},
        {"activeSignature", JsonValue(0)},
        {"signatures", JsonValue(JsonValue::ArrayType{
                           JsonValue(std::move(sig_info)),
                       })},
    });
}

} // namespace luma::lsp
