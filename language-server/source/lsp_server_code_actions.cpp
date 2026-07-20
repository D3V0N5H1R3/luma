#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "json/json.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_code_action_builder.hpp"
#include "lsp_code_action_handler.hpp"
#include "lsp_config.hpp"
#include "lsp_diagnostic_codes.hpp"
#include "lsp_identifier_collector.hpp"
#include "lsp_param_extraction.hpp"
#include "lsp_params.hpp"
#include "lsp_quickfix_handler.hpp"
#include "lsp_response_helpers.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_types.hpp"
#include "symbols/qualified_name.hpp"

namespace luma::lsp {

using extraction::extract_first_quoted_name;
using extraction::extract_quoted_name;
using luma::protocol::uri_to_path;

// ═══════════════════════════════════════════════════════════
// Shared quick-fix helpers
// ═══════════════════════════════════════════════════════════

namespace {

// Find all token indices matching a given name and type.
[[nodiscard]] std::vector<std::size_t>
find_matching_tokens(const std::vector<Token>& tokens, std::string_view name,
                     TokenType type = TokenType::Identifier) {
    std::vector<std::size_t> indices;
    for (std::size_t i{0}; i < tokens.size(); ++i) {
        if (tokens[i].type == type && tokens[i].lexeme == name) {
            indices.push_back(i);
        }
    }
    return indices;
}

// Whether a prefix-underscore fix renames every occurrence of an identifier
// or only the first.
enum class PrefixFixMode {
    AllOccurrences,
    FirstOnly
};

// Build a "Prefix 'name' with '_'" code action that renames identifier
// occurrences within a scope range.  With PrefixFixMode::FirstOnly, only
// the first matching occurrence is renamed (used for shadowed variables
// where the outer binding should remain untouched).
void apply_prefix_underscore_fix(const std::string& name, const std::vector<Token>& tokens,
                                 int scope_start_1based, int scope_end_1based,
                                 const std::string& uri, const Diagnostic& diag,
                                 JsonValue::ArrayType& actions, const PositionEncoder& encoder,
                                 PrefixFixMode mode = PrefixFixMode::AllOccurrences) {
    CodeActionBuilder builder;
    builder.set_title(std::format("Prefix '{}' with '_'", name))
        .set_kind(code_action_kind::k_quickfix)
        .set_diagnostics({diag});

    auto ranges =
        collect_identifier_ranges(tokens, name, scope_start_1based, scope_end_1based, &encoder);

    if (mode == PrefixFixMode::FirstOnly) {
        if (!ranges.empty()) {
            builder.add_edit(uri, ranges.front(), "_" + name);
        }
    } else {
        for (const auto& r : ranges) {
            builder.add_edit(uri, r, "_" + name);
        }
    }

    if (builder.has_edits()) {
        actions.push_back(builder.build());
    }
}

// ═══════════════════════════════════════════════════════════
// QuickFixHandler implementations
//
// Each handler encapsulates matching and code-action generation
// for a single diagnostic kind.  Registered once in the
// QuickFixRegistry and invoked automatically by collect_quick_fixes().
// ═══════════════════════════════════════════════════════════

// ── Immutable assignment (E0006) ──
class ImmutableAssignmentFix final : public QuickFixHandler {
public:
    [[nodiscard]] bool matches(const Diagnostic& diag) const override {
        return diag.code == "E0006" ||
               diag.message.starts_with("cannot assign to immutable variable '");
    }

