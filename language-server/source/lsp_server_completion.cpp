#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/lexer/token_type.hpp"
#include "json/json.hpp"
#include "lsp_brace_matcher.hpp"
#include "lsp_completion_handler.hpp"
#include "lsp_completion_provider.hpp"
#include "lsp_completion_strategy.hpp"
#include "lsp_constants.hpp"
#include "lsp_keyword_catalog.hpp"
#include "lsp_lexical_context.hpp"
#include "lsp_path_utils.hpp"
#include "lsp_scope_stack.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_stdlib_registry.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_symbol_lookup.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_types.hpp"
#include "protocol/uri_utils.hpp"

namespace luma::lsp {

namespace {

// Whether keyword completions are requested at file scope or inside a
// function body (governs which context-specific keywords are offered).
enum class KeywordScope {
    Global,
    InsideFunction
};

[[nodiscard]] JsonValue::ArrayType
keyword_completions(bool snippet_support, const std::vector<std::string>& module_names,
                    const std::string& line_text_before_cursor = "",
                    KeywordScope context = KeywordScope::Global) {
    // Determine context from the text before cursor on this line.
    // Check if the trimmed text before cursor ends with '}'.
    auto trimmed = line_text_before_cursor;
    while (!trimmed.empty() && trimmed.back() == ' ') {
        trimmed.pop_back();
    }
    const bool after_brace = !trimmed.empty() && trimmed.back() == '}';

    JsonValue::ArrayType items;

    auto should_include = [&](const auto& kw) {
        if (kw.context == KeywordContext::AfterBrace) {
            return after_brace;
        }
        if (kw.context == KeywordContext::Function) {
            return context == KeywordScope::InsideFunction;
        }
        if (kw.context == KeywordContext::Declaration) {
            return context == KeywordScope::Global;
        }
        return true;
    };

    // Keyword items from the shared catalog.
    for (const auto& kw : keyword_catalog()) {
        if (!should_include(kw)) {
            continue;
        }

        std::string insert_text;
        int insert_fmt{constants::insert_text_format::plaintext};

        if (snippet_support && !kw.snippet.empty()) {
            insert_text = std::string(kw.snippet);
            insert_fmt = constants::insert_text_format::snippet;
        }

        items.push_back(CompletionItemBuilder()
                            .label(kw.name)
                            .kind(constants::completion_kind::keyword)
                            .detail(kw.detail)
                            .insert_text(insert_text)
                            .insert_text_format(insert_fmt)
                            .build());
    }

    // Stdlib module name items — typing "Ma" should offer "Math".
    for (const auto& mod : module_names) {
        // Snippet: type "Math." to immediately trigger member completion.
        const std::string insert_text = snippet_support ? mod + ".$0" : std::string{};

        items.push_back(lsp_builders::completion_item(
            mod, constants::completion_kind::module_, "stdlib module", insert_text,
            snippet_support ? constants::insert_text_format::snippet
                            : constants::insert_text_format::plaintext));
    }

    return items;
}

// The cursor's lexical context on its own line, used to suppress completion
// inside string literals and comments (B06).
enum class CursorLexicalContext {
    Code,
    String,
    Comment
};

// Classify whether the cursor (at the end of `line_prefix`, the raw text from
// the line start up to the cursor) sits in code, a string literal, or a
// comment. Reuses the shared forward lexical scanner so the string/comment/
// interpolation grammar stays in one place. A `${...}` interpolation counts as
// code, so completion still fires inside interpolated expressions.
[[nodiscard]] CursorLexicalContext classify_cursor_context(std::string_view line_prefix) {
    lexical::LineContext lex;
    std::string sink;
    const std::string line{line_prefix};
    for (std::size_t i{0}; i < line.size(); ++i) {
        bool append_rest{false};
        (void)lex.update(line, i, sink, append_rest);
        if (append_rest) {
            return CursorLexicalContext::Comment;
        }
    }
    return lex.is_code() ? CursorLexicalContext::Code : CursorLexicalContext::String;
}

// ═══════════════════════════════════════════════════════════
// Generic completion category helpers
//
// Each function appends completion items for a specific category
// (user functions, parameters, locals, type names, annotations)
// to the given items array. Called by handle_generic_completions() to
// build the full unfiltered completion list.
//
// Note: append_user_function_completions, append_type_name_completions,
// and append_annotation_completions have been migrated to the
// CompletionProvider strategy pattern (see lsp_completion_provider.hpp).
// The parameter and local variable helpers below are retained for
// reference — they use scope-aware logic that makes them candidates
// for a future ScopeAwareCompletionProvider.
// ═══════════════════════════════════════════════════════════

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// Completion helpers
// ═══════════════════════════════════════════════════════════

LspCompletionHandler::CompletionContext
LspCompletionHandler::build_completion_context(const LockToken& token, const std::string& uri,
                                               const std::string& text, int line,
                                               int character) const {
    const std::size_t cursor_offset =
        ctx_.doc_store.position_to_offset(token, uri, text, line, character);

    std::size_t line_start = 0;
    if (cursor_offset > 0) {
        const std::size_t nl = text.rfind('\n', cursor_offset - 1);
        if (nl != std::string::npos) {
            line_start = nl + 1;
        }
    }

    const std::string_view line_prefix =
        std::string_view(text).substr(line_start, cursor_offset - line_start);

    return CompletionContext{.uri = uri,
                             .line = line,
                             .cursor_offset = cursor_offset,
                             .line_start = line_start,
                             .line_prefix = line_prefix,
                             .text = text};
}

std::optional<std::string> LspCompletionHandler::parse_module_name(const std::string& text,
                                                                   std::size_t line_start,
                                                                   std::size_t dot_pos) {
    if (dot_pos < line_start) {
        return std::nullopt;
    }

    // Walk backwards to find the start of the identifier.
    const std::size_t name_end{dot_pos};
    std::size_t name_start{name_end};

    while (name_start > line_start) {
        const char c{text[name_start - 1]};

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '_') {
            --name_start;
        } else {
            break;
        }
    }

