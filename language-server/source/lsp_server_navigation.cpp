#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "analysis/lexer/token_type.hpp"
#include "json/json.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_analysis_view.hpp"
#include "lsp_constants.hpp"
#include "lsp_definition_resolver.hpp"
#include "lsp_identifier_collector.hpp"
#include "lsp_navigation_handler.hpp"
#include "lsp_optional_ref.hpp"
#include "lsp_param_extraction.hpp"
#include "lsp_params.hpp"
#include "lsp_response_helpers.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_symbol_handler.hpp"
#include "lsp_symbol_lookup.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_token_utils.hpp"

namespace luma::lsp {

// LSP DocumentHighlightKind values.
namespace highlight_kind {
[[maybe_unused]] constexpr int text = 1;
constexpr int read = 2;
constexpr int write = 3;
} // namespace highlight_kind

using luma::protocol::path_to_uri;
using luma::protocol::uri_to_path;
using response::make_empty_array_result;
using response::make_location_result;

// ═══════════════════════════════════════════════════════════
// Go to definition
// ═══════════════════════════════════════════════════════════

JsonValue LspNavigationHandler::handle_definition(const JsonValue& params) {
    return ctx_.resolve_token_context(params, [&](const TokenContext& ctx) -> JsonValue {
        const auto& [uri, result, idx, token_ptr, cache] = ctx;
        const auto& token = *token_ptr;
        const auto& tokens = result->semantic.tokens;
        const std::string qualified_name = build_qualified_name(tokens, idx);

        const DefinitionResolver resolver(
            uri, *result, tokens, [this](const std::string& u) { return ctx_.find_analysis(u); },
            [](const std::vector<Token>& toks, const SourceLocation& loc) {
                return find_block_range(toks, loc);
            },
            cache->entries(), cache);

        auto location = resolver.resolve(qualified_name, token, idx);
        if (location) {
            // resolver ranges are in codepoint columns; convert to the client's
            // UTF-16 columns using the target document's analysed source.
            if (auto target = ctx_.find_analysis(location->uri)) {
                location->range = target->to_wire(location->range);
            }
            return serialise_location(*location);
        }

        return {};
    });
}

// ═══════════════════════════════════════════════════════════
// Find references — extracted helper (LS-14)
// ═══════════════════════════════════════════════════════════

namespace {

// Check whether the token at `idx` is a write (assignment target or declaration site).
[[nodiscard]] bool is_write_occurrence(const std::vector<Token>& tokens, std::size_t idx,
                                       const AnalysisResultView& view,
                                       const std::string& target_name) {
    // Assignment target: next token is '='.
    if (idx + 1 < tokens.size() && tokens[idx + 1].type == TokenType::Equals) {
        return true;
    }
    // Declaration site: preceded by 'mutable', 'function', 'for', or 'catch'.
    if (idx > 0) {
        const auto prev_type = tokens[idx - 1].type;
        if (prev_type == TokenType::Mutable || prev_type == TokenType::Function ||
            prev_type == TokenType::For || prev_type == TokenType::Catch) {
            return true;
        }
    }
    // Definition site: the stored definition location is anchored at the
    // declaration keyword, not the name, so compare the token against the
    // resolved NAME range rather than the raw keyword-anchored location.
    auto def_ref = view.find_definition(target_name);

    if (!def_ref) {
        return false;
    }

    const Range name_range = find_declaration_name_range(tokens, def_ref->location, target_name);
    const Range tok_rng = token_range(tokens[idx]);

    return tok_rng.start.line == name_range.start.line &&
           tok_rng.start.character == name_range.start.character;
}

// Bundled parameters for collect_references_from() to reduce argument count.
struct ReferenceCollectionContext {
    const std::string& origin_uri;
    const std::string& target_name;
    const std::string& target_ns;
    bool is_local;
    const std::optional<std::string>& enclosing_fn;
    bool include_declaration;
};

// Collect all reference locations of `target_name` within a single document.
// Appends matching Location JSON values to `out`.
void collect_references_from(const std::string& doc_uri, const AnalysisResult& doc_result,
                             const ReferenceCollectionContext& ref_ctx, JsonValue::ArrayType& out) {
    auto indices = collect_scoped_occurrences(
        doc_result, ref_ctx.target_name, doc_uri,
        ScopedOccurrenceFilter{.namespace_prefix = ref_ctx.target_ns,
                               .is_local = ref_ctx.is_local,
                               .enclosing_function = ref_ctx.enclosing_fn,
                               .origin_uri = ref_ctx.origin_uri,
                               .include_declaration = ref_ctx.include_declaration});

    for (const std::size_t tok_idx : indices) {
        out.push_back(make_location_result(
            doc_uri, doc_result.to_wire(token_range(doc_result.semantic.tokens[tok_idx]))));
    }
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// Find references
// ═══════════════════════════════════════════════════════════

JsonValue LspNavigationHandler::handle_references(const JsonValue& params) {
    const auto empty_array = make_empty_array_result();

    // Use typed ReferenceParams for structured extraction of
    // textDocument/position and the context.includeDeclaration flag.
    auto ref_params = params::ReferenceParams::from_json(params);
    if (!ref_params) {
        throw InvalidParamsError("Missing or malformed textDocument/position params");
    }
    const bool include_declaration = ref_params->include_declaration;

    return ctx_.resolve_token_context(
        params,
        [&](const TokenContext& ctx) -> JsonValue {
            const auto& [uri, result, idx, token_ptr, cache] = ctx;
            const auto& tokens = result->semantic.tokens;
            const auto& target_token = *token_ptr;
            const std::string target_name = target_token.lexeme;

            // Detect namespace prefix: if the cursor token is preceded by "Ns.",
            // restrict references to occurrences with the same prefix.
            std::string target_ns;
            if (auto ns = extract_namespace_prefix(tokens, idx)) {
                target_ns = std::move(*ns);
            }

            const bool is_local = is_local_variable(*result, target_name);
            const auto enclosing_fn =
                is_local ? find_enclosing_function(*result, target_token.location.line)
                         : std::nullopt;

            JsonValue::ArrayType locations;

            const ReferenceCollectionContext ref_ctx{.origin_uri = uri,
                                                     .target_name = target_name,
                                                     .target_ns = target_ns,
                                                     .is_local = is_local,
                                                     .enclosing_fn = enclosing_fn,
                                                     .include_declaration = include_declaration};

            if (is_local) {
                collect_references_from(uri, *result, ref_ctx, locations);
            } else {
                for (const auto& [doc_uri, doc_result] : cache->entries()) {
                    collect_references_from(doc_uri, doc_result, ref_ctx, locations);
                }
            }

            return JsonValue(std::move(locations));
        },
        empty_array);
}

// ═══════════════════════════════════════════════════════════
// Document highlight
// ═══════════════════════════════════════════════════════════

JsonValue LspNavigationHandler::handle_document_highlight(const JsonValue& params) {
    return ctx_.resolve_token_context(
        params,
        [&](const TokenContext& ctx) -> JsonValue {
            const auto& [uri, result, idx, token_ptr, cache] = ctx;
            const AnalysisResultView view{*result};
            const auto& tokens = view.tokens();
            const auto& target = *token_ptr;
            if (target.type != TokenType::Identifier) {
                return make_empty_array_result();
            }

            // DocumentHighlightKind: 1 = Text, 2 = Read, 3 = Write.
            // We mark assignment targets and declarations as Write, everything else as Read.
            JsonValue::ArrayType highlights;

            // Use identifier index for O(1) lookup.
            auto occurrences = view.find_identifier_occurrences(target.lexeme);
            if (!occurrences) {
                return make_empty_array_result();
            }

            // For local variables, use block-level scope to filter highlights.
            const bool is_local = view.is_local_variable(target.lexeme);

            for (const std::size_t i : *occurrences) {
                const int tok_line_1 = tokens[i].location.line; // 1-based

                // Skip occurrences outside the cursor's block scope.
                if (is_local && !view.is_in_scope(target.lexeme, tok_line_1)) {
                    continue;
                }

                const int kind = is_write_occurrence(tokens, i, view, target.lexeme)
                                     ? highlight_kind::write
                                     : highlight_kind::read;

                highlights.emplace_back(JsonValue::ObjectType{
                    {"range", serialise_range(result->to_wire(token_range(tokens[i])))},
                    {"kind", JsonValue(static_cast<int64_t>(kind))},
                });
            }

            return JsonValue(std::move(highlights));
        },
        make_empty_array_result());
}

// ═══════════════════════════════════════════════════════════
// Type definition
// ═══════════════════════════════════════════════════════════

JsonValue LspNavigationHandler::handle_type_definition(const JsonValue& params) {
    return ctx_.resolve_token_context(params, [&](const TokenContext& ctx) -> JsonValue {
        const auto& [uri, result, idx, token_ptr, cache] = ctx;
        const auto& token = *token_ptr;
        if (token.type != TokenType::Identifier) {
            return {};
        }

        // Find the type name of this variable (scope-aware lookup).
        std::string type_name;

        const auto resolved_td = resolve_variable_type(*result, token.lexeme, token.location.line);
        if (resolved_td.has_value()) {
            type_name = resolved_td->type_name;
        }

        if (type_name.empty()) {
            if (auto def = result->find_definition(token.lexeme)) {
                type_name = def->type_string;
            }
        }

        if (type_name.empty()) {
            return {};
        }

        // Look up the type definition (record, choice, interface, type alias).
        for (const auto& [doc_uri, doc_result] : cache->entries()) {
            if (auto td = doc_result.find_definition(type_name)) {
                const auto& ts = td->type_string;
                if (constants::type_kind::is_type_definition(ts)) {
                    return make_location_result(
                        doc_uri, doc_result.to_wire(find_declaration_name_range(
                                     doc_result.semantic.tokens, td->location, type_name)));
                }
            }
        }

        return {};
    });
}

// ═══════════════════════════════════════════════════════════
// Go to implementation
// ═══════════════════════════════════════════════════════════

JsonValue LspNavigationHandler::handle_implementation(const JsonValue& params) {
    return ctx_.resolve_token_context(
        params,
        [&](const TokenContext& ctx) -> JsonValue {
            const auto& target = *ctx.token;
            if (target.type != TokenType::Identifier) {
                return make_empty_array_result();
            }

            // Check if this identifier is an interface name.
            JsonValue::ArrayType locations;

            for (const auto& [doc_uri, doc_result] : ctx.cache->entries()) {
                auto impl_it =
                    doc_result.semantic.symbols.interface_implementations.find(target.lexeme);
                if (impl_it == doc_result.semantic.symbols.interface_implementations.end()) {
                    continue;
                }

                for (const auto& rec_name : impl_it->second) {
                    auto def = doc_result.find_definition(rec_name);
                    if (!def) {
                        continue;
                    }

                    locations.push_back(make_location_result(
                        doc_uri, doc_result.to_wire(find_declaration_name_range(
                                     doc_result.semantic.tokens, def->location, rec_name))));
                }
            }

            return JsonValue(std::move(locations));
        },
        make_empty_array_result());
}

// ═══════════════════════════════════════════════════════════
// Document links (include paths)
// ═══════════════════════════════════════════════════════════

JsonValue LspNavigationHandler::handle_document_link(const JsonValue& params) {
    const auto uri_opt = extraction::extract_text_document_uri(params);
    if (!uri_opt) {
        return make_empty_array_result();
    }
    const auto& uri = *uri_opt;

    auto state = ctx_.acquire_read_lock();
    const auto result = ctx_.find_analysis(uri);
    if (!result) {
        return make_empty_array_result();
    }

    JsonValue::ArrayType links;

    // Find include string literals in the token stream and map them
    // to the resolved file paths from include_literals.
    const auto& tokens = result->semantic.tokens;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type != TokenType::Include || i + 1 >= tokens.size()) {
            continue;
        }

        // The token after 'include' should be a string literal.
        if (tokens[i + 1].type != TokenType::StringLiteral) {
            continue;
        }

        const auto& str_tok = tokens[i + 1];

        // Resolve the include path relative to the document URI.
        const auto file_path_opt = uri_to_path(uri);
        if (!file_path_opt.has_value()) {
            continue;
        }
        const auto base_dir = std::filesystem::path(*file_path_opt).parent_path();

        // Strip quotes from the lexeme.
        std::string inc_path = str_tok.lexeme;
        if (inc_path.size() >= 2 && inc_path.front() == '"' && inc_path.back() == '"') {
            inc_path = inc_path.substr(1, inc_path.size() - 2);
        }

        const auto resolved = base_dir / std::filesystem::path(inc_path);

        // Only create a link if the target file actually exists.
        if (!std::filesystem::exists(resolved)) {
            continue;
        }

        const auto target_uri = path_to_uri(resolved.string());

        links.emplace_back(JsonValue::ObjectType{
            {"range", serialise_range(result->to_wire(token_range(str_tok)))},
            {"target", JsonValue(target_uri)},
        });
    }

