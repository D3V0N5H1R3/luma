#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/lexer/token.hpp"
#include "analysis/lexer/token_type.hpp"
#include "json/json.hpp"
#include "lsp_analysis_result.hpp"
#include "lsp_constants.hpp"
#include "lsp_inlay_hint_handler.hpp"
#include "lsp_param_extraction.hpp"
#include "lsp_param_utils.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_stdlib_registry.hpp"
#include "lsp_symbol_resolver.hpp"
#include "lsp_token_utils.hpp"
#include "lsp_type_formatter.hpp"
#include "lsp_types.hpp"
#include "symbols/qualified_name.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// Inlay hints
// ═══════════════════════════════════════════════════════════

namespace {

// ─── Inlay-hint emitters ───
// Each emitter inspects the token at index `i` (or, for return types, all
// user functions) and appends any applicable hints to `hints`. They share the
// "resolve a non-empty, non-unknown type" pattern, factored into
// push_type_hint, so each emitter reads as detection logic plus one call.

// Append a type inlay hint at (line0, col0) labelled `prefix + type`, unless
// `type` is empty or the "unknown" sentinel. Centralises the shared
// resolve→reject→push tail of the type-hint emitters.
void push_type_hint(JsonValue::ArrayType& hints, int line0, int col0, std::string_view prefix,
                    std::string_view type) {
    if (!util::is_known_type(type)) {
        return;
    }
    std::string label;
    label.reserve(prefix.size() + type.size());
    label.append(prefix);
    label.append(type);
    hints.push_back(lsp_builders::inlay_hint(line0, col0, label, constants::inlay_hint_kind::type));
}

// Type hint for a variable declaration: `Identifier =` with no explicit type.
void emit_variable_decl_hint(const AnalysisResult& cached, const std::vector<Token>& tokens,
                             std::size_t i, JsonValue::ArrayType& hints) {
    if (tokens[i].type != TokenType::Identifier) {
        return;
    }
    const auto& name = tokens[i].lexeme;
    const auto resolved_hint = resolve_variable_type(cached, name, tokens[i].location.line);
    const std::string var_type =
        resolved_hint.has_value() ? resolved_hint->type_name : std::string{};
    if (!util::is_known_type(var_type)) {
        return;
    }
    if (i + 1 >= tokens.size() || tokens[i + 1].type != TokenType::Equals) {
        return;
    }

    // Only show type hint at the declaration site — not on re-assignments. A
    // declaration is identified by being preceded by 'mutable' or being the
    // first occurrence of this name in the scope.
    const auto enclosing_hint = find_enclosing_function(cached, tokens[i].location.line);
    bool is_declaration = false;
    if (i > 0 && tokens[i - 1].type == TokenType::Mutable) {
        is_declaration = true;
    } else {
        auto idx_it = cached.metadata.identifier_index.find(name);
        if (enclosing_hint.has_value() && idx_it != cached.metadata.identifier_index.end()) {
            auto br_it = cached.semantic.functions.function_body_ranges.find(*enclosing_hint);
            if (br_it != cached.semantic.functions.function_body_ranges.end()) {
                for (const std::size_t ti : idx_it->second) {
                    const int tl = tokens[ti].location.line;
                    if (tl >= br_it->second.first && tl <= br_it->second.second) {
                        is_declaration = (ti == i);
                        break;
                    }
                }
            }
        } else if (idx_it != cached.metadata.identifier_index.end() && !idx_it->second.empty() &&
                   idx_it->second.front() == i) {
            is_declaration = true;
        }
    }

    if (is_declaration) {
        const int hint_line = tokens[i].location.line - 1;
        const int hint_col = tokens[i].location.column - 1;
        push_type_hint(hints, hint_line, hint_col, ": ", var_type);
    }
}

// Type hint for a for-loop variable: `For Ident In` or `For Mutable Ident In`.
void emit_for_loop_var_hint(const AnalysisResult& cached, const std::vector<Token>& tokens,
                            std::size_t i, JsonValue::ArrayType& hints) {
    if (tokens[i].type != TokenType::For) {
        return;
    }
    std::size_t var_idx{0};
    bool has_var{false};
    if (i + 2 < tokens.size() && tokens[i + 1].type == TokenType::Identifier &&
        tokens[i + 2].type == TokenType::In) {
        var_idx = i + 1;
        has_var = true;
    } else if (i + 3 < tokens.size() && tokens[i + 1].type == TokenType::Mutable &&
               tokens[i + 2].type == TokenType::Identifier && tokens[i + 3].type == TokenType::In) {
        var_idx = i + 2;
        has_var = true;
    }
    if (!has_var) {
        return;
    }
    const auto& var_name = tokens[var_idx].lexeme;
    const auto resolved = resolve_variable_type(cached, var_name, tokens[var_idx].location.line);
    const std::string var_type = resolved.has_value() ? resolved->type_name : std::string{};
    const int hint_line = tokens[var_idx].location.line - 1;
    const int hint_col = tokens[var_idx].location.column - 1;
    push_type_hint(hints, hint_line, hint_col, ": ", var_type);
}

// Type hint for a catch variable: `Catch Identifier LeftBrace`.
void emit_catch_var_hint(const AnalysisResult& cached, const std::vector<Token>& tokens,
                         std::size_t i, JsonValue::ArrayType& hints) {
    if (tokens[i].type != TokenType::Catch || i + 2 >= tokens.size() ||
        tokens[i + 1].type != TokenType::Identifier || tokens[i + 2].type != TokenType::LeftBrace) {
        return;
    }
    const auto& var_name = tokens[i + 1].lexeme;
    const auto resolved = resolve_variable_type(cached, var_name, tokens[i + 1].location.line);
    const std::string var_type = resolved.has_value() ? resolved->type_name : std::string{};
    const int hint_line = tokens[i + 1].location.line - 1;
    const int hint_col = tokens[i + 1].location.column - 1;
    push_type_hint(hints, hint_line, hint_col, ": ", var_type);
}

// Resolves the parameter names for a call to `fn_name` at token `i`, using the
// user-function table first and falling back to the stdlib signature.
[[nodiscard]] std::vector<std::string> resolve_call_param_names(const AnalysisResult& cached,
                                                                const StdlibRegistry& stdlib,
                                                                const std::vector<Token>& tokens,
                                                                std::size_t i,
                                                                const std::string& fn_name) {
    const auto& user_fns = cached.semantic.symbols.user_functions;
    auto fn_it = user_fns.find(fn_name);

    // For namespaced functions, the token lexeme is the bare name (e.g. "func")
    // but user_fns keys are qualified ("Ns.func"); resolve via short names.
    if (fn_it == user_fns.end()) {
        auto sn_it = cached.semantic.symbols.function_short_names.find(fn_name);
        if (sn_it != cached.semantic.symbols.function_short_names.end()) {
            for (const auto& qname : sn_it->second) {
                auto candidate = user_fns.find(qname);
                if (candidate != user_fns.end()) {
                    fn_it = candidate;
                    break;
                }
            }
        }
    }

    std::vector<std::string> param_names;
    if (fn_it != user_fns.end() && !fn_it->second.parameters.empty()) {
        const auto& param_list = fn_it->second.parameters;
        param_names.reserve(param_list.size());
        for (const auto& p : param_list) {
            param_names.push_back(p.name);
        }
    } else if (i >= 2 && tokens[i - 1].type == TokenType::Dot &&
               tokens[i - 2].type == TokenType::Identifier) {
        const auto qualified = tokens[i - 2].lexeme + "." + fn_name;
        auto stdlib_ptr = stdlib.find_function(qualified);
        if (stdlib_ptr && !stdlib_ptr->params_signature.empty()) {
            const auto parts = util::split_param_list(stdlib_ptr->params_signature);
            param_names.reserve(parts.size());
            for (const auto& part : parts) {
                if (auto name = util::extract_param_name(part)) {
                    param_names.push_back(std::move(*name));
                }
            }
        }
    }
    return param_names;
}

// Parameter-name hints at a call site: `Identifier LeftParen args...`.
void emit_call_param_name_hints(const AnalysisResult& cached, const StdlibRegistry& stdlib,
                                const std::vector<Token>& tokens, std::size_t i,
                                JsonValue::ArrayType& hints) {
    if (tokens[i].type != TokenType::Identifier || i + 1 >= tokens.size() ||
        tokens[i + 1].type != TokenType::LeftParen) {
        return;
    }
    const auto param_names = resolve_call_param_names(cached, stdlib, tokens, i, tokens[i].lexeme);
    if (param_names.empty()) {
        return;
    }

    // Find the argument positions after the '('.
    std::size_t arg_idx{0};
    int depth{0};
    for (std::size_t j = i + 2; j < tokens.size() && arg_idx < param_names.size(); ++j) {
        // Emit hint for the first token of an argument at depth 0 BEFORE
        // updating depth — so bracket-starting args get hints.
        if (depth == 0 && arg_idx < param_names.size() && tokens[j].type != TokenType::Comma) {
            if (tokens[j].type != TokenType::Identifier ||
                tokens[j].lexeme != param_names[arg_idx]) {
                const int hl = tokens[j].location.line - 1;
                const int hc =
                    tokens[j].location.column - 1 - lexeme_column_width(tokens[j].lexeme);
                hints.push_back(lsp_builders::inlay_hint(hl, hc, param_names[arg_idx] + ":",
                                                         constants::inlay_hint_kind::parameter,
                                                         false, true));
            }

            // Skip to next comma or end.
            while (j + 1 < tokens.size() && tokens[j + 1].type != TokenType::Comma &&
                   tokens[j + 1].type != TokenType::RightParen) {
                ++j;
                if (tokens[j].type == TokenType::LeftParen ||
                    tokens[j].type == TokenType::LeftBracket ||
                    tokens[j].type == TokenType::LeftBrace) {
                    ++depth;
                } else if ((tokens[j].type == TokenType::RightParen ||
                            tokens[j].type == TokenType::RightBracket ||
                            tokens[j].type == TokenType::RightBrace) &&
                           depth > 0) {
                    --depth;
                }
            }
            continue;
        }

        if (tokens[j].type == TokenType::LeftParen || tokens[j].type == TokenType::LeftBracket ||
            tokens[j].type == TokenType::LeftBrace) {
            ++depth;
        } else if (tokens[j].type == TokenType::RightParen ||
                   tokens[j].type == TokenType::RightBracket ||
                   tokens[j].type == TokenType::RightBrace) {
            if (depth == 0) {
                break;
            }
            --depth;
        } else if (tokens[j].type == TokenType::Comma && depth == 0) {
            ++arg_idx;
        }
    }
}

// Pipe-operator parameter-name hint: `expr |> Ident(...)` or `|> Module.Ident(...)`.
void emit_pipe_param_name_hint(const AnalysisResult& cached, const StdlibRegistry& stdlib,
                               const std::vector<Token>& tokens, std::size_t i,
                               JsonValue::ArrayType& hints) {
    if (tokens[i].type != TokenType::PipeGreater) {
        return;
    }
    // Find the function name token after |>.
    std::size_t fn_idx{0};
    bool found_fn{false};
    if (i + 1 < tokens.size() && tokens[i + 1].type == TokenType::Identifier) {
        if (i + 2 < tokens.size() && tokens[i + 2].type == TokenType::LeftParen) {
            fn_idx = i + 1;
            found_fn = true;
        } else if (i + 3 < tokens.size() && tokens[i + 2].type == TokenType::Dot &&
                   tokens[i + 3].type == TokenType::Identifier && i + 4 < tokens.size() &&
                   tokens[i + 4].type == TokenType::LeftParen) {
            fn_idx = i + 3;
            found_fn = true;
        }
    }
    if (!found_fn) {
        return;
    }

    const auto& fn_name = tokens[fn_idx].lexeme;
    const auto& user_fns = cached.semantic.symbols.user_functions;
    std::string first_param;

    auto fn_it = user_fns.find(fn_name);
    if (fn_it == user_fns.end()) {
        auto sn_it = cached.semantic.symbols.function_short_names.find(fn_name);
        if (sn_it != cached.semantic.symbols.function_short_names.end()) {
            for (const auto& qname : sn_it->second) {
                auto candidate = user_fns.find(qname);
                if (candidate != user_fns.end()) {
                    fn_it = candidate;
                    break;
                }
            }
        }
    }

    if (fn_it != user_fns.end() && !fn_it->second.parameters.empty()) {
        first_param = fn_it->second.parameters.front().name;
    } else if (fn_idx >= 2 && tokens[fn_idx - 1].type == TokenType::Dot &&
               tokens[fn_idx - 2].type == TokenType::Identifier) {
        const auto qualified = tokens[fn_idx - 2].lexeme + "." + fn_name;
        auto stdlib_ptr = stdlib.find_function(qualified);
        if (stdlib_ptr && !stdlib_ptr->params_signature.empty()) {
            const auto parts = util::split_param_list(stdlib_ptr->params_signature);
            if (!parts.empty()) {
                if (auto name = util::extract_param_name(parts[0])) {
                    first_param = std::move(*name);
                }
            }
        }
    }

    if (!first_param.empty()) {
        // Place the hint before the |> operator.
        const int hl = tokens[i].location.line - 1;
        const int hc = tokens[i].location.column - 1 - lexeme_column_width(tokens[i].lexeme);
        hints.push_back(lsp_builders::inlay_hint(
            hl, hc, first_param + ":", constants::inlay_hint_kind::parameter, false, true));
    }
}

// Return-type hints: show `-> type` after a function's ')' when no explicit
// return type is written.
void emit_return_type_hints(const AnalysisResult& cached, const std::vector<Token>& tokens,
                            JsonValue::ArrayType& hints) {
    for (const auto& [fn_name, fn_info] : cached.semantic.symbols.user_functions) {
        if (fn_info.return_type == "void" || !util::is_known_type(fn_info.return_type)) {
            continue;
        }

        // For namespaced functions fn_name is qualified ("Ns.func") but the
        // token lexeme is just "func".
        const std::string fn_short{qualified_member(fn_name)};

        // Locate the declaration via the identifier index instead of scanning
        // the whole token stream.  Occurrences are stored in ascending order,
        // so the first one preceded by `function` is the declaration.
        const auto idx_it = cached.metadata.identifier_index.find(fn_short);
        if (idx_it == cached.metadata.identifier_index.end()) {
            continue;
        }
        for (const std::size_t j : idx_it->second) {
            if (j == 0 || tokens[j - 1].type != TokenType::Function) {
                continue;
            }
            // Find the closing ')' of the parameter list.
            int depth{0};
            bool found_paren{false};
            std::size_t close_j{0};
            for (std::size_t k = j + 1; k < tokens.size(); ++k) {
                if (tokens[k].type == TokenType::LeftParen) {
                    ++depth;
                    found_paren = true;
                } else if (tokens[k].type == TokenType::RightParen) {
                    --depth;
                    if (depth == 0) {
                        close_j = k;
                        break;
                    }
                }
            }

            if (!found_paren || close_j == 0) {
                break;
            }
            if (close_j + 1 < tokens.size() && tokens[close_j + 1].type == TokenType::Arrow) {
                break; // Already has explicit return type.
            }

            const int hl = tokens[close_j].location.line - 1;
            const int hc = tokens[close_j].location.column - 1;
            push_type_hint(hints, hl, hc, " -> ", fn_info.return_type);
            break;
        }
    }
}

// True when (line0, char0) — a 0-based LSP position — falls within the
// inclusive [start, end] span of `range`. Used to drop inlay hints that lie
// outside the client's requested (visible) range.
bool position_within_range(int line0, int char0, const Range& range) {
    const bool after_start =
        line0 > range.start.line || (line0 == range.start.line && char0 >= range.start.character);
    const bool before_end =
        line0 < range.end.line || (line0 == range.end.line && char0 <= range.end.character);
    return after_start && before_end;
}

} // anonymous namespace

