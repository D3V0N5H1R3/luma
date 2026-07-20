#include <cstddef>
#include <string>
#include <vector>

#include "analysis/lexer/token.hpp"
#include "analysis/lexer/token_type.hpp"
#include "json/json.hpp"
#include "lsp_folding_handler.hpp"
#include "lsp_param_extraction.hpp"
#include "lsp_server_state_lock.hpp"
#include "lsp_types.hpp"

namespace luma::lsp {

// ═══════════════════════════════════════════════════════════
// Folding ranges
// ═══════════════════════════════════════════════════════════

namespace {

// Returns true for token types that introduce a braced block.
[[nodiscard]] constexpr bool is_block_keyword(TokenType type) {
    switch (type) {
        case TokenType::Function:
        case TokenType::If:
        case TokenType::Else:
        case TokenType::For:
        case TokenType::While:
        case TokenType::Match:
        case TokenType::Record:
        case TokenType::Choice:
        case TokenType::Try:
        case TokenType::Catch:
        case TokenType::Finally:
        case TokenType::TaskScope:
        case TokenType::Namespace:
        case TokenType::Interface:
            return true;
        default:
            return false;
    }
}

// Scan raw source text for consecutive comment lines (lines whose first
// non-whitespace character is '#') and emit folding ranges for each group.
void collect_comment_folds(const std::string& source, JsonValue::ArrayType& ranges) {
    int comment_start{-1};
    int comment_end{-1};
    int line{0};
    std::size_t pos{0};

    while (pos <= source.size()) {
        // Find end of current line.
        auto nl = source.find('\n', pos);
        if (nl == std::string::npos) {
            nl = source.size();
        }

        // Determine whether this line is a comment line.
        bool is_comment{false};
        for (std::size_t j = pos; j < nl; ++j) {
            const char c = source[j];
            if (c == ' ' || c == '\t' || c == '\r') {
                continue;
            }
            if (c == '#') {
                is_comment = true;
            }
            break;
        }

        if (is_comment) {
            if (comment_start < 0) {
                comment_start = line;
            }
            comment_end = line;
        } else {
            if (comment_start >= 0 && comment_end > comment_start) {
                ranges.push_back(
                    lsp_builders::folding_range(comment_start, comment_end, "comment"));
            }
            comment_start = -1;
        }

        ++line;
        if (nl == source.size()) {
            break;
        }
        pos = nl + 1;
    }

    // Flush a trailing comment block.
    if (comment_start >= 0 && comment_end > comment_start) {
        ranges.push_back(lsp_builders::folding_range(comment_start, comment_end, "comment"));
    }
}

// Fallback folding used when no analysis is available: pair raw '{' and '}'
// characters from the document text and emit a region fold for each
// multi-line pair.
void collect_brace_folds_from_text(const std::string& source, JsonValue::ArrayType& ranges) {
    std::vector<int> brace_stack;
    int line = 0;

    for (const char c : source) {
        if (c == '{') {
            brace_stack.push_back(line);
        } else if (c == '}' && !brace_stack.empty()) {
            const int start_line = brace_stack.back();
            brace_stack.pop_back();
            if (line > start_line) {
                ranges.push_back(lsp_builders::folding_range(start_line, line, "region"));
            }
        } else if (c == '\n') {
            ++line;
        }
    }
}

// Keyword-aware block folding from the token stream. For each '{', scan
// backward through the token stream to find the nearest block keyword
// (function, if, for, …). When found, the fold starts at the keyword line
// instead of the brace line, giving the editor a more meaningful fold label.
void collect_block_folds_from_tokens(const std::vector<Token>& tokens,
                                     JsonValue::ArrayType& ranges) {
    struct BraceEntry {
        int start_line; // keyword line (or brace line if no keyword)
        int brace_line; // line of the '{'
    };

    std::vector<BraceEntry> brace_stack;

    for (std::size_t i{0}; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::LeftBrace) {
            const int brace_line = tokens[i].location.line - 1; // 0-based
            int keyword_line{-1};

            // Scan backward for a block keyword, stopping at another
            // brace which would belong to a different block context.
            for (std::size_t k = i; k > 0; --k) {
                const auto& prev = tokens[k - 1];
                if (prev.type == TokenType::LeftBrace || prev.type == TokenType::RightBrace) {
                    break;
                }
                if (is_block_keyword(prev.type)) {
                    keyword_line = prev.location.line - 1;
                    break;
                }
            }

            const int start = (keyword_line >= 0) ? keyword_line : brace_line;
            brace_stack.push_back({.start_line = start, .brace_line = brace_line});
        } else if (tokens[i].type == TokenType::RightBrace && !brace_stack.empty()) {
            const auto entry = brace_stack.back();
            brace_stack.pop_back();
            const int end_line = tokens[i].location.line - 1;

            if (end_line > entry.start_line) {
                ranges.push_back(lsp_builders::folding_range(entry.start_line, end_line, "region"));
            }
        }
    }
}

} // anonymous namespace

JsonValue LspFoldingHandler::handle_folding_range(const JsonValue& params) {
    const auto uri_opt = extraction::extract_text_document_uri(params);
    if (!uri_opt) {
        return JsonValue(JsonValue::ArrayType{});
    }
    const auto& uri = *uri_opt;

    auto state = ctx_.acquire_read_lock();
    const auto cached = ctx_.find_analysis(uri);

    if (!cached) {
        // No analysis yet — fall back to simple brace-matching on raw text.
        const auto* doc_ptr = ctx_.doc_store.get_content(state.token(), uri);
        if (doc_ptr == nullptr) {
            return JsonValue(JsonValue::ArrayType{});
        }

        JsonValue::ArrayType ranges;
        collect_brace_folds_from_text(*doc_ptr, ranges);

        // Comment folding from raw text.
        collect_comment_folds(*doc_ptr, ranges);

        return JsonValue(std::move(ranges));
    }

    const auto& tokens = cached->semantic.tokens;

    JsonValue::ArrayType ranges;

    // ── Keyword-aware block folding ──
    collect_block_folds_from_tokens(tokens, ranges);

    // ── Consecutive include folding ──
    int inc_start{-1};
    int inc_end{-1};

    for (const auto& tok : tokens) {
        if (tok.type == TokenType::Include) {
            const int line = tok.location.line - 1;
            if (inc_start < 0) {
                inc_start = line;
                inc_end = line;
            } else if (line <= inc_end + 2) { // allow a blank line gap
                inc_end = line;
            } else {
                if (inc_end > inc_start) {
                    ranges.push_back(lsp_builders::folding_range(inc_start, inc_end, "imports"));
                }
                inc_start = line;
                inc_end = line;
            }
        }
    }

    if (inc_end > inc_start && inc_start >= 0) {
        ranges.push_back(lsp_builders::folding_range(inc_start, inc_end, "imports"));
    }

    // ── Multi-line string literal folding ──
    for (const auto& tok : tokens) {
        if (tok.type == TokenType::StringLiteral) {
            const int start_line = tok.location.line - 1;
            int nl_count{0};
            for (const char c : tok.lexeme) {
                if (c == '\n') {
                    ++nl_count;
                }
            }

            if (nl_count > 0) {
                ranges.push_back(
                    lsp_builders::folding_range(start_line, start_line + nl_count, "region"));
            }
        }
    }

    // ── Comment folding (from raw source text) ──
    const auto* doc_ptr = ctx_.doc_store.get_content(state.token(), uri);
    if (doc_ptr != nullptr) {
        collect_comment_folds(*doc_ptr, ranges);
    }

    return JsonValue(std::move(ranges));
}

} // namespace luma::lsp
