#include <format>
#include <string>
#include <unordered_map>

#include "analysis/lexer/token.hpp"
#include "analysis/lexer/token_type.hpp"
#include "common/string_hash.hpp"
#include "json/json.hpp"
#include "lsp_analysis_view.hpp"
#include "lsp_hover_handler.hpp"
#include "lsp_hover_literals.hpp"
#include "lsp_keyword_catalog.hpp"
#include "lsp_scope_stack.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_symbol_lookup.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

namespace {

// Build the keyword → hover-doc map from the shared keyword catalog.
// Called once to initialise the static map in keyword_hover().
//
// StringMap gives the map transparent (heterogeneous) hashing/equality so the
// per-hover lookup below can probe with the token's lexeme directly, without
// materialising a temporary std::string key.
[[nodiscard]] StringMap<std::string> build_keyword_hover_map() {
    StringMap<std::string> m;
    for (const auto& entry : keyword_catalog()) {
        if (!entry.hover_doc.empty()) {
            m[std::string(entry.name)] = std::string(entry.hover_doc);
        }
    }
    return m;
}

// Return hover documentation for built-in language keywords, type names,
// and literal tokens. Returns an empty string if the token is not a keyword.
[[nodiscard]] std::string keyword_hover(const Token& token) {
    // Special case: boolean literal includes the actual value.
    if (token.type == TokenType::BooleanLiteral) {
        return std::format("```luma\nboolean  # {}\n```", token.lexeme);
    }

    // Check literal/type-keyword token types first (map defined in lsp_hover_literals.hpp).
    const auto& literal_hover_map = get_literal_hover_map();
    auto lit_it = literal_hover_map.find(token.type);
    if (lit_it != literal_hover_map.end()) {
        return std::string(lit_it->second);
    }

    // Look up keyword by lexeme in a static hash map built from the shared catalog.
    static const auto keyword_hover_map = build_keyword_hover_map();

    auto kw_it = keyword_hover_map.find(token.lexeme);
    if (kw_it != keyword_hover_map.end()) {
        return kw_it->second;
    }

    return {};
}

// ═══════════════════════════════════════════════════════════
// Identifier hover resolvers
//
// Each function attempts to produce hover text for a plain identifier
// token (TokenType::Identifier). Returns an empty string when the
// resolver does not apply. The main handler calls them in priority
// order; the first non-empty result wins.
// ═══════════════════════════════════════════════════════════

// Format a value as a fenced luma code block, optionally followed by a
// documentation separator and doc text. Centralises the repeated
// "```luma\n...\n```" + optional doc pattern used by most resolvers.
[[nodiscard]] std::string format_hover_block(std::string_view code, std::string_view doc = {}) {
    std::string text = std::format("```luma\n{}\n```", code);
    if (!doc.empty()) {
        text += "\n\n---\n\n";
        text += doc;
    }
    return text;
}

// Resolve hover for a stdlib module name (e.g., "Math", "String").
// Shows the module namespace and the number of functions it exposes.
[[nodiscard]] std::string hover_stdlib_module(const Token& token, const StdlibRegistry& registry) {
    auto mod_it = registry.find_module(token.lexeme);
    if (!mod_it) {
        return {};
    }
    return format_hover_block(std::format("namespace {}", token.lexeme),
                              std::format("{} functions available", mod_it->size()));
}

// Resolve hover for a user-defined function, showing its signature
// and doc comment (if any).
[[nodiscard]] std::string hover_user_function(const Token& token, const AnalysisResult& result) {
    const SymbolLookup lookup{result};
    auto func_ref = lookup.find_function(token.lexeme);
    if (!func_ref) {
        return {};
    }
    auto doc_ref = lookup.find_doc_comment(token.lexeme);
    return format_hover_block(func_ref->signature, doc_ref ? *doc_ref : std::string_view{});
}

// Resolve hover for a local variable, showing its inferred type.
[[nodiscard]] std::string hover_local_variable(const Token& token, const AnalysisResult& result) {
    const auto resolved = resolve_variable_type(result, token.lexeme, token.location.line);
    if (!resolved.has_value()) {
        return {};
    }
    return format_hover_block(std::format("{}: {}", token.lexeme, resolved->type_name));
}

// Resolve hover for a function parameter within its enclosing function.
[[nodiscard]] std::string hover_function_parameter(const Token& token,
                                                   const AnalysisResult& result) {
    const ScopeStack scopes{result, token.location.line};
    if (!scopes.inside_function()) {
        return {};
    }
    auto sym = scopes.find_symbol(token.lexeme);
    if (!sym.has_value() || !sym->is_parameter) {
        return {};
    }
    return format_hover_block(std::format("(parameter) {}: {}", sym->name, sym->type_name));
}

// Resolve hover for a record type definition, showing its fields.
[[nodiscard]] std::string hover_record_type(const Token& token, const AnalysisResult& result) {
    const SymbolLookup lookup{result};
    auto rec_ref = lookup.find_record(token.lexeme);
    if (!rec_ref || token.lexeme.starts_with(k_interface_record_prefix)) {
        return {};
    }
    std::string fields;
    for (const auto& [fname, ftype] : rec_ref->fields) {
        fields += std::format("    {}: {}\n", fname, ftype);
    }
    return format_hover_block(std::format("record {} {{\n{}}}", token.lexeme, fields));
}

// Resolve hover for a choice type definition, showing its variants.
[[nodiscard]] std::string hover_choice_type(const Token& token, const AnalysisResult& result) {
    const SymbolLookup lookup{result};
    auto ch_ref = lookup.find_choice_variants(token.lexeme);
    if (!ch_ref) {
        return {};
    }
    std::string variants;
    for (const auto& v : *ch_ref) {
        variants += std::format("    {}\n", v);
    }
    return format_hover_block(std::format("choice {} {{\n{}}}", token.lexeme, variants));
}

// Resolve hover for an interface definition, showing its method signatures.
[[nodiscard]] std::string hover_interface_type(const Token& token, const AnalysisResult& result) {
    const SymbolLookup lookup{result};
    auto iface_ref = lookup.find_record(std::string{k_interface_record_prefix} + token.lexeme);
    if (!iface_ref) {
        return {};
    }
    std::string fields;
    for (const auto& [fname, ftype] : iface_ref->fields) {
        fields += std::format("    {}: {}\n", fname, ftype);
    }
    return format_hover_block(std::format("interface {} {{\n{}}}", token.lexeme, fields));
}

// Resolve hover for a user-defined namespace name, showing its member count.
// A namespace is recognised when the identifier is a prefix of at least one
// qualified user-function key ("Ns.func", "Ns.Sub.func", …).
[[nodiscard]] std::string hover_user_namespace(const Token& token, const AnalysisResult& result) {
    const AnalysisResultView view{result};
    const auto count = view.count_namespace_members(token.lexeme);
    if (count == 0) {
        return {};
    }
    return format_hover_block(std::format("namespace {}", token.lexeme),
                              std::format("{} member{} defined", count, count == 1 ? "" : "s"));
}

// ═══════════════════════════════════════════════════════════
// Dot-access hover resolvers
//
// Each function resolves hover text for a qualified access pattern
// (Identifier.Identifier). The caller provides the prefix name
// (before the dot) and the member name (after the dot).
// ═══════════════════════════════════════════════════════════

// Resolve hover for a stdlib function access (e.g., "Math.sqrt").
[[nodiscard]] std::string hover_stdlib_function(const std::string& module_name,
                                                const std::string& func_name,
                                                const StdlibRegistry& registry) {
    auto func_ptr = registry.find_function(module_name + "." + func_name);
    if (!func_ptr) {
        return {};
    }
    const auto& func = *func_ptr;
    if (func.params_signature.empty()) {
        return format_hover_block(
            std::format("{}.{} -> {}", module_name, func_name, func.return_type));
    }
    return format_hover_block(std::format("{}.{}{} -> {}", module_name, func_name,
                                          func.params_signature, func.return_type));
}

// Resolve hover for a user namespace function (e.g., "MyModule.helper").
[[nodiscard]] std::string hover_user_ns_function(const std::string& ns_name,
                                                 const std::string& func_name,
                                                 const AnalysisResult& result) {
    const SymbolLookup lookup{result};
    const auto qualified = ns_name + "." + func_name;
    auto func_ref = lookup.find_function(qualified);
    if (!func_ref) {
        return {};
    }
    auto doc_ref = lookup.find_doc_comment(qualified);
    return format_hover_block(func_ref->signature, doc_ref ? *doc_ref : std::string_view{});
}

// Resolve hover for a record field access (e.g., "person.name").
// Uses scope-aware variable type resolution to find the record type.
[[nodiscard]] std::string hover_record_field(const std::string& var_name,
                                             const std::string& field_name, int line,
                                             const AnalysisResult& result) {
    const auto resolved = resolve_variable_type(result, var_name, line);
    if (!resolved.has_value()) {
        return {};
    }
    const SymbolLookup lookup{result};
    auto rec_ref = lookup.find_record(resolved->type_name);
    if (!rec_ref) {
        return {};
    }
    for (const auto& [fname, ftype] : rec_ref->fields) {
        if (fname == field_name) {
            return format_hover_block(std::format("(field) {}: {}", fname, ftype));
        }
    }
    return {};
}

// Resolve hover for a choice variant access (e.g., "Color.Red").
[[nodiscard]] std::string hover_choice_variant(const std::string& type_name,
                                               const std::string& variant_name,
                                               const AnalysisResult& result) {
    const SymbolLookup lookup{result};
    auto ch_ref = lookup.find_choice_variants(type_name);
    if (!ch_ref) {
        return {};
    }
    for (const auto& v : *ch_ref) {
        if (v == variant_name) {
            return format_hover_block(std::format("(variant) {}.{}", type_name, variant_name));
        }
    }
    return {};
}

// Build a Hover response from markdown content and a token's source range.
// Centralises the common pattern of formatting hover text + range wrapping.
// The token range is converted from codepoint columns to the client's UTF-16
// columns via `encoder` before it is emitted.
[[nodiscard]] JsonValue make_hover_response(std::string_view markdown_content, const Token& token,
                                            const PositionEncoder& encoder) {
    const auto tok_range = encoder.to_utf16(token_range(token));
    return lsp_builders::hover(markdown_content, tok_range.start.line, tok_range.start.character,
                               tok_range.end.line, tok_range.end.character);
}

// ═══════════════════════════════════════════════════════════
// Dot-access pattern detection
// ═══════════════════════════════════════════════════════════

// Check whether the token at `idx` is the member part of a dot-access
// pattern (Identifier.Identifier), used for qualified hover resolution.
[[nodiscard]] bool is_dot_access_pattern(const std::vector<Token>& tokens, std::size_t idx) {
    return idx >= 2 && tokens[idx].type == TokenType::Identifier &&
           tokens[idx - 1].type == TokenType::Dot && tokens[idx - 2].type == TokenType::Identifier;
}

// ═══════════════════════════════════════════════════════════
// Hover resolution helper
//
// Uses the shared lsp_builders::try_resolve template to chain
// multiple resolver functions, returning the first non-empty result.
// ═══════════════════════════════════════════════════════════

using lsp_builders::try_resolve;

} // namespace