    [[nodiscard]] std::vector<JsonValue> generate(const Diagnostic& diag,
                                                  const QuickFixContext& ctx) const override {
        std::vector<JsonValue> actions;

        const std::string var_name =
            extract_quoted_name(diag.message, "cannot assign to immutable variable '");
        if (var_name.empty()) {
            return actions;
        }

        const auto& tokens = ctx.analysis.semantic.tokens;
        const auto [scope_start, scope_end] =
            enclosing_scope_range(ctx.analysis, diag.range.start.line + 1);

        for (const auto i : find_matching_tokens(tokens, var_name)) {
            if (tokens[i].location.line >= scope_start && tokens[i].location.line <= scope_end &&
                i + 1 < tokens.size() && tokens[i + 1].type == TokenType::Colon) {
                const bool already_mutable{i > 0 && tokens[i - 1].type == TokenType::Mutable};

                if (!already_mutable) {
                    const int decl_line = token_line_0based(tokens[i]);
                    const int decl_col =
                        token_col_end_0based(tokens[i]) - lexeme_column_width(var_name);

                    actions.push_back(
                        CodeActionBuilder()
                            .set_title(std::format("Add 'mutable' to '{}'", var_name))
                            .set_kind(code_action_kind::k_quickfix)
                            .set_diagnostics({diag})
                            .add_edit(
                                ctx.uri,
                                ctx.analysis.to_wire(Range{
                                    .start = Position{.line = decl_line, .character = decl_col},
                                    .end = Position{.line = decl_line, .character = decl_col}}),
                                "mutable ")
                            .build());
                }

                break;
            }
        }

        return actions;
    }
};

// ── Missing return type ──
class MissingReturnTypeFix final : public QuickFixHandler {
public:
    [[nodiscard]] bool matches(const Diagnostic& diag) const override {
        return diag.message.starts_with("missing return type for function '");
    }

    [[nodiscard]] std::vector<JsonValue> generate(const Diagnostic& diag,
                                                  const QuickFixContext& ctx) const override {
        std::vector<JsonValue> actions;

        const std::string func_name =
            extract_quoted_name(diag.message, "missing return type for function '");
        if (func_name.empty()) {
            return actions;
        }

        const auto& tokens = ctx.analysis.semantic.tokens;
        for (const auto i : find_matching_tokens(tokens, func_name)) {
            if (i > 0 && tokens[i - 1].type == TokenType::Function) {
                const int name_line = token_line_0based(tokens[i]);
                const int name_col =
                    token_col_end_0based(tokens[i]) - lexeme_column_width(tokens[i].lexeme);

                actions.push_back(
                    CodeActionBuilder()
                        .set_title(std::format("Add return type 'void' to '{}'", func_name))
                        .set_kind(code_action_kind::k_quickfix)
                        .set_diagnostics({diag})
                        .add_edit(ctx.uri,
                                  ctx.analysis.to_wire(Range{
                                      .start = Position{.line = name_line, .character = name_col},
                                      .end = Position{.line = name_line, .character = name_col}}),
                                  "void ")
                        .build());
                break;
            }
        }

        return actions;
    }
};

// ── Unused variable (W0001) ──
class UnusedVariableFix final : public QuickFixHandler {
public:
    [[nodiscard]] bool matches(const Diagnostic& diag) const override {
        return diag.code == diagnostic_code::unused_variable ||
               diag.message.starts_with("unused variable '");
    }

    [[nodiscard]] std::vector<JsonValue> generate(const Diagnostic& diag,
                                                  const QuickFixContext& ctx) const override {
        std::vector<JsonValue> actions;

        const std::string var_name = extract_quoted_name(diag.message, "unused variable '");
        if (var_name.empty()) {
            return actions;
        }

        const auto& tokens = ctx.analysis.semantic.tokens;
        const auto [scope_start, scope_end] =
            enclosing_scope_range(ctx.analysis, diag.range.start.line + 1);

        apply_prefix_underscore_fix(var_name, tokens, scope_start, scope_end, ctx.uri, diag,
                                    actions, ctx.analysis.encoder());

        // Also offer to remove the entire declaration line.
        actions.push_back(build_line_removal_action(
            std::format("Remove unused variable '{}'", var_name), ctx.uri, diag));

        return actions;
    }
};

// ── Unused function (W0002) ──
class UnusedFunctionFix final : public QuickFixHandler {
public:
    [[nodiscard]] bool matches(const Diagnostic& diag) const override {
        return diag.code == diagnostic_code::unused_function ||
               (diag.message.starts_with("function '") &&
                diag.message.ends_with("' is declared but never called"));
    }

