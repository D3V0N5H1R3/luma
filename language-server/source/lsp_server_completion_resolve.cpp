#include <algorithm>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

#include "analysis/lexer/token_type.hpp"
#include "json/json.hpp"
#include "lsp_brace_matcher.hpp"
#include "lsp_completion_handler.hpp"
#include "lsp_constants.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_stdlib_registry.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_symbol_lookup.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_types.hpp"
#include "symbols/qualified_name.hpp"

namespace luma::lsp {

namespace {

constexpr std::size_t k_stdlib_prefix_len = std::string_view("stdlib:").size();

} // namespace

std::optional<JsonValue>
LspCompletionHandler::try_module_completion(const std::string& module_name) const {
    const auto mod_ptr = ctx_.stdlib_registry.find_module(module_name);
    if (!mod_ptr) {
        return std::nullopt;
    }

    // Build completion items.
    JsonValue::ArrayType items;

    for (const auto& func : *mod_ptr) {
        const int kind = func.is_constant ? constants::completion_kind::constant
                                          : constants::completion_kind::function;

        const bool use_snippet = !func.is_constant && ctx_.configuration.snippet_support();
        const std::string insert_text = call_snippet_insert_text(func.name, use_snippet);
        const int insert_fmt = use_snippet ? constants::insert_text_format::snippet
                                           : constants::insert_text_format::plaintext;

        items.push_back(CompletionItemBuilder()
                            .label(func.name)
                            .kind(kind)
                            .detail(std::format("-> {}", func.return_type))
                            .insert_text(insert_text)
                            .insert_text_format(insert_fmt)
                            .sort_text(std::format("stdlib:{}.{}", module_name, func.name))
                            .data(std::format("stdlib:{}.{}", module_name, func.name))
                            .build());
    }

    return JsonValue(std::move(items));
}

namespace {

// Resolves dot completion for user-defined functions/methods.
[[nodiscard]] std::optional<JsonValue>
try_resolve_function_completion(const std::string& symbol_name, const std::string& detail,
                                const std::string& member_name, bool snippet_support) {
    const std::string insert_text = call_snippet_insert_text(member_name, snippet_support);
    return CompletionItemBuilder()
        .label(member_name)
        .kind(constants::completion_kind::function)
        .detail(detail)
        .documentation(symbol_name)
        .insert_text(insert_text)
        .insert_text_format(snippet_support ? constants::insert_text_format::snippet
                                            : constants::insert_text_format::plaintext)
        .build();
}

// Resolves dot completion for record fields.
[[nodiscard]] std::optional<JsonValue>
try_resolve_record_completion(const RecordInfo& rec, const std::string& member_name,
                              [[maybe_unused]] bool snippet_support) {
    for (const auto& [fname, ftype] : rec.fields) {
        if (fname == member_name) {
            return CompletionItemBuilder()
                .label(fname)
                .kind(constants::completion_kind::field)
                .detail(": " + ftype)
                .build();
        }
    }
    return std::nullopt;
}

// Resolves dot completion for choice variant constructors.
[[nodiscard]] std::optional<JsonValue>
try_resolve_choice_completion(const std::vector<std::string>& variants,
                              const std::string& variant_name, bool snippet_support) {
    if (std::ranges::find(variants, variant_name) == variants.end()) {
        return std::nullopt;
    }
    const std::string insert_text = call_snippet_insert_text(variant_name, snippet_support);
    return CompletionItemBuilder()
        .label(variant_name)
        .kind(constants::completion_kind::enum_)
        .detail("(variant)")
        .insert_text(insert_text)
        .insert_text_format(snippet_support ? constants::insert_text_format::snippet
                                            : constants::insert_text_format::plaintext)
        .build();
}

// Returns non-empty item list if `identifier` is a user-defined namespace.
[[nodiscard]] std::optional<JsonValue::ArrayType>
try_namespace_completions(const AnalysisResult& cached, const std::string& identifier,
                          bool snippet_support) {
    const std::string ns_prefix = identifier + ".";
    JsonValue::ArrayType ns_items;

    for (const auto& [fname, finfo] : cached.semantic.symbols.user_functions) {
        if (fname.size() > ns_prefix.size() && fname.starts_with(ns_prefix)) {
            const auto short_name = fname.substr(ns_prefix.size());

            // Only include direct members, not nested-ns members.
            if (!util::is_qualified_name(short_name)) {
                const std::string detail =
                    finfo.return_type.empty() ? "(user function)" : "-> " + finfo.return_type;
                if (auto item = try_resolve_function_completion(finfo.signature, detail, short_name,
                                                                snippet_support)) {
                    ns_items.push_back(std::move(*item));
                }
            }
        }
    }

    if (!ns_items.empty()) {
        return ns_items;
    }
    return std::nullopt;
}

// Returns non-empty item list if `identifier` resolves to a record type at `line`.
[[nodiscard]] std::optional<JsonValue::ArrayType>
try_record_field_completions(const AnalysisResult& cached, const std::string& identifier,
                             int line) {
    const auto resolved_for_field = resolve_variable_type(cached, identifier, line + 1);

    if (!resolved_for_field.has_value()) {
        return std::nullopt;
    }

    const auto& var_type_for_field = resolved_for_field->type_name;
    const auto& rec_defs = cached.semantic.symbols.record_definitions;

    const auto rec_it = rec_defs.find(var_type_for_field);

    if (rec_it == rec_defs.end()) {
        return std::nullopt;
    }

    JsonValue::ArrayType field_items;

    for (const auto& [fname2, ftype] : rec_it->second.fields) {
        if (auto item = try_resolve_record_completion(rec_it->second, fname2, false)) {
            field_items.push_back(std::move(*item));
        }
    }

    if (!field_items.empty()) {
        return field_items;
    }
    return std::nullopt;
}

// Returns non-empty item list if `identifier` is a choice type.
[[nodiscard]] std::optional<JsonValue::ArrayType>
try_choice_variant_completions(const AnalysisResult& cached, const std::string& identifier,
                               bool snippet_support) {
    const auto choice_it = cached.semantic.symbols.choice_variants.find(identifier);
    if (choice_it == cached.semantic.symbols.choice_variants.end()) {
        return std::nullopt;
    }

    // Collect already-matched variants by locating `case ChoiceName.Variant`
    // patterns via the identifier index instead of scanning every token.
    std::unordered_set<std::string> matched_variants;
    const auto& tokens = cached.semantic.tokens;
    const auto id_it = cached.metadata.identifier_index.find(identifier);
    if (id_it != cached.metadata.identifier_index.end()) {
        for (const std::size_t p : id_it->second) {
            // tokens[p] is the choice-name identifier; check for the
            // surrounding `case <name> . <variant>` shape.
            if (p >= 1 && p + 2 < tokens.size() && tokens[p - 1].type == TokenType::Case &&
                tokens[p + 1].type == TokenType::Dot &&
                tokens[p + 2].type == TokenType::Identifier) {
                matched_variants.insert(tokens[p + 2].lexeme);
            }
        }
    }

    JsonValue::ArrayType variant_items;
    for (const auto& variant_name : choice_it->second) {
        if (matched_variants.contains(variant_name)) {
            continue; // skip already-matched variants
        }
        if (auto item =
                try_resolve_choice_completion(choice_it->second, variant_name, snippet_support)) {
            variant_items.push_back(std::move(*item));
        }
    }

    if (!variant_items.empty()) {
        return variant_items;
    }
    return std::nullopt;
}

} // anonymous namespace

std::optional<JsonValue> LspCompletionHandler::try_dot_completion(const CompletionContext& ctx,
                                                                  const std::string& identifier) {
    const auto cached = ctx_.find_analysis(ctx.uri);
    if (!cached) {
        return std::nullopt;
    }
    const bool snippets = ctx_.configuration.snippet_support();
    if (auto items = try_namespace_completions(*cached, identifier, snippets);
        items && !items->empty()) {
        return JsonValue(std::move(*items));
    }
    if (auto items = try_record_field_completions(*cached, identifier, ctx.line);
        items && !items->empty()) {
        return JsonValue(std::move(*items));
    }
    if (auto items = try_choice_variant_completions(*cached, identifier, snippets);
        items && !items->empty()) {
        return JsonValue(std::move(*items));
    }
    return std::nullopt;
}

// ═══════════════════════════════════════════════════════════
// Completion item resolve — free-function helpers (LS-5)
//
// These are pure functions that receive all dependencies as parameters
// so they can be unit-tested independently of server state.
// ═══════════════════════════════════════════════════════════

namespace {

// Enrich a CompletionItem JSON object with a markdown documentation field.
[[nodiscard]] JsonValue enrich_with_documentation(const JsonValue& item,
                                                  const std::string& doc_markdown) {
    JsonValue enriched = item;
    enriched.as_object()["documentation"] = JsonValue(JsonValue::ObjectType{
        {"kind", JsonValue("markdown")},
        {"value", JsonValue(doc_markdown)},
    });
    return enriched;
}

// Build stdlib function documentation markdown.
[[nodiscard]] std::string build_stdlib_doc(const std::string& module_name,
                                           const StdlibFunction& fn) {
    return std::format("```luma\n{}.{}{} -> {}\n```", module_name, fn.name,
                       fn.params_signature.empty() ? "()" : fn.params_signature,
                       fn.return_type.empty() ? "void" : fn.return_type);
}

// Build user function documentation markdown.
[[nodiscard]] std::string build_user_function_doc(const UserFunctionInfo& fn) {
    std::string doc = std::format("```luma\n{}\n```", fn.signature);
    if (!fn.parameters.empty()) {
        doc += "\n\n**Parameters:**\n";
        for (const auto& p : fn.parameters) {
            doc += std::format("- `{}`: `{}`\n", p.name,
                               p.type_string.empty() ? util::k_unknown_type : p.type_string);
        }
    }
    return doc;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// Completion item resolve
// ═══════════════════════════════════════════════════════════

JsonValue LspCompletionHandler::handle_completion_resolve(const JsonValue& params) {
    // The client sends back the full CompletionItem. We enrich it with
    // documentation if available.  The "data" field carries the module
    // name + function name for stdlib lookups.
    if (!params.is_object()) {
        return params;
    }

    // Clone the item to a mutable object.
    auto result = params;

    auto data = luma::json::try_extract_field<std::string>(params, "data");
    if (!data) {
        return result;
    }

    // data format: "stdlib:Module.function" or "user:function_name"
    if (data->starts_with("stdlib:")) {
        // LOCK INVARIANT: ctx_.stdlib_registry is immutable after construction —
        // no lock required for read-only access.
        const auto qualified = data->substr(k_stdlib_prefix_len);
        const auto split = split_module(qualified);
        if (!split) {
            return result;
        }

        const std::string module_name{split->first};

        // O(1) lookup via the registry's function index instead of scanning
        // the module's function list.
        const auto fn_ref = ctx_.stdlib_registry.find_function(qualified);
        if (!fn_ref) {
            return result;
        }
        return enrich_with_documentation(params, build_stdlib_doc(module_name, *fn_ref));
    } else if (data->starts_with("user:")) {
        const auto func_name = data->substr(5);

        // LOCK INVARIANT: Iterating ctx_.analysis_cache requires shared_lock(ctx_.state_mutex).
        // Semantic token data is stored in semantic_token_cache_, not accessed here.
        auto state = ctx_.acquire_read_lock();
        for (const auto& [doc_uri, doc_result] : state.cache().entries()) {
            auto fn_it = doc_result.semantic.symbols.user_functions.find(func_name);
            if (fn_it == doc_result.semantic.symbols.user_functions.end()) {
                continue;
            }

            return enrich_with_documentation(params, build_user_function_doc(fn_it->second));
        }
    }

    return result;
}

} // namespace luma::lsp