    if (name_start == name_end) {
        return std::nullopt;
    }

    return text.substr(name_start, name_end - name_start);
}

JsonValue LspCompletionHandler::build_completion_item(std::string_view name, int kind,
                                                      std::string_view detail,
                                                      std::string_view insert_text, int format,
                                                      std::string_view sort_text,
                                                      const JsonValue& data) {
    return CompletionItemBuilder()
        .label(name)
        .kind(kind)
        .detail(detail)
        .insert_text(insert_text)
        .insert_text_format(format)
        .sort_text(sort_text)
        .data(data.is_string() ? std::string_view(data.as_string()) : std::string_view{})
        .build();
}

// ═══════════════════════════════════════════════════════════
// Top-level completion handler
// ═══════════════════════════════════════════════════════════

JsonValue LspCompletionHandler::handle_completion(const JsonValue& params) {
    auto state = ctx_.acquire_read_lock();
    const auto doc_at_pos = ctx_.get_document_at_position(params, state.token());
    if (!doc_at_pos) {
        throw InvalidParamsError("Missing or malformed textDocument/position params");
    }

    const CompletionContext ctx =
        build_completion_context(state.token(), doc_at_pos->uri, *doc_at_pos->content,
                                 doc_at_pos->line, doc_at_pos->character);

    // B06: suppress completion inside string literals and comments — completing
    // types/keywords there is noise. Include-path completion (`include "..."`)
    // legitimately lives inside a string, so it stays exempt and is handled by
    // IncludePathCompletionStrategy in the chain below.
    switch (classify_cursor_context(ctx.line_prefix)) {
        case CursorLexicalContext::Comment:
            return JsonValue(JsonValue::ArrayType{});
        case CursorLexicalContext::String:
            if (ctx.line_prefix.find("include \"") == std::string_view::npos) {
                return JsonValue(JsonValue::ArrayType{});
            }
            break;
        case CursorLexicalContext::Code:
            break;
    }

    if (ctx.cursor_offset == 0) {
        return handle_generic_completions(ctx);
    }

    // Run the strategy chain — first matching strategy wins.
    static const auto strategy_chain = create_default_strategy_chain();
    if (auto result = strategy_chain.try_provide(ctx, ctx_)) {
        return *result;
    }

    // Fallback: dot access and generic completions.
    return handle_dot_completions(ctx);
}

// ═══════════════════════════════════════════════════════════
// Dot and generic completions
// ═══════════════════════════════════════════════════════════

JsonValue LspCompletionHandler::handle_dot_completions(const CompletionContext& ctx) {
    auto cursor_offset = ctx.cursor_offset;

    if (ctx.text[cursor_offset - 1] != '.') {
        // Also check if we are typing after "Module." with some partial text.
        // Find the last "." before cursor.
        const std::size_t dot_pos{ctx.text.rfind('.', cursor_offset - 1)};

        if (dot_pos == std::string::npos || dot_pos < ctx.line_start) {
            // No module-access context — offer general completions.
            return handle_generic_completions(ctx);
        }

        cursor_offset = dot_pos + 1;
    } else {
        // cursor_offset - 1 is the dot position.
    }

    // Extract the module name before the dot.
    const std::size_t dot_pos{cursor_offset - 1};

    const auto module_name = parse_module_name(ctx.text, ctx.line_start, dot_pos);

    if (!module_name) {
        // No identifier before '.': give up on module access and return
        // general completions instead.
        return handle_generic_completions(ctx);
    }

    if (auto result = try_module_completion(*module_name)) {
        return *result;
    }

    if (auto result = try_dot_completion(ctx, *module_name)) {
        return *result;
    }

    return JsonValue(JsonValue::ArrayType{});
}

// ═══════════════════════════════════════════════════════════
// Completion sub-handlers
// ═══════════════════════════════════════════════════════════

JsonValue LspCompletionHandler::handle_generic_completions(const CompletionContext& ctx) {
    // Use ScopeStack to determine context and collect visible symbols.
    const auto cached = ctx_.find_analysis(ctx.uri);
    const int luma_line = ctx.line + 1; // 1-based for analysis data

    // Build the ScopeStack once: its constructor walks the cached analysis to
    // build the per-line scope levels, so the two consumers below (the
    // inside_function() context check and the visible-symbol collection) share a
    // single instance instead of reconstructing it.
    std::optional<ScopeStack> scopes;
    if (cached) {
        scopes.emplace(*cached, luma_line);
    }

    const bool in_function = scopes && scopes->inside_function();

    // ── Keywords and stdlib module names ──
    auto items =
        keyword_completions(ctx_.configuration.snippet_support(),
                            ctx_.stdlib_registry.module_names(), std::string(ctx.line_prefix),
                            in_function ? KeywordScope::InsideFunction : KeywordScope::Global);

    if (cached) {
        const bool snippets = ctx_.configuration.snippet_support();

        // ── Provider-based completions (user functions, types, annotations) ──
        static const auto provider_registry = create_default_provider_registry();
        const CompletionProviderContext provider_ctx{
            .analysis = *cached,
            .luma_line = luma_line,
            .snippet_support = snippets,
            .enclosing_function = find_enclosing_function(*cached, luma_line)};
        provider_registry.append_all(provider_ctx, items);

        // ── Parameters and local variables via ScopeStack ──
        for (const auto& sym : scopes->collect_visible_symbols()) {
            if (sym.origin == ScopeKind::Module &&
                cached->semantic.symbols.user_functions.contains(sym.name)) {
                continue; // already covered by UserFunctionCompletionProvider
            }
            const std::string detail =
                sym.is_parameter
                    ? (sym.type_name.empty() ? "(parameter)" : ": " + sym.type_name)
                    : (sym.type_name.empty() ? "(local variable)" : ": " + sym.type_name);
            items.push_back(CompletionItemBuilder()
                                .label(sym.name)
                                .kind(constants::completion_kind::variable)
                                .detail(detail)
                                .sort_text("0" + sym.name)
                                .build());
        }
    }

    return JsonValue(std::move(items));
}

} // namespace luma::lsp