    [[nodiscard]] std::vector<JsonValue> generate(const Diagnostic& diag,
                                                  const QuickFixContext& ctx) const override {
        std::vector<JsonValue> actions;

        const std::string func_name = extract_quoted_name(diag.message, "function '");
        if (func_name.empty()) {
            return actions;
        }

        const auto& tokens = ctx.analysis.semantic.tokens;

        // Prefix declaration with '_'.
        {
            CodeActionBuilder builder;
            builder.set_title(std::format("Prefix '{}' with '_' to suppress warning", func_name))
                .set_kind(code_action_kind::k_quickfix)
                .set_diagnostics({diag});

            for (const auto ti : find_matching_tokens(tokens, func_name)) {
                if (ti > 0 && tokens[ti - 1].type == TokenType::Function) {
                    builder.add_edit(ctx.uri, ctx.analysis.to_wire(token_range(tokens[ti])),
                                     "_" + func_name);
                    break;
                }
            }

            if (builder.has_edits()) {
                actions.push_back(builder.build());
            }
        }

        // Remove the entire function declaration.
        for (const auto ti : find_matching_tokens(tokens, func_name)) {
            if (ti > 0 && tokens[ti - 1].type == TokenType::Function) {
                int start_line = token_line_0based(tokens[ti - 1]);
                if (ti >= 2 && tokens[ti - 2].type == TokenType::Annotation &&
                    tokens[ti - 2].location.line == tokens[ti - 1].location.line - 1) {
                    start_line = token_line_0based(tokens[ti - 2]);
                }

                int brace_depth = 0;
                int end_line = start_line;
                for (std::size_t tj = ti; tj < tokens.size(); ++tj) {
                    if (tokens[tj].type == TokenType::LeftBrace) {
                        ++brace_depth;
                    } else if (tokens[tj].type == TokenType::RightBrace) {
                        --brace_depth;
                        if (brace_depth == 0) {
                            end_line = token_line_0based(tokens[tj]);
                            break;
                        }
                    }
                }

                actions.push_back(
                    CodeActionBuilder()
                        .set_title(std::format("Remove unused function '{}'", func_name))
                        .set_kind(code_action_kind::k_quickfix)
                        .set_diagnostics({diag})
                        .add_edit(ctx.uri,
                                  Range{.start = Position{.line = start_line, .character = 0},
                                        .end = Position{.line = end_line + 1, .character = 0}},
                                  "")
                        .build());
                break;
            }
        }

        return actions;
    }
};

// ── Mutable never mutated (W0004) ──
class MutableNeverMutatedFix final : public QuickFixHandler {
public:
    [[nodiscard]] bool matches(const Diagnostic& diag) const override {
        return diag.code == diagnostic_code::mutable_never_mutated ||
               (diag.message.starts_with("variable '") &&
                diag.message.ends_with("' is declared mutable but is never mutated")) ||
               (diag.message.starts_with("parameter '") &&
                diag.message.ends_with("' is declared mutable but is never mutated"));
    }

    [[nodiscard]] std::vector<JsonValue> generate(const Diagnostic& diag,
                                                  const QuickFixContext& ctx) const override {
        std::vector<JsonValue> actions;

        std::string var_name = extract_quoted_name(diag.message, "variable '");
        if (var_name.empty()) {
            var_name = extract_quoted_name(diag.message, "parameter '");
        }
        if (var_name.empty()) {
            return actions;
        }

        const auto& tokens = ctx.analysis.semantic.tokens;
        for (const auto i : find_matching_tokens(tokens, var_name)) {
            if (i > 0 && tokens[i - 1].type == TokenType::Mutable) {
                const auto& mut_tok = tokens[i - 1];

                const int tl = token_line_0based(mut_tok);
                const int te = token_col_end_0based(mut_tok);
                const int ts = te - lexeme_column_width(mut_tok.lexeme);

                const int next_col =
                    token_col_end_0based(tokens[i]) - lexeme_column_width(var_name);

                actions.push_back(
                    CodeActionBuilder()
                        .set_title(std::format("Remove 'mutable' from '{}'", var_name))
                        .set_kind(code_action_kind::k_quickfix)
                        .set_diagnostics({diag})
                        .add_edit(ctx.uri,
                                  ctx.analysis.to_wire(
                                      Range{.start = Position{.line = tl, .character = ts},
                                            .end = Position{.line = tl, .character = next_col}}),
                                  "")
                        .build());

                break;
            }
        }

        return actions;
    }
};

// ── Unused parameter (W0003) ──
class UnusedParameterFix final : public QuickFixHandler {
public:
    [[nodiscard]] bool matches(const Diagnostic& diag) const override {
        return diag.code == diagnostic_code::unused_parameter ||
               diag.message.starts_with("unused parameter '");
    }