    return JsonValue(std::move(links));
}

// ═══════════════════════════════════════════════════════════
// Selection range
// ═══════════════════════════════════════════════════════════

JsonValue LspNavigationHandler::handle_selection_range(const JsonValue& params) {
    const auto uri_opt = extraction::extract_text_document_uri(params);
    if (!uri_opt || !params.has("positions")) {
        return JsonValue(JsonValue::ArrayType{});
    }
    const auto& uri = *uri_opt;

    auto state = ctx_.acquire_read_lock();
    const auto cached = ctx_.find_analysis(uri);

    // Build selection ranges from brace nesting in the token stream.
    // For each requested position, find enclosing brace pairs from
    // innermost to outermost, forming a parent chain.
    const std::vector<Token>* token_ptr = nullptr;
    if (cached) {
        token_ptr = &cached->semantic.tokens;
    }

    // Build a list of brace-pair ranges from the token stream.
    struct BracePair {
        int start_line;
        int start_col;
        int end_line;
        int end_col;
    };

    std::vector<BracePair> brace_pairs;
    if (token_ptr != nullptr) {
        std::vector<std::pair<int, int>> stack; // (line, col)
        for (const auto& tok : *token_ptr) {
            if (tok.type == TokenType::LeftBrace) {
                const int l = tok.location.line - 1;
                const int c = tok.location.column - 1 - lexeme_column_width(tok.lexeme);
                stack.emplace_back(l, c);
            } else if (tok.type == TokenType::RightBrace && !stack.empty()) {
                auto [sl, sc] = stack.back();
                stack.pop_back();
                const int el = tok.location.line - 1;
                const int ec = tok.location.column - 1;
                brace_pairs.push_back(
                    {.start_line = sl, .start_col = sc, .end_line = el, .end_col = ec});
            }
        }
    }

    // Also get the full document range.
    const auto* doc_ptr = ctx_.doc_store.get_content(state.token(), uri);
    int total_lines = 0;
    if (doc_ptr != nullptr) {
        for (const char c : *doc_ptr) {
            if (c == '\n') {
                ++total_lines;
            }
        }
    }

    JsonValue::ArrayType result;
    for (const auto& pos : params["positions"].as_array()) {
        const int pl = util::clamp_to_int(pos["line"].as_integer());
        const int pc = util::clamp_to_int(pos["character"].as_integer());
        // Brace-pair columns are codepoint-based; convert the incoming UTF-16
        // cursor column to codepoint space before the containment test.
        const int pc_cp = cached ? cached->to_codepoint_col(pl, pc) : pc;

        // Find all brace pairs containing this position, sorted innermost first.
        std::vector<const BracePair*> enclosing;
        for (const auto& bp : brace_pairs) {
            if (bp.start_line < pl || (bp.start_line == pl && bp.start_col <= pc_cp)) {
                if (bp.end_line > pl || (bp.end_line == pl && bp.end_col >= pc_cp)) {
                    enclosing.push_back(&bp);
                }
            }
        }

        // Sort innermost (smallest range) first.
        std::ranges::sort(enclosing, [](const BracePair* a, const BracePair* b) {
            if (a->start_line != b->start_line) {
                return a->start_line > b->start_line;
            }
            if (a->start_col != b->start_col) {
                return a->start_col > b->start_col;
            }
            if (a->end_line != b->end_line) {
                return a->end_line < b->end_line;
            }
            return a->end_col < b->end_col;
        });

        // Also find the token at cursor for the innermost range.
        std::optional<Range> tok_range;
        if (token_ptr != nullptr) {
            auto idx = find_token_at(*cached, pl, pc);
            if (idx.has_value()) {
                tok_range = cached->to_wire(token_range((*token_ptr)[*idx]));
            }
        }

        // Build the chain: token → brace pairs → full document.
        JsonValue current = JsonValue(JsonValue::ObjectType{
            {"range", serialise_range(Range{.start = Position{.line = 0, .character = 0},
                                            .end = Position{.line = total_lines, .character = 0}})},
        });

        // Outermost brace pair to innermost.
        for (auto bp : std::views::reverse(enclosing)) {
            current = JsonValue(JsonValue::ObjectType{
                {"range", serialise_range(cached->to_wire(Range{
                              .start = Position{.line = bp->start_line, .character = bp->start_col},
                              .end = Position{.line = bp->end_line, .character = bp->end_col}}))},
                {"parent", std::move(current)},
            });
        }

        // Innermost: token range.
        if (tok_range.has_value()) {
            current = JsonValue(JsonValue::ObjectType{
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access): guarded by has_value() above.
                {"range", serialise_range(*tok_range)},
                {"parent", std::move(current)},
            });
        }

        result.push_back(std::move(current));
    }

    return JsonValue(std::move(result));
}

} // namespace luma::lsp
