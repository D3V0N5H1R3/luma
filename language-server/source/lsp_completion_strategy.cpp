#include "lsp_completion_strategy.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "analysis/lexer/token_type.hpp"
#include "lsp_brace_matcher.hpp"
#include "lsp_constants.hpp"
#include "lsp_keyword_catalog.hpp"
#include "lsp_path_utils.hpp"
#include "lsp_stdlib_registry.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_symbol_lookup.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_types.hpp"
#include "protocol/uri_utils.hpp"

namespace luma::lsp {

using luma::protocol::uri_to_path;

namespace {

// Types that accept a generic type parameter (shown as snippet with <$1>).
constexpr auto k_generic_types = std::array{
    std::string_view{"array"},     std::string_view{"dictionary"}, std::string_view{"optional"},
    std::string_view{"result"},    std::string_view{"channel"},    std::string_view{"task"},
    std::string_view{"reference"},
};

struct MatchArmTemplate {
    std::string_view label;
    std::string_view detail;
    std::string_view snippet_insert; // empty → use label as plain insert
    std::string_view plain_insert;   // empty → use label as plain insert
    std::string_view sort_text;
};

constexpr auto k_match_arm_templates = std::array{
    MatchArmTemplate{.label = "success(value)",
                     .detail = "(result pattern)",
                     .snippet_insert = "success(${1:value})",
                     .plain_insert = "success()",
                     .sort_text = "1success"},
    MatchArmTemplate{.label = "failure(error)",
                     .detail = "(result pattern)",
                     .snippet_insert = "failure(${1:error})",
                     .plain_insert = "failure()",
                     .sort_text = "1failure"},
    MatchArmTemplate{.label = "some(value)",
                     .detail = "(optional pattern)",
                     .snippet_insert = "some(${1:value})",
                     .plain_insert = "some()",
                     .sort_text = "1some"},
    MatchArmTemplate{.label = "none",
                     .detail = "(optional pattern)",
                     .snippet_insert = "",
                     .plain_insert = "",
                     .sort_text = "1none"},
    MatchArmTemplate{.label = "else",
                     .detail = "(default pattern)",
                     .snippet_insert = "",
                     .plain_insert = "",
                     .sort_text = "2else"},
    MatchArmTemplate{.label = "true",
                     .detail = "(boolean pattern)",
                     .snippet_insert = "",
                     .plain_insert = "",
                     .sort_text = "1true"},
    MatchArmTemplate{.label = "false",
                     .detail = "(boolean pattern)",
                     .snippet_insert = "",
                     .plain_insert = "",
                     .sort_text = "1false"},
};

} // anonymous namespace

JsonValue::ArrayType collect_include_completions(const std::filesystem::path& base_dir,
                                                 const std::string& partial_path) {
    std::filesystem::path search_dir = base_dir;
    std::string prefix_filter;
    // Accept either separator so `include "sub\fi` completes like `"sub/fi`.
    const auto last_slash = partial_path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        search_dir = base_dir / utf8_path(partial_path.substr(0, last_slash));
        prefix_filter = partial_path.substr(last_slash + 1);
    } else {
        prefix_filter = partial_path;
    }

    {
        std::error_code ec;
        const auto canonical_base = std::filesystem::weakly_canonical(base_dir, ec);
        const auto canonical_search = std::filesystem::weakly_canonical(search_dir, ec);
        // Reject any search path that escapes base_dir. A raw string-prefix test
        // would wrongly accept a sibling whose path merely starts with the same
        // characters (e.g. base "/p/proj" vs "/p/proj-secret"), so compare by
        // path components: lexically_relative yields ".." as its first element
        // exactly when canonical_search is outside canonical_base (and an empty
        // path when the two share no common root).
        const auto relative = canonical_search.lexically_relative(canonical_base);
        if (ec || relative.empty() || *relative.begin() == std::filesystem::path("..")) {
            return {};
        }
    }