    [[nodiscard]] std::vector<JsonValue> generate(const Diagnostic& diag,
                                                  const QuickFixContext& ctx) const override {
        std::vector<JsonValue> actions;

        const std::string param_name = extract_quoted_name(diag.message, "unused parameter '");
        if (param_name.empty()) {
            return actions;
        }

        const auto& tokens = ctx.analysis.semantic.tokens;
        const auto [scope_start, scope_end] =
            enclosing_scope_range(ctx.analysis, diag.range.start.line + 1);

        apply_prefix_underscore_fix(param_name, tokens, scope_start, scope_end, ctx.uri, diag,
                                    actions, ctx.analysis.encoder());

        return actions;
    }
};

// ── Self-assignment (W0005) ──
class SelfAssignmentFix final : public QuickFixHandler {
public:
    [[nodiscard]] bool matches(const Diagnostic& diag) const override {
        return diag.code == diagnostic_code::self_assignment ||
               diag.message.starts_with("Self-assignment: '");
    }

    [[nodiscard]] std::vector<JsonValue> generate(const Diagnostic& diag,
                                                  const QuickFixContext& ctx) const override {
        return {build_line_removal_action("Remove self-assignment", ctx.uri, diag)};
    }
};

// ── Unreachable code (W0006) ──
class UnreachableCodeFix final : public QuickFixHandler {
public:
    [[nodiscard]] bool matches(const Diagnostic& diag) const override {
        return diag.code == diagnostic_code::unreachable_code ||
               diag.message.starts_with("unreachable code after ");
    }

    [[nodiscard]] std::vector<JsonValue> generate(const Diagnostic& diag,
                                                  const QuickFixContext& ctx) const override {
        return {build_line_removal_action("Remove unreachable code", ctx.uri, diag)};
    }
};

// ── Shadowed variable (W0012) ──
class ShadowedVariableFix final : public QuickFixHandler {
public:
    [[nodiscard]] bool matches(const Diagnostic& diag) const override {
        return diag.code == diagnostic_code::shadowed_variable ||
               diag.message.ends_with("shadows an outer variable");
    }

    [[nodiscard]] std::vector<JsonValue> generate(const Diagnostic& diag,
                                                  const QuickFixContext& ctx) const override {
        std::vector<JsonValue> actions;

        const auto var_name_opt = extract_first_quoted_name(diag.message);
        if (!var_name_opt) {
            return actions;
        }
        const auto& var_name = *var_name_opt;

        const auto& tokens = ctx.analysis.semantic.tokens;
        const int diag_line_1based = diag.range.start.line + 1;

        // Only rename the first occurrence on the diagnostic line.
        apply_prefix_underscore_fix(var_name, tokens, diag_line_1based, diag_line_1based, ctx.uri,
                                    diag, actions, ctx.analysis.encoder(),
                                    PrefixFixMode::FirstOnly);

        return actions;
    }
};

// ── Auto-import (E0003, E0010) ──
class UndefinedSymbolFix final : public QuickFixHandler {
public:
    [[nodiscard]] bool matches(const Diagnostic& diag) const override {
        return diag.code == "E0003" || diag.code == "E0010" ||
               diag.message.starts_with("undefined variable '") ||
               diag.message.starts_with("undefined function '");
    }

