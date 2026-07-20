#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "json/json.hpp"
#include "lsp_brace_matcher.hpp"
#include "lsp_code_action_builder.hpp"
#include "lsp_code_action_handler.hpp"
#include "lsp_config.hpp"
#include "lsp_diagnostic_codes.hpp"
#include "lsp_document_store.hpp"
#include "lsp_params.hpp"
#include "lsp_refactoring_provider.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_string_utils.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_type_formatter.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

using util::clamp_to_int;

namespace {

// Selection range from a codeAction request, in 0-based coordinates.
struct SelectionRange {
    int start_line;
    int start_col;
    int end_line;
    int end_col;

    // True when the range covers more than a single caret position.
    [[nodiscard]] bool has_selection() const {
        return start_line != end_line || start_col != end_col;
    }
};

// Parses the "range" field of a codeAction request into 0-based coordinates.
// Returns nullopt when the request carries no range.
[[nodiscard]] std::optional<SelectionRange> extract_selection_range(const JsonValue& params) {
    if (!params.has("range")) {
        return std::nullopt;
    }
    const auto& range = params["range"];
    return SelectionRange{
        .start_line = clamp_to_int(range["start"]["line"].as_integer()),
        .start_col = clamp_to_int(range["start"]["character"].as_integer()),
        .end_line = clamp_to_int(range["end"]["line"].as_integer()),
        .end_col = clamp_to_int(range["end"]["character"].as_integer()),
    };
}

// A document's cached source text together with its line-start offsets. The
// pointers are owned by the DocumentStore and remain valid for as long as the
// caller holds the lock token used to fetch them.
struct DocumentText {
    const std::string* source;
    const std::vector<std::size_t>* line_starts;
};

// Fetches the cached source and line-start offsets for `uri`, returning nullopt
// when either is unavailable.
[[nodiscard]] std::optional<DocumentText> fetch_document_text(const DocumentStore& documents,
                                                              const LockToken& lock_token,
                                                              const std::string& uri) {
    const auto* source = documents.get_content(lock_token, uri);
    if (source == nullptr) {
        return std::nullopt;
    }
    const auto* line_starts = documents.get_line_starts(lock_token, uri);
    if (line_starts == nullptr) {
        return std::nullopt;
    }
    return DocumentText{.source = source, .line_starts = line_starts};
}

// Attempts to infer a type annotation for a variable declaration at the given token index.
// Returns a code action if inference succeeds, or nullopt if not applicable.
[[nodiscard]] std::optional<JsonValue> try_infer_type_annotation(const std::vector<Token>& tokens,
                                                                 std::size_t decl_idx,
                                                                 const AnalysisResult& analysis,
                                                                 const std::string& uri) {
    if (tokens[decl_idx].type != TokenType::Identifier) {
        return std::nullopt;
    }

    // Pattern: Identifier Equals (no colon before equals → no type annotation).
    if (decl_idx + 1 >= tokens.size() || tokens[decl_idx + 1].type != TokenType::Equals) {
        return std::nullopt;
    }

    // Ensure this is a declaration, not a reassignment.
    // A declaration is preceded by 'mutable' or is the first
    // occurrence in its scope.
    const bool preceded_by_mutable =
        decl_idx > 0 && tokens[decl_idx - 1].type == TokenType::Mutable;

    bool is_first_occurrence = false;
    auto idx_it = analysis.metadata.identifier_index.find(tokens[decl_idx].lexeme);
    if (idx_it != analysis.metadata.identifier_index.end() && !idx_it->second.empty()) {
        const auto enclosing = find_enclosing_function(analysis, tokens[decl_idx].location.line);
        if (enclosing.has_value()) {
            auto br_it = analysis.semantic.functions.function_body_ranges.find(*enclosing);
            if (br_it != analysis.semantic.functions.function_body_ranges.end()) {
                for (const std::size_t ti : idx_it->second) {
                    const int tl = tokens[ti].location.line;
                    if (tl >= br_it->second.first && tl <= br_it->second.second) {
                        is_first_occurrence = (ti == decl_idx);
                        break;
                    }
                }
            }
        } else if (idx_it->second.front() == decl_idx) {
            is_first_occurrence = true;
        }
    }

    if (!preceded_by_mutable && !is_first_occurrence) {
        return std::nullopt;
    }

    // Look up the inferred type.
    const auto resolved =
        resolve_variable_type(analysis, tokens[decl_idx].lexeme, tokens[decl_idx].location.line);
    if (!resolved.has_value() || !util::is_known_type(resolved->type_name)) {
        return std::nullopt;
    }

    const auto& var_name = tokens[decl_idx].lexeme;
    const auto& type_name = resolved->type_name;

    // Insert "type " before the variable name (or before 'mutable' if present).
    const int insert_line = token_line_0based(tokens[decl_idx]);
    const int insert_col = token_col_end_0based(tokens[decl_idx]) - lexeme_column_width(var_name);

    return CodeActionBuilder()
        .set_title(std::format("Add type annotation '{}'", type_name))
        .set_kind(code_action_kind::k_refactor_rewrite)
        .add_edit(
            uri,
            analysis.to_wire(Range{.start = Position{.line = insert_line, .character = insert_col},
                                   .end = Position{.line = insert_line, .character = insert_col}}),
            type_name + " ")
        .build();
}

// ── Refactoring: Add type annotation ─────────────────────
// When the cursor is on a variable declaration without an explicit type
// (e.g., `x = 42`), offer to add the inferred type annotation.
class TypeAnnotationRefactoring final : public RefactoringProvider {
public:
    [[nodiscard]] std::vector<JsonValue> generate(const RefactoringContext& ctx) const override {
        JsonValue::ArrayType actions;
        const auto sel = extract_selection_range(ctx.params);
        if (!sel) {
            return actions;
        }

        const auto& tokens = ctx.analysis.semantic.tokens;
        for (std::size_t i{0}; i < tokens.size(); ++i) {
            if (token_line_0based(tokens[i]) != sel->start_line) {
                continue;
            }

            auto action = try_infer_type_annotation(tokens, i, ctx.analysis, ctx.uri);
            if (action.has_value()) {
                actions.push_back(std::move(*action));
                break;
            }
        }
        return actions;
    }
};

// Extracts the selected text from source and validates it's a non-trivial expression.
[[nodiscard]] std::optional<std::string> extract_valid_selection(const std::string& source,
                                                                 std::size_t start_offset,
                                                                 std::size_t end_offset) {
    if (end_offset > source.size() || start_offset >= end_offset) {
        return std::nullopt;
    }

    const auto text = source.substr(start_offset, end_offset - start_offset);

    // Don't offer for trivially short or identifier-only selections.
    if (text.size() <= 1 || text.find_first_of(".(|+-><!") == std::string::npos) {
        return std::nullopt;
    }

    return text;
}

// Detects the indentation level at a given line offset in source.
[[nodiscard]] std::string detect_line_indentation(const std::string& source,
                                                  std::size_t line_start_offset) {
    std::string indent;
    for (auto p = line_start_offset; p < source.size() && (source[p] == ' ' || source[p] == '\t');
         ++p) {
        indent += source[p];
    }
    return indent;
}

// ── Refactoring: Extract Variable ────────────────────────────────────────────────────────────
// When a non-trivial expression is selected, offer to extract it into a variable.
class ExtractVariableRefactoring final : public RefactoringProvider {
public:
    [[nodiscard]] std::vector<JsonValue> generate(const RefactoringContext& ctx) const override {
        JsonValue::ArrayType actions;
        const auto sel = extract_selection_range(ctx.params);
        if (!sel || !sel->has_selection() || sel->start_line != sel->end_line) {
            return actions;
        }

        const auto doc = fetch_document_text(ctx.documents, ctx.lock_token, ctx.uri);
        if (!doc || static_cast<std::size_t>(sel->start_line) >= doc->line_starts->size()) {
            return actions;
        }

        const auto& source = *doc->source;
        const auto line_start = (*doc->line_starts)[sel->start_line];

        // Selection columns are client UTF-16 offsets; resolve them to byte
        // offsets through the same conversion incremental didChange uses, so
        // multi-byte UTF-8 before/within the selection is handled correctly.
        const auto start_offset = ctx.documents.position_to_offset(ctx.lock_token, ctx.uri, source,
                                                                   sel->start_line, sel->start_col);
        const auto end_offset = ctx.documents.position_to_offset(ctx.lock_token, ctx.uri, source,
                                                                 sel->end_line, sel->end_col);

        const auto selected_text = extract_valid_selection(source, start_offset, end_offset);
        if (!selected_text.has_value()) {
            return actions;
        }

        const std::string var_name = "extracted";
        const std::string indent = detect_line_indentation(source, line_start);

        // Insert declaration before the current line, replace selection with variable name.
        actions.push_back(
            CodeActionBuilder()
                .set_title("Extract variable")
                .set_kind(code_action_kind::k_refactor_extract_variable)
                .add_edit(ctx.uri,
                          Range{.start = Position{.line = sel->start_line, .character = 0},
                                .end = Position{.line = sel->start_line, .character = 0}},
                          std::format("{}{} = {}\n", indent, var_name, *selected_text))
                .add_edit(
                    ctx.uri,
                    Range{.start = Position{.line = sel->start_line, .character = sel->start_col},
                          .end = Position{.line = sel->end_line, .character = sel->end_col}},
                    var_name)
                .build());
        return actions;
    }
};

// ── Refactoring: Extract Function ───────────────────────────
// When multiple lines are selected, offer to extract them into a function.
class ExtractFunctionRefactoring final : public RefactoringProvider {
public:
    [[nodiscard]] std::vector<JsonValue> generate(const RefactoringContext& ctx) const override {
        JsonValue::ArrayType actions;
        const auto sel = extract_selection_range(ctx.params);
        if (!sel || !sel->has_selection() || sel->end_line <= sel->start_line) {
            return actions;
        }

        const auto doc = fetch_document_text(ctx.documents, ctx.lock_token, ctx.uri);
        if (!doc) {
            return actions;
        }

        const auto& source = *doc->source;
        const auto& line_starts = *doc->line_starts;

        // Compute selected text.
        std::size_t start_off = 0;
        std::size_t end_off = source.size();

        if (static_cast<std::size_t>(sel->start_line) < line_starts.size()) {
            start_off = line_starts[sel->start_line];
        }

        if (static_cast<std::size_t>(sel->end_line) + 1 < line_starts.size()) {
            end_off = line_starts[static_cast<std::size_t>(sel->end_line) + 1];
        }

        if (end_off <= start_off) {
            return actions;
        }

        const auto selected = source.substr(start_off, end_off - start_off);
        const std::string fn_name = "extracted_function";

        // Insert function definition before the enclosing function.
        const std::string fn_def = std::format("function {}() {{\n{}}}\n\n", fn_name, selected);

        CodeActionBuilder extract_fn_builder;
        extract_fn_builder.set_title("Extract function")
            .set_kind(code_action_kind::k_refactor_extract_function);

        extract_fn_builder.add_edit(
            ctx.uri,
            Range{.start = Position{.line = sel->start_line, .character = 0},
                  .end = Position{.line = sel->start_line, .character = 0}},
            fn_def);

        // Replace selected lines with a call.
        extract_fn_builder.add_edit(
            ctx.uri,
            Range{.start = Position{.line = sel->start_line, .character = 0},
                  .end = Position{.line = sel->end_line + 1, .character = 0}},
            std::format("{}()\n", fn_name));

        actions.push_back(extract_fn_builder.build());
        return actions;
    }
};

// ── Refactoring: Convert if/else chain to match ─────────────
// When the cursor is on an if/else if/else chain that tests
// the same variable with '==' comparisons, offer to convert
// it to a match expression.

struct IfChainArm {
    std::string pattern; // The value being matched (RHS of ==).
    int body_start_line; // 0-based, inclusive.
    int body_end_line;   // 0-based, exclusive.
    bool is_else{false}; // True for the final 'else' arm.
};

struct IfChainParseResult {
    std::vector<IfChainArm> arms;
    std::string match_subject;
    int chain_end_line{0};
    bool valid{false};
};

// Parses a single if/else-if arm starting at token index `start`.
// Returns the arm (condition pattern + body range) and the index past the closing brace.
struct IfArm {
    std::string condition;
    Range body_range;
};

[[nodiscard]] std::optional<std::pair<IfArm, std::size_t>>
parse_single_if_arm(const std::vector<Token>& tokens, std::size_t start_idx) {
    if (start_idx >= tokens.size() || tokens[start_idx].type != TokenType::If) {
        return std::nullopt;
    }

    if (start_idx + 3 >= tokens.size()) {
        return std::nullopt;
    }

    if (tokens[start_idx + 1].type != TokenType::Identifier) {
        return std::nullopt;
    }

    if (tokens[start_idx + 2].type != TokenType::EqualsEquals) {
        return std::nullopt;
    }

    const auto& pat_tok = tokens[start_idx + 3];
    std::string pattern = pat_tok.lexeme;
    if (pat_tok.type == TokenType::StringLiteral) {
        pattern = "\"" + pattern + "\"";
    }

    // Find the '{' that starts the body.
    std::size_t brace_idx = start_idx + 4;
    while (brace_idx < tokens.size() && tokens[brace_idx].type != TokenType::LeftBrace) {
        ++brace_idx;
    }
    if (brace_idx >= tokens.size()) {
        return std::nullopt;
    }

    const int body_start = tokens[brace_idx].location.line;

    // Find matching '}'.
    auto close_opt = find_matching_close_brace(tokens, brace_idx);
    if (!close_opt.has_value()) {
        return std::nullopt;
    }
    const std::size_t end_idx = *close_opt + 1;
    const int body_end = token_line_0based(tokens[end_idx - 1]);

    IfArm arm{.condition = pattern,
              .body_range = Range{.start = Position{.line = body_start, .character = 0},
                                  .end = Position{.line = body_end, .character = 0}}};
    return std::pair<IfArm, std::size_t>{std::move(arm), end_idx};
}

// Parse the if/else-if/else chain starting at token index `start_ti`,
// collecting arms and the matched subject variable.
IfChainParseResult parse_if_chain_arms(const std::vector<Token>& tokens, std::size_t start_ti,
                                       int if_line) {
    IfChainParseResult result;
    result.chain_end_line = if_line;
    result.valid = true;

    std::size_t ci = start_ti;
    bool first_arm = true;
    while (ci < tokens.size() && result.valid) {
        // Use the helper to parse a single if-arm.
        auto arm_opt = parse_single_if_arm(tokens, ci);
        if (!arm_opt.has_value()) {
            result.valid = false;
            break;
        }

        const auto& [arm, next_idx] = *arm_opt;

        // Validate subject consistency.
        const auto& subj = tokens[ci + 1].lexeme;
        if (first_arm) {
            result.match_subject = subj;
            first_arm = false;
        } else if (subj != result.match_subject) {
            result.valid = false;
            break;
        }

        const int body_start = arm.body_range.start.line;
        const int body_end = arm.body_range.end.line;
        result.chain_end_line = body_end;
        result.arms.push_back(IfChainArm{.pattern = arm.condition,
                                         .body_start_line = body_start,
                                         .body_end_line = body_end,
                                         .is_else = false});

        // Check for 'else' after the '}'.
        ci = next_idx;
        if (ci < tokens.size() && tokens[ci].type == TokenType::Else) {
            ++ci;
            if (ci < tokens.size() && tokens[ci].type == TokenType::If) {
                continue;
            }
            // Plain 'else { ... }' — final arm.
            if (ci < tokens.size() && tokens[ci].type == TokenType::LeftBrace) {
                const int else_body_start = tokens[ci].location.line;
                auto else_close_opt = find_matching_close_brace(tokens, ci);
                if (!else_close_opt.has_value()) {
                    result.valid = false;
                    break;
                }
                const std::size_t else_end = *else_close_opt + 1;
                const int else_body_end = token_line_0based(tokens[else_end - 1]);
                result.chain_end_line = else_body_end;

                result.arms.push_back(IfChainArm{.pattern = "_",
                                                 .body_start_line = else_body_start,
                                                 .body_end_line = else_body_end,
                                                 .is_else = true});
            }
            break;
        }
        break;
    }

    return result;
}

// Check structural constraints: chain must be valid and have at least 2 arms.
bool validate_if_chain(const IfChainParseResult& parse_result) {
    return parse_result.valid && parse_result.arms.size() >= 2;
}

// Build the replacement match expression text from parsed arms.
std::string generate_match_expression(const std::vector<IfChainArm>& arms,
                                      const std::string& match_subject, int if_line,
                                      const std::string& source,
                                      const std::vector<std::size_t>& line_starts) {
    // Detect indentation of the original 'if' line.
    std::string indent;
    if (static_cast<std::size_t>(if_line) < line_starts.size()) {
        const auto line_off = line_starts[if_line];
        for (auto p = line_off; p < source.size() && (source[p] == ' ' || source[p] == '\t'); ++p) {
            indent += source[p];
        }
    }

    std::string match_text = std::format("{}match {} {{\n", indent, match_subject);
    for (const auto& arm : arms) {
        std::string body;
        for (int ln = arm.body_start_line; ln < arm.body_end_line; ++ln) {
            if (static_cast<std::size_t>(ln) < line_starts.size()) {
                auto start = line_starts[ln];
                auto end = (static_cast<std::size_t>(ln) + 1 < line_starts.size())
                               ? line_starts[static_cast<std::size_t>(ln) + 1]
                               : source.size();
                auto line_content = source.substr(start, end - start);
                body += "    " + line_content;
            }
        }
        if (body.empty()) {
            body = indent + "        # empty\n";
        }
        match_text +=
            std::format("{}    case {} {{\n{}{}    }}\n", indent, arm.pattern, body, indent);
    }
    match_text += indent + "}";
    return match_text;
}

// ── Refactoring: Convert if/else chain to match ─────────────
// When the cursor is on an if/else-if/else chain that tests the same variable
// with '==' comparisons, offer to convert it to a match expression.
class IfToMatchRefactoring final : public RefactoringProvider {
public:
    [[nodiscard]] std::vector<JsonValue> generate(const RefactoringContext& ctx) const override {
        JsonValue::ArrayType actions;
        const auto sel = extract_selection_range(ctx.params);
        if (!sel) {
            return actions;
        }

        const auto& tokens = ctx.analysis.semantic.tokens;

        // Find an 'if' token on the cursor line.
        for (std::size_t ti = 0; ti < tokens.size(); ++ti) {
            if (tokens[ti].type != TokenType::If) {
                continue;
            }

            const int if_line = token_line_0based(tokens[ti]);
            if (if_line < sel->start_line || if_line > sel->end_line) {
                continue;
            }

            // Parse and validate the if/else-if/else chain.
            auto parse_result = parse_if_chain_arms(tokens, ti, if_line);
            if (!validate_if_chain(parse_result)) {
                continue;
            }

            // Build the match expression text.
            const auto doc = fetch_document_text(ctx.documents, ctx.lock_token, ctx.uri);
            if (!doc) {
                continue;
            }

            const auto match_text =
                generate_match_expression(parse_result.arms, parse_result.match_subject, if_line,
                                          *doc->source, *doc->line_starts);

            actions.push_back(
                CodeActionBuilder()
                    .set_title("Convert to match expression")
                    .set_kind(code_action_kind::k_refactor_rewrite)
                    .add_edit(ctx.uri,
                              Range{.start = Position{.line = if_line, .character = 0},
                                    .end = Position{.line = parse_result.chain_end_line + 1,
                                                    .character = 0}},
                              match_text + "\n")
                    .build());
            break; // Only offer for the first if-chain found.
        }
        return actions;
    }
};

// Builds the singleton registry of refactoring providers. Providers run in
// registration order, which determines the order of the offered actions.
[[nodiscard]] RefactoringRegistry& get_refactoring_registry() {
    static RefactoringRegistry registry = [] {
        RefactoringRegistry r;
        r.register_provider(std::make_unique<TypeAnnotationRefactoring>());
        r.register_provider(std::make_unique<ExtractVariableRefactoring>());
        r.register_provider(std::make_unique<ExtractFunctionRefactoring>());
        r.register_provider(std::make_unique<IfToMatchRefactoring>());
        return r;
    }();
    return registry;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// Refactoring actions (cursor-position-driven)
// ═══════════════════════════════════════════════════════════

void LspCodeActionHandler::collect_refactorings(const std::string& uri,
                                                const AnalysisResult& cached,
                                                const LockToken& lock_token,
                                                const JsonValue& params,
                                                JsonValue::ArrayType& actions) const {
    const RefactoringContext ctx{.uri = uri,
                                 .analysis = cached,
                                 .documents = ctx_.doc_store,
                                 .lock_token = lock_token,
                                 .params = params};
    for (auto& action : get_refactoring_registry().collect(ctx)) {
        actions.push_back(std::move(action));
    }
}

} // namespace luma::lsp