    JsonValue::ArrayType items;
    try {
        if (std::filesystem::exists(search_dir) && std::filesystem::is_directory(search_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(search_dir)) {
                const auto name = path_to_utf8(entry.path().filename());
                if (name.starts_with(".")) {
                    continue;
                }
                if (!prefix_filter.empty() && !name.starts_with(prefix_filter)) {
                    continue;
                }

                if (entry.is_directory()) {
                    items.push_back(CompletionItemBuilder()
                                        .label(name + "/")
                                        .kind(constants::completion_kind::module_)
                                        .detail("(directory)")
                                        .build());
                } else if (name.ends_with(".luma")) {
                    const auto rel_dir = last_slash != std::string::npos
                                             ? partial_path.substr(0, last_slash + 1)
                                             : "";
                    items.push_back(CompletionItemBuilder()
                                        .label(rel_dir + name)
                                        .kind(constants::completion_kind::field)
                                        .detail("(luma source)")
                                        .build());
                }
            }
        }
    } catch (const std::filesystem::filesystem_error&) { // NOLINT(bugprone-empty-catch)
        // Filesystem errors during completion are non-fatal.
    }

    return items;
}

namespace {

// True when `prefix` ends inside the parameter list of a function declaration
// (`function <type> <name>(`) at a parameter's type: right after the `(` or a
// top-level `,`, with at most a partial word typed. Once a space follows the
// word, the cursor is at the parameter's name instead.
[[nodiscard]] bool is_at_parameter_type(std::string_view prefix) {
    static const std::regex function_keyword{R"((?:^|\s)function\s)"};
    std::size_t decl_start = std::string_view::npos;
    for (std::cregex_iterator it{prefix.data(), prefix.data() + prefix.size(), function_keyword},
         end;
         it != end; ++it) {
        decl_start =
            static_cast<std::size_t>(it->position()) + static_cast<std::size_t>(it->length());
    }
    if (decl_start == std::string_view::npos) {
        return false;
    }
    const auto open = prefix.find('(', decl_start);
    if (open == std::string_view::npos) {
        return false;
    }

    int paren_depth = 0;
    int angle_depth = 0;
    std::size_t segment_start = open + 1;
    for (std::size_t i = open; i < prefix.size(); ++i) {
        const char c = prefix[i];
        if (c == '(') {
            ++paren_depth;
        } else if (c == ')') {
            if (--paren_depth == 0) {
                return false; // parameter list already closed
            }
        } else if (c == '<') {
            ++angle_depth;
        } else if (c == '>' && angle_depth > 0) {
            --angle_depth;
        } else if (c == ',' && paren_depth == 1 && angle_depth == 0) {
            segment_start = i + 1;
        }
    }
    if (paren_depth != 1 || angle_depth != 0) {
        return false;
    }
    static const std::regex partial_word{R"(\s*\w*)"};
    const auto segment = prefix.substr(segment_start);
    return std::regex_match(segment.begin(), segment.end(), partial_word);
}

// Returns true if the cursor is at a position where only a type can be
// written. Luma annotations are type-first, so these are: a function's return
// type (`function |`), a parameter's type (`function integer f(|`,
// `…, |`), the type after `mutable` (`mutable |`), a generic argument
// (`array<|`, `dictionary<array<|`), and a generic bound (`<T: |`). A
// statement start is not one of them — it may begin a declaration or an
// expression — and neither is `name: |`, which is a named argument's value.
[[nodiscard]] bool is_valid_type_annotation_context(const std::string& line,
                                                    std::size_t cursor_col) {
    const std::string_view prefix{line.data(), std::min(cursor_col, line.size())};
    static const std::regex return_type{R"((?:^|\s)function\s+\w*$)"};
    static const std::regex mutable_type{R"((?:^|\s)mutable\s+\w*$)"};
    // The head of a generic must be a type — a built-in generic type, a cast,
    // or a PascalCase user type — so a comparison typed without spaces
    // (`if i<n`) is not mistaken for one. Type parameters are PascalCase too.
    static const std::regex generic_argument{
        R"(\b(?:array|dictionary|set|queue|stack|optional|result|reference|channel|)"
        R"(task|key_value_store|downcast|trusted_downcast|[A-Z]\w*)<(?:[\w<>\s]*,)?\s*\w*$)"};
    static const std::regex generic_bound{R"(<\s*[A-Z]\w*\s*:\s*\w*$)"};
    const auto matches = [&](const std::regex& pattern) {
        return std::regex_search(prefix.begin(), prefix.end(), pattern);
    };
    return matches(return_type) || matches(mutable_type) || matches(generic_argument) ||
           matches(generic_bound) || is_at_parameter_type(prefix);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// CompletionStrategyChain
// ═══════════════════════════════════════════════════════════

void CompletionStrategyChain::add(std::unique_ptr<CompletionStrategy> strategy) {
    strategies_.push_back(std::move(strategy));
}

std::optional<JsonValue>
CompletionStrategyChain::try_provide(const LspCompletionHandler::CompletionContext& ctx,
                                     LspHandlerContext& handler_ctx) const {
    for (const auto& strategy : strategies_) {
        if (auto result = strategy->try_provide(ctx, handler_ctx)) {
            return result;
        }
    }
    return std::nullopt;
}

// ═══════════════════════════════════════════════════════════
// IncludePathCompletionStrategy
// ═══════════════════════════════════════════════════════════

std::optional<JsonValue>
IncludePathCompletionStrategy::try_provide(const LspCompletionHandler::CompletionContext& ctx,
                                           LspHandlerContext& /*handler_ctx*/) const {
    static constexpr std::string_view k_include_keyword = "include \"";
    const auto include_pos = ctx.line_prefix.find(k_include_keyword);
    if (include_pos == std::string::npos) {
        return std::nullopt;
    }

    const auto path_start = include_pos + k_include_keyword.size();
    const std::string partial_path{ctx.line_prefix.substr(path_start)};

    if (!is_safe_include_path(partial_path)) {
        return JsonValue(JsonValue::ArrayType{});
    }

    const auto file_path_opt = uri_to_path(ctx.uri);
    if (!file_path_opt.has_value()) {
        return JsonValue(JsonValue::ArrayType{});
    }
    const auto base_dir = utf8_path(*file_path_opt).parent_path();

    auto items = collect_include_completions(base_dir, partial_path);

    if (!items.empty()) {
        return JsonValue(std::move(items));
    }

    return std::nullopt;
}

// ═══════════════════════════════════════════════════════════
// TypeAnnotationCompletionStrategy
// ═══════════════════════════════════════════════════════════

std::optional<JsonValue>
TypeAnnotationCompletionStrategy::try_provide(const LspCompletionHandler::CompletionContext& ctx,
                                              LspHandlerContext& handler_ctx) const {
    const std::size_t line_end = ctx.text.find('\n', ctx.line_start);
    const std::string current_line{ctx.text, ctx.line_start,
                                   (line_end == std::string::npos ? ctx.text.size() : line_end) -
                                       ctx.line_start};
    if (!is_valid_type_annotation_context(current_line, ctx.cursor_offset - ctx.line_start)) {
        return std::nullopt;
    }

    const auto cached = handler_ctx.find_analysis(ctx.uri);
    JsonValue::ArrayType items;
    const bool snippets = handler_ctx.configuration.snippet_support();

    const auto builtin_types = get_type_keywords();

    for (const auto& [name, detail] : builtin_types) {
        std::string insert_text;

        if (snippets && std::ranges::any_of(k_generic_types, [&](auto sv) { return name == sv; })) {
            insert_text = name + "<$0>";
        }
        items.push_back(CompletionItemBuilder()
                            .label(name)
                            .kind(constants::completion_kind::class_)
                            .detail("(" + detail + ")")
                            .insert_text(insert_text)
                            .insert_text_format(snippets && !insert_text.empty()
                                                    ? constants::insert_text_format::snippet
                                                    : constants::insert_text_format::plaintext)
                            .sort_text(std::string(constants::sort_priority::highest) + name)
                            .build());
    }

    if (cached) {
        for (const auto& [name, rec_info] : cached->semantic.symbols.record_definitions) {
            if (name.starts_with(k_interface_record_prefix)) {
                continue;
            }
            items.push_back(CompletionItemBuilder()
                                .label(name)
                                .kind(constants::completion_kind::struct_)
                                .detail("(record type)")
                                .sort_text(std::string(constants::sort_priority::high) + name)
                                .build());
        }

        for (const auto& [name, variants] : cached->semantic.symbols.choice_variants) {
            items.push_back(CompletionItemBuilder()
                                .label(name)
                                .kind(constants::completion_kind::enum_)
                                .detail("(choice type)")
                                .sort_text(std::string(constants::sort_priority::high) + name)
                                .build());
        }
    }

    if (!items.empty()) {
        return JsonValue(std::move(items));
    }

    return std::nullopt;
}

// ═══════════════════════════════════════════════════════════
// MatchArmCompletionStrategy
// ═══════════════════════════════════════════════════════════

std::optional<JsonValue>
MatchArmCompletionStrategy::try_provide(const LspCompletionHandler::CompletionContext& ctx,
                                        LspHandlerContext& handler_ctx) const {
    auto trimmed_prefix = std::string(ctx.line_prefix);
    const auto first_non_ws = trimmed_prefix.find_first_not_of(" \t");
    if (first_non_ws != std::string::npos) {
        trimmed_prefix = trimmed_prefix.substr(first_non_ws);
    }
    if (!trimmed_prefix.starts_with("case ") || ctx.text[ctx.cursor_offset - 1] == '.') {
        return std::nullopt;
    }

    const auto cached = handler_ctx.find_analysis(ctx.uri);
    if (!cached) {
        return std::nullopt;
    }

    const bool snippets = handler_ctx.configuration.snippet_support();
    JsonValue::ArrayType items;

    for (const auto& [choice_name, variants] : cached->semantic.symbols.choice_variants) {
        for (const auto& variant_name : variants) {
            std::string qualified = choice_name;
            qualified += '.';
            qualified += variant_name;
            const std::string insert_text = snippets ? qualified + "($0)" : qualified;
            items.push_back(
                CompletionItemBuilder()
                    .label(qualified)
                    .kind(constants::completion_kind::enum_)
                    .detail("(variant pattern)")
                    .insert_text(insert_text)
                    .insert_text_format(snippets ? constants::insert_text_format::snippet
                                                 : constants::insert_text_format::plaintext)
                    .sort_text(std::string(constants::sort_priority::highest) + qualified)
                    .build());
        }
    }

    for (const auto& tmpl : k_match_arm_templates) {
        const bool has_snippet = !tmpl.snippet_insert.empty();
        const std::string insert =
            has_snippet ? std::string(snippets ? tmpl.snippet_insert : tmpl.plain_insert)
                        : std::string(tmpl.label);
        auto builder = CompletionItemBuilder()
                           .label(tmpl.label)
                           .kind(constants::completion_kind::keyword)
                           .detail(tmpl.detail)
                           .insert_text(insert)
                           .sort_text(tmpl.sort_text);
        if (has_snippet && snippets) {
            builder.insert_text_format(constants::insert_text_format::snippet);
        }
        items.push_back(builder.build());
    }

    if (!items.empty()) {
        return JsonValue(std::move(items));
    }

    return std::nullopt;
}

// ═══════════════════════════════════════════════════════════
// PipeCompletionStrategy
// ═══════════════════════════════════════════════════════════

namespace {

// Per-item fields for a pipe (`|>`) completion entry.  Bundled into a struct
// so call sites use designated initializers instead of a long, ambiguous
// positional argument list (and can omit the optional documentation/data/
// filter-text fields entirely).
struct PipeCompletionItemParams {
    std::string label;
    std::string detail;
    std::string sort_text;
    std::string documentation;
    std::string data;
    std::string filter_text;
};

void build_pipe_completion_item(const PipeCompletionItemParams& params, bool snippets,
                                std::size_t max_items, JsonValue::ArrayType& output) {
    if (output.size() >= max_items) {
        return;
    }
    const std::string insert_text =
        snippets ? " " + params.label + "($0)" : " " + params.label + "()";
    auto builder = CompletionItemBuilder()
                       .label(params.label)
                       .kind(constants::completion_kind::function)
                       .detail(params.detail)
                       .insert_text(insert_text)
                       .insert_text_format(snippets ? constants::insert_text_format::snippet
                                                    : constants::insert_text_format::plaintext)
                       .sort_text(params.sort_text);
    if (!params.documentation.empty()) {
        builder.documentation(params.documentation);
    }
    if (!params.data.empty()) {
        builder.data(params.data);
    }
    if (!params.filter_text.empty()) {
        builder.filter_text(params.filter_text);
    }
    output.push_back(builder.build());
}

} // anonymous namespace

std::optional<JsonValue>
PipeCompletionStrategy::try_provide(const LspCompletionHandler::CompletionContext& ctx,
                                    LspHandlerContext& handler_ctx) const {
    if (ctx.cursor_offset < 2 || ctx.text[ctx.cursor_offset - 1] != '>' ||
        ctx.text[ctx.cursor_offset - 2] != '|') {
        return std::nullopt;
    }

    constexpr std::size_t k_max_pipe_completions = 100;
    const bool snippets = handler_ctx.configuration.snippet_support();
    JsonValue::ArrayType items;
    items.reserve(k_max_pipe_completions);

    for (const auto& [module_name, funcs] : handler_ctx.stdlib_registry.modules()) {
        for (const auto& func : funcs) {
            if (func.is_constant || func.params_signature.empty()) {
                continue;
            }
            const std::string qualified = module_name + "." + func.name;
            build_pipe_completion_item(
                PipeCompletionItemParams{
                    .label = qualified,
                    .detail = std::format("-> {}", func.return_type),
                    .sort_text = std::string(constants::sort_priority::highest) + qualified,
                    .data = "stdlib:" + qualified,
                    .filter_text = func.name,
                },
                snippets, k_max_pipe_completions, items);
        }
    }

    const auto cached = handler_ctx.find_analysis(ctx.uri);
    if (cached) {
        for (const auto& [name, info] : cached->semantic.symbols.user_functions) {
            if (info.parameters.empty()) {
                continue;
            }
            if (util::is_qualified_name(name)) {
                continue;
            }
            const std::string detail =
                info.return_type.empty() ? "(user function)" : "-> " + info.return_type;
            build_pipe_completion_item(
                PipeCompletionItemParams{
                    .label = name,
                    .detail = detail,
                    .sort_text = std::string(constants::sort_priority::high) + name,
                    .documentation = info.signature,
                },
                snippets, k_max_pipe_completions, items);
        }
    }

    return JsonValue(std::move(items));
}

// ═══════════════════════════════════════════════════════════
// RecordFieldCompletionStrategy
// ═══════════════════════════════════════════════════════════

std::optional<JsonValue>
RecordFieldCompletionStrategy::try_provide(const LspCompletionHandler::CompletionContext& ctx,
                                           LspHandlerContext& handler_ctx) const {
    const auto cached = handler_ctx.find_analysis(ctx.uri);
    if (!cached) {
        return std::nullopt;
    }

    const auto& tokens = cached->semantic.tokens;
    const int luma_line = ctx.line + 1;

    const auto brace_idx =
        find_enclosing_brace_token_index(tokens, luma_line, cached->metadata.line_index);
    if (!brace_idx || *brace_idx == 0) {
        return std::nullopt;
    }
    const std::size_t brace_token_idx = *brace_idx;

    const auto& name_tok = tokens[brace_token_idx - 1];
    if (name_tok.type != TokenType::Identifier) {
        return std::nullopt;
    }
    const auto& record_name = name_tok.lexeme;

    const SymbolLookup lookup{*cached};
    auto rec_ref = lookup.find_record(record_name);
    if (!rec_ref || record_name.starts_with(k_interface_record_prefix)) {
        return std::nullopt;
    }

    const auto already_set = collect_assigned_fields(tokens, brace_token_idx, luma_line);
    const bool snippets = handler_ctx.configuration.snippet_support();

    JsonValue::ArrayType field_items;
    for (const auto& [fname, ftype] : rec_ref->fields) {
        if (already_set.contains(fname)) {
            continue;
        }
        const std::string insert_text = snippets ? fname + " = $0" : fname + " = ";
        field_items.push_back(
            CompletionItemBuilder()
                .label(fname)
                .kind(constants::completion_kind::field)
                .detail(": " + ftype)
                .insert_text(insert_text)
                .insert_text_format(snippets ? constants::insert_text_format::snippet
                                             : constants::insert_text_format::plaintext)
                .sort_text(std::string(constants::sort_priority::highest) + fname)
                .build());
    }
    return JsonValue(std::move(field_items));
}

// ═══════════════════════════════════════════════════════════
// Factory
// ═══════════════════════════════════════════════════════════

CompletionStrategyChain create_default_strategy_chain() {
    CompletionStrategyChain chain;
    chain.add(std::make_unique<IncludePathCompletionStrategy>());
    chain.add(std::make_unique<TypeAnnotationCompletionStrategy>());
    chain.add(std::make_unique<RecordFieldCompletionStrategy>());
    chain.add(std::make_unique<MatchArmCompletionStrategy>());
    chain.add(std::make_unique<PipeCompletionStrategy>());
    return chain;
}

} // namespace luma::lsp