    [[nodiscard]] std::vector<JsonValue> generate(const Diagnostic& diag,
                                                  const QuickFixContext& ctx) const override {
        std::vector<JsonValue> actions;

        const auto& msg = diag.message;
        std::string sym_name;
        if (msg.starts_with("undefined variable '")) {
            sym_name = extract_quoted_name(msg, "undefined variable '");
        } else if (msg.starts_with("undefined function '")) {
            sym_name = extract_quoted_name(msg, "undefined function '");
        } else if (msg.starts_with("unknown type '")) {
            sym_name = extract_quoted_name(msg, "unknown type '");
        }
        if (sym_name.empty()) {
            return actions;
        }

        ctx.analysis_cache.for_each([&](const std::string& other_uri,
                                        const AnalysisResult& other_result) {
            if (other_uri == ctx.uri) {
                return;
            }

            if (other_result.find_definition(sym_name)) {
                auto include_path_opt = uri_to_path(other_uri);
                auto current_path_opt = uri_to_path(ctx.uri);

                if (!include_path_opt.has_value() || !current_path_opt.has_value()) {
                    return;
                }

                auto current_dir = std::filesystem::path(*current_path_opt).parent_path();
                auto rel = std::filesystem::relative(std::filesystem::path(*include_path_opt),
                                                     current_dir);
                auto rel_str = rel.generic_string();

                actions.push_back(CodeActionBuilder()
                                      .set_title(std::format("Add include \"{}\"", rel_str))
                                      .set_kind(code_action_kind::k_quickfix)
                                      .set_diagnostics({diag})
                                      .add_edit(ctx.uri,
                                                Range{.start = Position{.line = 0, .character = 0},
                                                      .end = Position{.line = 0, .character = 0}},
                                                std::format("include \"{}\"\n", rel_str))
                                      .build());
            }
        });

        return actions;
    }
};

// ── Wrap spawn in task_scope ──
class SpawnOutsideTaskScopeFix final : public QuickFixHandler {
public:
    [[nodiscard]] bool matches(const Diagnostic& diag) const override {
        return diag.message.find("spawn outside task_scope") != std::string::npos;
    }