// ═══════════════════════════════════════════════════════════
// Top-level hover handler
// ═══════════════════════════════════════════════════════════

JsonValue LspHoverHandler::handle_hover(const JsonValue& params) {
    return ctx_.resolve_token_context(params, [&](const TokenContext& ctx) -> JsonValue {
        const auto& [uri, result, idx, token_ptr, cache] = ctx;
        const AnalysisResultView view{*result};
        const auto& tokens = view.tokens();
        const auto& token = *token_ptr;

        std::string hover_text;

        // resolve_token_context only invokes this handler once it has verified
        // a cached analysis exists, so `result` is always a valid pointer here.
        // The static analyzer cannot prove that: find_analysis() is defined in a
        // separate translation unit, so the analyzer conjures its optional_ref
        // return value and models the dereferenced pointer as undefined. Suppress
        // the resulting false-positive null-dereference reports across both
        // resolver blocks below.
        // NOLINTBEGIN(clang-analyzer-core.NullDereference)

        // ── Identifier hover resolution ──
        // Try each resolver in priority order for plain identifiers.
        if (is_identifier(token)) {
            hover_text =
                try_resolve([&] { return hover_stdlib_module(token, ctx_.stdlib_registry); },
                            [&] { return hover_user_function(token, *result); },
                            [&] { return hover_local_variable(token, *result); },
                            [&] { return hover_function_parameter(token, *result); },
                            [&] { return hover_record_type(token, *result); },
                            [&] { return hover_choice_type(token, *result); },
                            [&] { return hover_interface_type(token, *result); },
                            [&] { return hover_user_namespace(token, *result); });
        }

        // ── Dot-access hover resolution ──
        // Pattern: Identifier.Identifier — the hovered token is after the dot.
        if (hover_text.empty() && is_dot_access_pattern(tokens, idx)) {
            const auto& prefix = tokens[idx - 2].lexeme;
            const auto& member = token.lexeme;

            hover_text = try_resolve(
                [&] { return hover_stdlib_function(prefix, member, ctx_.stdlib_registry); },
                [&] { return hover_user_ns_function(prefix, member, *result); },
                [&] { return hover_record_field(prefix, member, token.location.line, *result); },
                [&] { return hover_choice_variant(prefix, member, *result); });
        }
        // NOLINTEND(clang-analyzer-core.NullDereference)

        // ── Keyword and literal hover ──
        if (hover_text.empty()) {
            hover_text = keyword_hover(token);
        }

        if (hover_text.empty()) {
            return {}; // null = no hover
        }

        return make_hover_response(hover_text, token, result->encoder());
    });
}

} // namespace luma::lsp