JsonValue LspInlayHintHandler::handle_inlay_hint(const JsonValue& params) {
    if (!ctx_.configuration.config().get()->inlay_hints_enabled) {
        return JsonValue(JsonValue::ArrayType{});
    }

    const auto uri_opt = extraction::extract_text_document_uri(params);
    if (!uri_opt) {
        return JsonValue(JsonValue::ArrayType{});
    }
    const auto& uri = *uri_opt;

    // The client sends the currently-visible document range; only hints inside
    // it need to be returned. An absent or malformed range means "whole
    // document", preserving the previous behaviour for such clients.
    std::optional<Range> range_opt;
    if (params.is_object() && params.has("range")) {
        range_opt = extraction::extract_range(params["range"]);
    }

    auto state = ctx_.acquire_read_lock();
    const auto cached = ctx_.find_analysis(uri);

    if (!cached) {
        return JsonValue(JsonValue::ArrayType{});
    }

    const auto& tokens = cached->semantic.tokens;

    JsonValue::ArrayType hints;

    for (std::size_t i{0}; i < tokens.size(); ++i) {
        emit_variable_decl_hint(*cached, tokens, i, hints);

        emit_for_loop_var_hint(*cached, tokens, i, hints);

        emit_catch_var_hint(*cached, tokens, i, hints);

        emit_call_param_name_hints(*cached, ctx_.stdlib_registry, tokens, i, hints);

        emit_pipe_param_name_hint(*cached, ctx_.stdlib_registry, tokens, i, hints);
    }

    emit_return_type_hints(*cached, tokens, hints);

    // Hint positions are emitted in the lexer's codepoint columns. Convert them
    // to the client's UTF-16 columns before returning (and before the range
    // filter below, so the incoming UTF-16 range compares against like units).
    {
        const auto enc = cached->encoder();
        for (auto& hint : hints) {
            if (!hint.is_object()) {
                continue;
            }
            auto& hint_obj = hint.as_object();
            auto pos_it = hint_obj.find("position");
            if (pos_it == hint_obj.end() || !pos_it->second.is_object()) {
                continue;
            }
            auto& pos_obj = pos_it->second.as_object();
            auto line_it = pos_obj.find("line");
            auto char_it = pos_obj.find("character");
            if (line_it == pos_obj.end() || char_it == pos_obj.end()) {
                continue;
            }
            const int line = static_cast<int>(line_it->second.as_integer());
            const int character = static_cast<int>(char_it->second.as_integer());
            char_it->second = JsonValue(static_cast<int64_t>(enc.to_utf16(line, character)));
        }
    }

    if (range_opt) {
        std::erase_if(hints, [&](const JsonValue& hint) {
            const auto& pos = hint["position"];
            const int line = static_cast<int>(pos["line"].as_integer());
            const int character = static_cast<int>(pos["character"].as_integer());
            return !position_within_range(line, character, *range_opt);
        });
    }

    return JsonValue(std::move(hints));
}

} // namespace luma::lsp