    [[nodiscard]] std::vector<JsonValue> generate(const Diagnostic& diag,
                                                  const QuickFixContext& ctx) const override {
        std::vector<JsonValue> actions;

        const int spawn_line = diag.range.start.line;
        const auto* doc_ptr = ctx.documents.get_content(ctx.lock_token, ctx.uri);
        if (doc_ptr == nullptr) {
            return actions;
        }

        const auto& source = *doc_ptr;
        const auto* lines_ptr = ctx.documents.get_line_starts(ctx.lock_token, ctx.uri);
        if (lines_ptr == nullptr || static_cast<std::size_t>(spawn_line) >= lines_ptr->size()) {
            return actions;
        }

        const auto line_start = (*lines_ptr)[spawn_line];

        std::string indent;
        for (auto p = line_start; p < source.size() && (source[p] == ' ' || source[p] == '\t');
             ++p) {
            indent += source[p];
        }

        std::size_t line_end = source.find('\n', line_start);
        if (line_end == std::string::npos) {
            line_end = source.size();
        }
        const auto line_content =
            source.substr(line_start + indent.size(), line_end - line_start - indent.size());

        actions.push_back(
            CodeActionBuilder()
                .set_title("Wrap in task_scope { }")
                .set_kind(code_action_kind::k_quickfix)
                .set_diagnostics({diag})
                .add_edit(ctx.uri,
                          Range{.start = Position{.line = spawn_line, .character = 0},
                                .end = Position{.line = spawn_line + 1, .character = 0}},
                          std::format("{}task_scope {{\n{}    {}\n{}}}\n", indent, indent,
                                      line_content, indent))
                .build());

        return actions;
    }
};

// ── Fix all unused variables (W0001) ──
// Offer a single "Fix all" action when there are multiple W0001 diagnostics.
// This is not a per-diagnostic handler — it inspects all cached diagnostics.
void collect_fix_all_unused_variables(const std::string& uri, const AnalysisResult& cached,
                                      JsonValue::ArrayType& actions) {
    std::vector<const Diagnostic*> unused_var_diags;
    for (const auto& d : cached.semantic.diagnostics) {
        if (d.code == diagnostic_code::unused_variable) {
            unused_var_diags.push_back(&d);
        }
    }

    if (unused_var_diags.size() <= 1) {
        return;
    }

    CodeActionBuilder builder;
    builder.set_title(std::format("Fix all {} unused variables", unused_var_diags.size()))
        .set_kind("source.fixAll");

    const auto& tokens = cached.semantic.tokens;

    for (const auto* d : unused_var_diags) {
        const std::string var_name = extract_quoted_name(d->message, "unused variable '");
        if (var_name.empty()) {
            continue;
        }

        const auto scope_range = enclosing_scope_range(cached, d->range.start.line + 1);

        const auto enc = cached.encoder();
        for (const auto& r : collect_identifier_ranges(tokens, var_name, scope_range.first,
                                                       scope_range.second, &enc)) {
            builder.add_edit(uri, r, "_" + var_name);
        }
    }

    if (builder.has_edits()) {
        actions.push_back(builder.build());
    }
}

// Build and return the singleton QuickFixRegistry with all handlers.
[[nodiscard]] QuickFixRegistry& get_quickfix_registry() {
    static QuickFixRegistry registry = []() {
        QuickFixRegistry r;
        r.register_handler(std::make_unique<ImmutableAssignmentFix>());
        r.register_handler(std::make_unique<MissingReturnTypeFix>());
        r.register_handler(std::make_unique<UnusedVariableFix>());
        r.register_handler(std::make_unique<UnusedFunctionFix>());
        r.register_handler(std::make_unique<MutableNeverMutatedFix>());
        r.register_handler(std::make_unique<UnusedParameterFix>());
        r.register_handler(std::make_unique<SelfAssignmentFix>());
        r.register_handler(std::make_unique<UnreachableCodeFix>());
        r.register_handler(std::make_unique<ShadowedVariableFix>());
        r.register_handler(std::make_unique<UndefinedSymbolFix>());
        r.register_handler(std::make_unique<SpawnOutsideTaskScopeFix>());
        return r;
    }();
    return registry;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// Code actions (quick fixes)
// ═══════════════════════════════════════════════════════════

JsonValue LspCodeActionHandler::handle_code_action(const JsonValue& params) {
    // Use typed CodeActionParams for structured extraction of
    // textDocument.uri and the request range.
    auto parsed = params::CodeActionParams::from_json(params);
    if (!parsed) {
        return JsonValue(JsonValue::ArrayType{});
    }
    const auto& uri = parsed->uri;

    auto state = ctx_.acquire_read_lock();
    const auto cached = ctx_.find_analysis(uri);

    if (!cached) {
        return JsonValue(JsonValue::ArrayType{});
    }

    // Collect diagnostics in the requested range.
    auto range_diags = extraction::parse_diagnostics(params);

    // Fallback: use cached diagnostics that overlap the request range.
    if (range_diags.empty()) {
        const int req_start = parsed->range.start.line;
        const int req_end = parsed->range.end.line;

        for (const auto& d : cached->semantic.diagnostics) {
            if (d.range.start.line >= req_start && d.range.start.line <= req_end) {
                range_diags.push_back(d);
            }
        }
    }

    JsonValue::ArrayType actions;

    // Delegate to sub-handlers for quick fixes and refactorings.
    collect_quick_fixes(uri, *cached, state.token(), range_diags, actions);
    collect_refactorings(uri, *cached, state.token(), params, actions);

    return JsonValue(std::move(actions));
}

// ═══════════════════════════════════════════════════════════
// Quick fix collection (diagnostic-driven code actions)
// ═══════════════════════════════════════════════════════════
//
// Quick fixes are registered as QuickFixHandler subclasses in
// get_quickfix_registry().  The registry iterates all diagnostics,
// invokes every matching handler, and collects the resulting actions.
//
// The collect_fix_all_unused_variables action operates across all
// cached diagnostics (not per-diagnostic), so it remains separate.

void LspCodeActionHandler::collect_quick_fixes(const std::string& uri, const AnalysisResult& cached,
                                               const LockToken& lock_token,
                                               const std::vector<Diagnostic>& range_diags,
                                               JsonValue::ArrayType& actions) const {
    const QuickFixContext ctx{.uri = uri,
                              .analysis = cached,
                              .documents = ctx_.doc_store,
                              .lock_token = lock_token,
                              .analysis_cache = ctx_.analysis_cache};
    auto registry_actions = get_quickfix_registry().collect_fixes(range_diags, ctx);
    actions.insert(actions.end(), std::make_move_iterator(registry_actions.begin()),
                   std::make_move_iterator(registry_actions.end()));

    collect_fix_all_unused_variables(uri, cached, actions);
}

// ═══════════════════════════════════════════════════════════
// Execute command (code lens actions)
// ═══════════════════════════════════════════════════════════

JsonValue LspCodeActionHandler::handle_execute_command(const JsonValue& params) {
    auto command = luma::json::try_extract_field<std::string>(params, "command");
    if (!command) {
        throw InvalidParamsError("Missing or malformed 'command' parameter");
    }

    if (*command == "luma.showReferences") {
        return execute_show_references(params);
    }

    ctx_.log_message(std::format("executeCommand: unknown command '{}'", *command));
    return {};
}

// Handles the `luma.showReferences` command sent by code lens: resolves the
// identifier at the supplied position and returns every occurrence across all
// cached documents as Location results.
JsonValue LspCodeActionHandler::execute_show_references(const JsonValue& params) {
    const auto& args_val = params.get("arguments");
    if (!args_val.is_array()) {
        ctx_.log_message("luma.showReferences: missing arguments");
        return {};
    }

    const auto& args = args_val.as_array();
    if (args.size() < 2 || !args[0].is_string() || !args[1].is_object()) {
        ctx_.log_message("luma.showReferences: malformed arguments");
        return {};
    }

    const auto uri = args[0].as_string();
    const auto& pos_obj = args[1];
    if (!pos_obj.has("line") || !pos_obj.has("character")) {
        ctx_.log_message("luma.showReferences: malformed position");
        return {};
    }

    const int line = util::clamp_to_int(pos_obj["line"].as_integer());
    const int character = util::clamp_to_int(pos_obj["character"].as_integer());

    auto state = ctx_.acquire_read_lock();

    const auto result = ctx_.find_analysis(uri);
    if (!result) {
        return {};
    }

    const auto idx_opt = find_token_at(*result, line, character);
    if (!idx_opt.has_value()) {
        return {};
    }

    const auto& tokens = result->semantic.tokens;
    const auto& target_token = tokens[*idx_opt];
    const std::string target_name = target_token.lexeme;

    // Collect all occurrences of the identifier across cached documents.
    JsonValue::ArrayType locations;
    for (const auto& [doc_uri, doc_result] : ctx_.analysis_cache.entries()) {
        auto ident_it = doc_result.metadata.identifier_index.find(target_name);
        if (ident_it == doc_result.metadata.identifier_index.end()) {
            continue;
        }
        for (const std::size_t tok_idx : ident_it->second) {
            const auto& tok = doc_result.semantic.tokens[tok_idx];
            locations.push_back(
                response::make_location_result(doc_uri, doc_result.to_wire(token_range(tok))));
        }
    }

    return JsonValue(std::move(locations));
}

// ═══════════════════════════════════════════════════════════
// Code lens
// ═══════════════════════════════════════════════════════════

namespace {

// Count references to a user function across every cached document, skipping
// the declaration itself.  For a namespaced name ("Ns.func"), a hit only counts
// when the immediately preceding tokens form "Ns ." before the short name.
[[nodiscard]] int count_references_to(LspHandlerContext& ctx, const std::string& short_name,
                                      const std::string& ns_prefix, const std::string& decl_uri,
                                      const SourceLocation& decl_location) {
    int ref_count = 0;
    auto state = ctx.acquire_read_lock();
    for (const auto& [doc_uri, doc_result] : state.cache().entries()) {
        auto idx_it = doc_result.metadata.identifier_index.find(short_name);
        if (idx_it == doc_result.metadata.identifier_index.end()) {
            continue;
        }
        for (const std::size_t tok_idx : idx_it->second) {
            const auto& tok = doc_result.semantic.tokens[tok_idx];
            // Skip the declaration itself.
            if (doc_uri == decl_uri && tok.location.line == decl_location.line &&
                tok.location.column == decl_location.column) {
                continue;
            }
            // For qualified names, verify the preceding tokens
            // form "Namespace." before the short name.
            if (!ns_prefix.empty()) {
                if (tok_idx < 2 || doc_result.semantic.tokens[tok_idx - 1].type != TokenType::Dot ||
                    doc_result.semantic.tokens[tok_idx - 2].lexeme != ns_prefix) {
                    continue;
                }
            }
            ++ref_count;
        }
    }
    return ref_count;
}

// Whether the function declared at decl_location carries an @test annotation,
// found by walking back over the leading keyword/annotation tokens (Function,
// Internal, Annotation) from the first same-line name token.
[[nodiscard]] bool has_test_annotation(const AnalysisResult& analysis,
                                       const std::string& short_name,
                                       const SourceLocation& decl_location) {
    const auto& tokens = analysis.semantic.tokens;
    auto idx_it = analysis.metadata.identifier_index.find(short_name);
    if (idx_it == analysis.metadata.identifier_index.end()) {
        return false;
    }
    for (const std::size_t ti : idx_it->second) {
        if (tokens[ti].location.line != decl_location.line) {
            continue;
        }
        // Walk back over keywords (Function, Internal, Annotation).
        std::size_t k = ti;
        while (k > 0) {
            --k;
            if (tokens[k].type == TokenType::Annotation && tokens[k].lexeme == "@test") {
                return true;
            }
            if (tokens[k].type != TokenType::Function && tokens[k].type != TokenType::Internal &&
                tokens[k].type != TokenType::Annotation) {
                break;
            }
        }
        // Only the first same-line name token is considered.
        return false;
    }
    return false;
}

} // namespace

JsonValue LspCodeActionHandler::handle_code_lens(const JsonValue& params) {
    if (!ctx_.configuration.config().get()->code_lens_enabled) {
        return JsonValue(JsonValue::ArrayType{});
    }

    const auto uri_opt = extraction::extract_text_document_uri(params);
    if (!uri_opt) {
        return JsonValue(JsonValue::ArrayType{});
    }
    const auto& uri = *uri_opt;

    const auto cached = ctx_.find_analysis(uri);
    if (!cached) {
        return JsonValue(JsonValue::ArrayType{});
    }

    const auto& user_fns = cached->semantic.symbols.user_functions;
    const auto& tokens = cached->semantic.tokens;

    JsonValue::ArrayType lenses;

    for (const auto& [fn_name, fn_info] : user_fns) {
        // Split on the last dot: "Ns.func" → namespace "Ns", member "func".
        const auto qualified = QualifiedName::parse(fn_name);
        const std::string& short_name = qualified.member_part;
        const std::string& ns_prefix = qualified.namespace_part;

        const int ref_count =
            count_references_to(ctx_, short_name, ns_prefix, uri, fn_info.location);
        // Convert the name range to the client's UTF-16 columns for the wire.
        const Range name_rng =
            cached->to_wire(find_declaration_name_range(tokens, fn_info.location, short_name));
        const bool is_test = has_test_annotation(*cached, short_name, fn_info.location);

        const std::string title = std::format("{} reference{}{}", ref_count,
                                              ref_count == 1 ? "" : "s", is_test ? " | @test" : "");

        lenses.emplace_back(JsonValue::ObjectType{
            {"range", serialise_range(name_rng)},
            {"command", JsonValue(JsonValue::ObjectType{
                            {"title", JsonValue(title)},
                            {"command", JsonValue("luma.showReferences")},
                            {"arguments", JsonValue(JsonValue::ArrayType{
                                              JsonValue(uri),
                                              serialise_position(name_rng.start),
                                          })},
                        })},
        });
    }

    return JsonValue(std::move(lenses));
}

} // namespace luma::lsp
