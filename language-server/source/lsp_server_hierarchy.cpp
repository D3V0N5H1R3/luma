#include <format>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "analysis/lexer/token_type.hpp"
#include "json/json.hpp"
#include "lsp_constants.hpp"
#include "lsp_hierarchy_handler.hpp"
#include "lsp_identifier_collector.hpp"
#include "lsp_lock_utils.hpp"
#include "lsp_position_utils.hpp"
#include "lsp_response_helpers.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_symbol_handler.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_types.hpp"
#include "symbols/qualified_name.hpp"

namespace luma::lsp {

using response::make_empty_array_result;
using response::make_location_result;

// ═══════════════════════════════════════════════════════════
// Shared hierarchy helpers
// ═══════════════════════════════════════════════════════════

namespace {

// Serialise a vector of Range objects into a JsonValue::ArrayType suitable
// for the `fromRanges` field in call-hierarchy results.
[[nodiscard]] JsonValue::ArrayType serialise_ranges(const std::vector<Range>& ranges) {
    JsonValue::ArrayType result;
    result.reserve(ranges.size());
    for (const auto& r : ranges) {
        result.push_back(serialise_range(r));
    }
    return result;
}

// Searches `items` for entries whose name contains `query_lower`
// (case-insensitive substring match) and appends up to `limit` matching
// SymbolInformation objects to `out`.
//
// `build_fn` is called as `build_fn(name, item)` for each candidate and must
// return std::optional<JsonValue>: nullopt skips the entry, a value includes it.
template <typename Range, typename BuildFn>
void collect_matching_symbols(const Range& items, std::string_view query_lower, std::size_t limit,
                              std::vector<JsonValue>& out, BuildFn build_fn) {
    for (const auto& [name, item] : items) {
        if (out.size() >= limit) {
            break;
        }
        if (!query_lower.empty() && !util::contains_ci_lower(name, query_lower)) {
            continue;
        }
        if (auto sym = build_fn(name, item)) {
            out.push_back(std::move(*sym));
        }
    }
}

// Collect function symbols from a single document that match query_lower,
// returning at most max_results items.
[[nodiscard]] std::vector<JsonValue> collect_function_symbols(const std::string& doc_uri,
                                                              const AnalysisResult& doc_result,
                                                              const std::string& query_lower,
                                                              std::size_t max_results) {
    std::vector<JsonValue> symbols;
    collect_matching_symbols(
        doc_result.semantic.symbols.user_functions, query_lower, max_results, symbols,
        [&](const std::string& name, const auto& info) -> std::optional<JsonValue> {
            return JsonValue(JsonValue::ObjectType{
                {"name", JsonValue(name)},
                {"kind", JsonValue(static_cast<int64_t>(to_lsp_symbol_kind(SymbolKind::Function)))},
                {"location", make_location_result(
                                 doc_uri, doc_result.to_wire(find_declaration_name_range(
                                              doc_result.semantic.tokens, info.location, name)))},
            });
        });
    return symbols;
}

// Collect definition symbols (records, choices, type aliases, etc.) from a
// single document that match query_lower, returning at most max_results items.
// Skips any name already listed as a user function to avoid duplicates.
[[nodiscard]] std::vector<JsonValue> collect_definition_symbols(const std::string& doc_uri,
                                                                const AnalysisResult& doc_result,
                                                                const std::string& query_lower,
                                                                std::size_t max_results) {
    std::vector<JsonValue> symbols;
    collect_matching_symbols(
        doc_result.semantic.symbols.definitions, query_lower, max_results, symbols,
        [&](const std::string& name, const auto& def) -> std::optional<JsonValue> {
            if (doc_result.semantic.symbols.user_functions.contains(name)) {
                return std::nullopt;
            }
            auto kind = SymbolKind::Variable;
            if (def.type_string == "record") {
                kind = SymbolKind::Struct;
            } else if (def.type_string == "choice") {
                kind = SymbolKind::Enum;
            } else if (def.type_string == "interface") {
                kind = SymbolKind::Interface;
            } else if (def.type_string == "namespace") {
                kind = SymbolKind::Namespace;
            } else if (def.type_string == "type_alias") {
                kind = SymbolKind::TypeAlias;
            }
            return JsonValue(JsonValue::ObjectType{
                {"name", JsonValue(name)},
                {"kind", JsonValue(static_cast<int64_t>(to_lsp_symbol_kind(kind)))},
                {"location", make_location_result(
                                 doc_uri, doc_result.to_wire(find_declaration_name_range(
                                              doc_result.semantic.tokens, def.location, name)))},
            });
        });
    return symbols;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// Workspace symbol search
// ═══════════════════════════════════════════════════════════

JsonValue LspSymbolHandler::handle_workspace_symbol(const JsonValue& params) {
    const auto query = params.get_or<std::string>("query", "");

    const std::string query_lower = util::to_lower(query);

    // If indexing is still in progress, results may be incomplete.
    const bool partial = ctx_.workspace.is_indexing();

    return with_shared_state(
        ctx_.state_mutex, ctx_.doc_store, ctx_.analysis_cache, ctx_.pending_uris,
        [&](ReadStateLock& state) -> JsonValue {
            JsonValue::ArrayType symbols;
            for (const auto& [doc_uri, doc_result] : state.cache().entries()) {
                if (symbols.size() >= constants::limits::max_workspace_symbols) {
                    break;
                }

                // Case-insensitive substring match — user functions first, then definitions.
                auto fn_syms = collect_function_symbols(doc_uri, doc_result, query_lower,
                                                        constants::limits::max_workspace_symbols -
                                                            symbols.size());
                for (auto& s : fn_syms) {
                    symbols.push_back(std::move(s));
                }

                if (symbols.size() < constants::limits::max_workspace_symbols) {
                    auto def_syms = collect_definition_symbols(
                        doc_uri, doc_result, query_lower,
                        constants::limits::max_workspace_symbols - symbols.size());
                    for (auto& s : def_syms) {
                        symbols.push_back(std::move(s));
                    }
                }
            }

            // If the workspace is still being indexed, prepend a synthetic entry so the
            // user can see that results are incomplete.
            if (partial && !symbols.empty()) {
                symbols.insert(
                    symbols.begin(),
                    JsonValue(JsonValue::ObjectType{
                        {"name", JsonValue("⏳ Indexing in progress — results may be incomplete")},
                        {"kind", JsonValue(static_cast<int64_t>(
                                     to_lsp_symbol_kind(SymbolKind::Namespace)))},
                        {"location", make_location_result(
                                         "", Range{.start = Position{.line = 0, .character = 0},
                                                   .end = Position{.line = 0, .character = 0}})},
                    }));
            }

            return JsonValue(std::move(symbols));
        });
}

// ═══════════════════════════════════════════════════════════
// Call hierarchy
// ═══════════════════════════════════════════════════════════

JsonValue LspHierarchyHandler::handle_call_hierarchy_prepare(const JsonValue& params) {
    return ctx_.resolve_token_context(
        params,
        [&](const TokenContext& ctx) -> JsonValue {
            const auto& [uri, result, idx, token_ptr, cache] = ctx;
            const auto& tokens = result->semantic.tokens;
            const auto& token = *token_ptr;

            if (token.type != TokenType::Identifier) {
                return make_empty_array_result();
            }

            // Check if it's a user function.
            const auto& user_fns = result->semantic.symbols.user_functions;
            auto fn_it = user_fns.find(token.lexeme);

            if (fn_it == user_fns.end()) {
                // Try namespace-qualified lookup: if the cursor is on "add" and
                // user_functions contains "Namespace.add", build the qualified
                // name by checking the preceding tokens for "Namespace" ".".
                std::string qualified_name;
                if (idx >= 2 && tokens[idx - 1].type == TokenType::Dot) {
                    const auto& ns_token = tokens[idx - 2];
                    if (ns_token.type == TokenType::Identifier) {
                        qualified_name = ns_token.lexeme + "." + token.lexeme;
                    }
                }
                if (!qualified_name.empty()) {
                    fn_it = user_fns.find(qualified_name);
                }
                if (fn_it == user_fns.end()) {
                    // Also try the short-name reverse map.
                    auto short_it =
                        result->semantic.symbols.function_short_names.find(token.lexeme);
                    if (short_it != result->semantic.symbols.function_short_names.end() &&
                        !short_it->second.empty()) {
                        fn_it = user_fns.find(short_it->second.front());
                    }
                }
                if (fn_it == user_fns.end()) {
                    return make_empty_array_result();
                }
            }

            const auto& info = fn_it->second;
            return JsonValue(JsonValue::ArrayType{
                lsp_builders::hierarchy_item(fn_it->first, SymbolKind::Function, uri,
                                             result->to_wire(find_declaration_name_range(
                                                 tokens, info.location, fn_it->first)))});
        },
        make_empty_array_result());
}

JsonValue LspHierarchyHandler::handle_call_hierarchy_incoming(const JsonValue& params) {
    if (!params.has("item") || !params["item"].has("name")) {
        return make_empty_array_result();
    }

    const auto target_name = params["item"]["name"].as_string();

    const JsonValue::ArrayType result;

    // Search all cached documents for functions that call target_name.
    return with_shared_state(
        ctx_.state_mutex, ctx_.doc_store, ctx_.analysis_cache, ctx_.pending_uris,
        [&](ReadStateLock& state) -> JsonValue {
            JsonValue::ArrayType result;

            for (const auto& [doc_uri, doc_result] : state.cache().entries()) {
                for (const auto& [caller_name, callees] :
                     doc_result.semantic.functions.call_graph) {
                    if (!callees.contains(target_name)) {
                        continue;
                    }

                    auto caller_it = doc_result.semantic.symbols.user_functions.find(caller_name);
                    if (caller_it == doc_result.semantic.symbols.user_functions.end()) {
                        continue;
                    }

                    const auto& info = caller_it->second;
                    const auto caller_range = doc_result.to_wire(find_declaration_name_range(
                        doc_result.semantic.tokens, info.location, caller_name));

                    // Find call sites of target_name within the caller's body range.
                    int scope_start = 0;
                    int scope_end = std::numeric_limits<int>::max();
                    auto br_it =
                        doc_result.semantic.functions.function_body_ranges.find(caller_name);
                    if (br_it != doc_result.semantic.functions.function_body_ranges.end()) {
                        scope_start = br_it->second.first;
                        scope_end = br_it->second.second;
                    }
                    const auto enc = doc_result.encoder();
                    auto from_ranges = serialise_ranges(collect_identifier_ranges(
                        doc_result.semantic.tokens, target_name, scope_start, scope_end, &enc));

                    result.emplace_back(JsonValue::ObjectType{
                        {"from", lsp_builders::hierarchy_item(caller_name, SymbolKind::Function,
                                                              doc_uri, caller_range)},
                        {"fromRanges", JsonValue(std::move(from_ranges))},
                    });
                }
            }

            return JsonValue(std::move(result));
        });
}

JsonValue LspHierarchyHandler::handle_call_hierarchy_outgoing(const JsonValue& params) {
    if (!params.has("item") || !params["item"].has("name")) {
        return make_empty_array_result();
    }

    const auto caller_name = params["item"]["name"].as_string();

    const JsonValue::ArrayType result;

    // Find the caller in all cached documents.
    return with_shared_state(
        ctx_.state_mutex, ctx_.doc_store, ctx_.analysis_cache, ctx_.pending_uris,
        [&](ReadStateLock& state) -> JsonValue {
            JsonValue::ArrayType result;

            // Build name→(uri, name range) index for O(1) callee lookups. The
            // range is computed from each defining document's own tokens so the
            // selection covers the callee's name, not its declaration keyword.
            std::unordered_map<std::string, std::pair<std::string, Range>> function_locations;
            for (const auto& [doc_uri, doc_result] : state.cache().entries()) {
                for (const auto& [name, info] : doc_result.semantic.symbols.user_functions) {
                    function_locations[name] = {
                        doc_uri, doc_result.to_wire(find_declaration_name_range(
                                     doc_result.semantic.tokens, info.location, name))};
                }
            }

            for (const auto& [doc_uri, doc_result] : state.cache().entries()) {
                auto graph_it = doc_result.semantic.functions.call_graph.find(caller_name);
                if (graph_it == doc_result.semantic.functions.call_graph.end()) {
                    continue;
                }

                for (const auto& callee_name : graph_it->second) {
                    // For qualified callee names ("Ns.func"), extract the short
                    // name for token comparison since token lexemes are bare names.
                    const std::string callee_short{qualified_member(callee_name)};

                    // Find callee definition via pre-built index.
                    std::string callee_uri = doc_uri;
                    auto callee_range = Range{
                        .start = Position{.line = 0, .character = 0},
                        .end = Position{.line = 0, .character = lexeme_column_width(callee_short)}};

                    auto fn_loc_it = function_locations.find(callee_name);
                    if (fn_loc_it != function_locations.end()) {
                        callee_uri = fn_loc_it->second.first;
                        callee_range = fn_loc_it->second.second;
                    }

                    // Find call sites within the caller's document.
                    const auto enc = doc_result.encoder();
                    auto from_ranges = serialise_ranges(
                        collect_identifier_ranges(doc_result.semantic.tokens, callee_short, 0,
                                                  std::numeric_limits<int>::max(), &enc));

                    result.emplace_back(JsonValue::ObjectType{
                        {"to", lsp_builders::hierarchy_item(callee_name, SymbolKind::Function,
                                                            callee_uri, callee_range)},
                        {"fromRanges", JsonValue(std::move(from_ranges))},
                    });
                }
            }

            return JsonValue(std::move(result));
        });
}

// ═══════════════════════════════════════════════════════════
// Type hierarchy
// ═══════════════════════════════════════════════════════════

JsonValue LspHierarchyHandler::handle_type_hierarchy_prepare(const JsonValue& params) {
    return ctx_.resolve_token_context(params, [&](const TokenContext& ctx) -> JsonValue {
        const auto& [uri, result, idx, token_ptr, cache] = ctx;
        const auto& target = *token_ptr;
        if (target.type != TokenType::Identifier) {
            return {};
        }

        // Only offer type hierarchy for records and interfaces.
        auto def = result->find_definition(target.lexeme);
        if (!def) {
            return {};
        }
        if (def->type_string != "record" && def->type_string != "interface") {
            return {};
        }

        const auto kind =
            (def->type_string == "interface") ? SymbolKind::Interface : SymbolKind::Struct;

        JsonValue::ArrayType items;
        items.push_back(lsp_builders::hierarchy_item(
            target.lexeme, kind, uri, result->to_wire(token_range(target)), target.lexeme));

        return JsonValue(std::move(items));
    });
}

JsonValue LspHierarchyHandler::handle_type_hierarchy_supertypes(const JsonValue& params) {
    if (!params.is_object() || !params.has("item")) {
        return make_empty_array_result();
    }

    const auto& item = params["item"];
    auto type_name = luma::json::try_extract_field<std::string>(item, "data");
    if (!type_name) {
        return make_empty_array_result();
    }

    // For a record, supertypes are interfaces it implements.
    return with_shared_state(
        ctx_.state_mutex, ctx_.doc_store, ctx_.analysis_cache, ctx_.pending_uris,
        [&](ReadStateLock& state) -> JsonValue {
            JsonValue::ArrayType supertypes;

            for (const auto& [doc_uri, doc_result] : state.cache().entries()) {
                for (const auto& [iface_name, implementors] :
                     doc_result.semantic.symbols.interface_implementations) {
                    for (const auto& rec : implementors) {
                        if (rec == *type_name) {
                            if (auto iface_def = doc_result.find_definition(iface_name)) {
                                supertypes.push_back(lsp_builders::hierarchy_item(
                                    iface_name, SymbolKind::Interface, doc_uri,
                                    doc_result.to_wire(find_declaration_name_range(
                                        doc_result.semantic.tokens, iface_def->location,
                                        iface_name)),
                                    iface_name));
                            }
                        }
                    }
                }
            }

            return JsonValue(std::move(supertypes));
        });
}

JsonValue LspHierarchyHandler::handle_type_hierarchy_subtypes(const JsonValue& params) {
    if (!params.is_object() || !params.has("item")) {
        return make_empty_array_result();
    }

    const auto& item = params["item"];
    auto type_name = luma::json::try_extract_field<std::string>(item, "data");
    if (!type_name) {
        return make_empty_array_result();
    }

    // For an interface, subtypes are records that implement it.
    return with_shared_state(
        ctx_.state_mutex, ctx_.doc_store, ctx_.analysis_cache, ctx_.pending_uris,
        [&](ReadStateLock& state) -> JsonValue {
            JsonValue::ArrayType subtypes;

            for (const auto& [doc_uri, doc_result] : state.cache().entries()) {
                auto impl_it =
                    doc_result.semantic.symbols.interface_implementations.find(*type_name);
                if (impl_it == doc_result.semantic.symbols.interface_implementations.end()) {
                    continue;
                }
                for (const auto& rec_name : impl_it->second) {
                    if (auto rec_def = doc_result.find_definition(rec_name)) {
                        subtypes.push_back(lsp_builders::hierarchy_item(
                            rec_name, SymbolKind::Struct, doc_uri,
                            doc_result.to_wire(find_declaration_name_range(
                                doc_result.semantic.tokens, rec_def->location, rec_name)),
                            rec_name));
                    }
                }
            }

            return JsonValue(std::move(subtypes));
        });
}

} // namespace luma::lsp
