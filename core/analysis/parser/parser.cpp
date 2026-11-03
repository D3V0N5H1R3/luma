#include "analysis/parser/parser.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "analysis/lexer/token.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/parser/parser_messages.hpp"
#include "analysis/parser/type_annotation_parser.hpp"
#include "analysis/source/source_location.hpp"

namespace luma {

namespace {

// Opening delimiters that error recovery treats as one "group" nesting level
// (everything except '{', whose closer also ends blocks).
bool is_group_opener(TokenType type) {
    return type == TokenType::LeftParen || type == TokenType::LeftBracket ||
           type == TokenType::QuestionBracket || type == TokenType::InterpolationStart;
}

// True if `closer` closes a construct opened by `opener`.
bool closes_delimiter(TokenType opener, TokenType closer) {
    switch (closer) {
        case TokenType::RightBrace:
            return opener == TokenType::LeftBrace;
        case TokenType::RightParen:
            return opener == TokenType::LeftParen;
        case TokenType::RightBracket:
            return opener == TokenType::LeftBracket || opener == TokenType::QuestionBracket;
        case TokenType::InterpolationEnd:
            return opener == TokenType::InterpolationStart;
        default:
            return false;
    }
}

// Returns a context-aware hint for an unexpected token in expect().
std::string expect_hint(TokenType expected) {
    switch (expected) {
        case TokenType::RightParen:
            return std::string{parser_msg::hint_missing_right_paren};
        case TokenType::RightBrace:
            return std::string{parser_msg::hint_missing_right_brace};
        case TokenType::RightBracket:
            return std::string{parser_msg::hint_missing_right_bracket};
        case TokenType::LeftParen:
            return std::string{parser_msg::hint_expect_left_paren};
        case TokenType::LeftBrace:
            return std::string{parser_msg::hint_expect_left_brace};
        case TokenType::Equals:
            return std::string{parser_msg::hint_expect_equals};
        case TokenType::Colon:
            return std::string{parser_msg::hint_expect_colon};
        case TokenType::Arrow:
            return std::string{parser_msg::hint_expect_arrow};
        case TokenType::Identifier:
            return std::string{parser_msg::hint_expect_identifier};
        default:
            return std::format("expected {} here", token_type_to_string(expected));
    }
}

} // namespace

// Returns a hint when the current identifier matches a keyword from another
// programming language, helping users transition to Luma syntax.
//
// Hint text may contain a single '{}' placeholder that is replaced with the
// matched keyword; entries without '{}' are returned verbatim.
std::optional<std::string> Parser::foreign_keyword_hint(std::string_view lexeme) {
    // Maps foreign-language keywords to Luma-specific hint strings.
    // unordered_map is chosen for O(1) average lookup; the map is small but is
    // consulted on every unrecognised identifier, so a linear scan would add up.
    //
    // To add a new entry: pick the foreign keyword as the key and write a hint
    // string (may contain a single '{}' placeholder that will be replaced with
    // the matched keyword).  Mirror the entry in the corresponding test in
    // tests/analysis/parser/ so the hint is covered by regression tests.
    static const std::unordered_map<std::string_view, std::string_view> k_hints{
        {"const", parser_msg::hint_immutable_by_default},
        {"enum", parser_msg::hint_did_you_mean_choice},
        {"elif", parser_msg::hint_use_else_if},
        {"elsif", parser_msg::hint_use_else_if},
        {"var", parser_msg::hint_var_decl},
        {"let", parser_msg::hint_var_decl},
        {"val", parser_msg::hint_var_decl},
        {"fn", parser_msg::hint_did_you_mean_function},
        {"func", parser_msg::hint_did_you_mean_function},
        {"def", parser_msg::hint_did_you_mean_function},
        {"sub", parser_msg::hint_did_you_mean_function},
        {"class", parser_msg::hint_did_you_mean_record},
        {"struct", parser_msg::hint_did_you_mean_record},
        {"trait", parser_msg::hint_did_you_mean_interface},
        {"protocol", parser_msg::hint_did_you_mean_interface},
        {"abstract", parser_msg::hint_did_you_mean_interface},
        {"import", parser_msg::hint_did_you_mean_include},
        {"require", parser_msg::hint_did_you_mean_include},
    };

    const auto it = k_hints.find(lexeme);
    if (it == k_hints.end()) {
        return std::nullopt;
    }

    const auto& hint_template = it->second;
    if (hint_template.find("{}") != std::string_view::npos) {
        return std::vformat(hint_template, std::make_format_args(lexeme));
    }
    return std::string{hint_template};
}

// ──────────── Lookahead helpers ────────────

TokenType Parser::token_type_at(int offset) const {
    const int idx = token_pos_ + offset;

    if (idx < 0 || idx >= token_count_) {
        return TokenType::EndOfFile;
    }

    return tokens_[static_cast<std::size_t>(idx)].type;
}

bool Parser::can_start_expression(TokenType type) {
    switch (type) {
        // Literals
        case TokenType::IntegerLiteral:
        case TokenType::NumberLiteral:
        case TokenType::StringLiteral:
        case TokenType::StringStart:
        case TokenType::BooleanLiteral:
        case TokenType::NoneLiteral:
        // Identifiers
        case TokenType::Identifier:
        // Grouping / collections
        case TokenType::LeftParen:
        case TokenType::LeftBracket:
        case TokenType::LeftBrace:
        // Prefix unary operators
        case TokenType::Bang:
        case TokenType::Minus:
        // Keyword expressions
        case TokenType::If:
        case TokenType::Match:
        case TokenType::Spawn:
        case TokenType::TaskScope:
        case TokenType::Await:
        case TokenType::Success:
        case TokenType::Failure:
        case TokenType::Some:
        case TokenType::Downcast:
        case TokenType::TrustedDowncast:
        case TokenType::Is:
            return true;
        default:
            return false;
    }
}

int Parser::find_matching_paren(int start_offset) const {
    int offset = start_offset;
    int depth = 1;

    while (depth > 0 && token_pos_ + offset < token_count_) {
        const auto type =
            tokens_[static_cast<std::size_t>(token_pos_) + static_cast<std::size_t>(offset)].type;

        if (type == TokenType::LeftParen) {
            ++depth;
        } else if (type == TokenType::RightParen) {
            --depth;
        }

        ++offset;
    }

    return offset;
}

// ──────────── Diagnostic helpers ────────────

void Parser::emit_error(std::string_view message, const SourceLocation& start,
                        std::string_view hint, std::optional<SourceLocation> end) {
    auto builder = diag::error(std::string{message})
                       .category(DiagnosticCategory::Syntax)
                       .source(DiagnosticSource::Syntax);

    // Token and AST locations mark the position one past a token's last
    // character; anchor the diagnostic on the token's real extent instead so
    // the rendered caret (and the LSP squiggle) starts at the token itself.
    const Token* token = end.has_value() ? nullptr : find_token_ending_at(start);

    if (end.has_value()) {
        builder.primary(start, *end);
    } else if (token != nullptr && token->start_location.line != 0) {
        builder.primary(token->start_location, token->location);
    } else {
        builder.primary(start);
    }

    if (!hint.empty()) {
        builder.hint(std::string{hint});
    }

    emit(builder.build());
}

const Token* Parser::find_token_ending_at(const SourceLocation& loc) const {
    const auto it = std::ranges::lower_bound(tokens_, loc, {}, &Token::location);

    if (it == tokens_.end() || it->location != loc) {
        return nullptr;
    }

    return &*it;
}

void Parser::syntax_error(std::string_view message, const SourceLocation& loc,
                          std::string_view hint) {
    emit_error(message, loc, hint);
    throw ParseFailure{};
}

void Parser::ensure_chain_within_limit(std::size_t chain_length, const SourceLocation& loc) {
    if (chain_length > static_cast<std::size_t>(ResourceLimits::max_expression_depth)) {
        syntax_error("expression exceeds the maximum nesting depth", loc,
                     "split this long chain into smaller sub-expressions or "
                     "intermediate variables");
    }
}

// ──────────── Public ────────────

Program Parser::parse() {
    Program program;

    while (!is_at_end()) {
        // Stop parsing after too many errors to avoid flooding diagnostics.
        if (diagnostics().size() >= k_max_parse_errors) {
            break;
        }

        try {
            if (is_declaration_start()) {
                program.declarations.push_back(parse_declaration());
            } else {
                program.statements.push_back(parse_statement());
            }
        } catch (const ParseFailure&) {
            // The diagnostic was already emitted at the throw site via
            // syntax_error().  Just synchronize to the next statement
            // boundary and continue parsing.
            synchronize();
        }
    }

    return program;
}

// ──────────── Token helpers ────────────

bool Parser::is_at_end() const {
    return token_pos_ >= token_count_ ||
           tokens_[static_cast<std::size_t>(token_pos_)].type == TokenType::EndOfFile;
}

const Token& Parser::current() const {
    if (token_pos_ >= token_count_) {
        return tokens_.back();
    }

    return tokens_[static_cast<std::size_t>(token_pos_)];
}

const Token& Parser::peek_at(int offset) const {
    const int idx = token_pos_ + offset;

    if (idx < 0 || idx >= token_count_) {
        return tokens_.back();
    }

    return tokens_[static_cast<std::size_t>(idx)];
}

bool Parser::check(TokenType type) const {
    return !is_at_end() && current().type == type;
}

bool Parser::check_at(int offset, TokenType type) const {
    const int idx = token_pos_ + offset;

    return idx >= 0 && idx < token_count_ && tokens_[static_cast<std::size_t>(idx)].type == type;
}

const Token& Parser::advance() {
    const Token& token = current();

    if (!is_at_end()) {
        ++token_pos_;
    }

    return token;
}

std::string Parser::build_expect_message(TokenType expected) const {
    return std::format("expected {}, got {} '{}'", token_type_to_string(expected),
                       token_type_to_string(current().type), current().lexeme);
}

const Token& Parser::expect(TokenType type) {
    if (!check(type)) {
        if (type == TokenType::Identifier && is_keyword_token_type(current().type)) {
            syntax_error(std::format("'{}' is a reserved keyword and cannot "
                                     "be used as an identifier",
                                     current().lexeme),
                         current().location,
                         "choose a different name for the variable or function");
        }

        // Detect keywords from other languages used by mistake.
        if (current().type == TokenType::Identifier) {
            const auto hint = foreign_keyword_hint(current().lexeme);

            if (hint.has_value()) {
                syntax_error(std::format("unexpected '{}' here", current().lexeme),
                             current().location, *hint);
            }
        }

        syntax_error(build_expect_message(type), current().location, expect_hint(type));
    }

    return advance();
}

bool Parser::consume_if(TokenType type) {
    if (check(type)) {
        advance();

        return true;
    }

    return false;
}

// Parser error recovery: when an expected token is missing, skip tokens
// until a synchronisation point (semicolons, closing delimiters, keywords
// that start new statements) is found. This allows the parser to continue
// and report multiple errors in a single pass rather than stopping at
// the first error.
void Parser::recover_expect(TokenType type) {
    // Happy path: the expected token is present.
    if (check(type)) {
        advance();

        return;
    }

    // Record the error.
    const auto message = build_expect_message(type);
    const auto hint = expect_hint(type);

    emit_error(message, current().location, hint);

    // Delegate strategy selection to the recovery policy.
    const auto strategy =
        error_recovery::choose_strategy(type, current().type, peek_at(1).type, is_at_end());

    switch (strategy) {
        case error_recovery::Strategy::SkipAndRetry:
            advance(); // skip the extraneous token
            advance(); // consume the expected token
            return;

        case error_recovery::Strategy::InsertExpected:
            return; // act as if the token was inserted

        case error_recovery::Strategy::Synchronize:
            // No local recovery applies; unwind to the nearest recovery point
            // like expect() does (the diagnostic was emitted above).
            throw ParseFailure{};
    }
}

bool Parser::looks_like_statement_start() const {
    return error_recovery::is_statement_start(current().type);
}

bool Parser::is_declaration_start() const {
    // `function` may also begin a variable declaration (e.g. a function-typed
    // variable), so it only starts a declaration when it is not that.
    if (check(TokenType::Function)) {
        return !looks_like_variable_decl();
    }

    return error_recovery::is_declaration_keyword(current().type);
}

Parser::OpenDelimiters Parser::unclosed_delimiters_since(int item_start) const {
    std::vector<TokenType> open;

    for (int pos = std::max(item_start, 0); pos < token_pos_ && pos < token_count_; ++pos) {
        const TokenType type = tokens_[static_cast<std::size_t>(pos)].type;

        if (is_group_opener(type) || type == TokenType::LeftBrace) {
            open.push_back(type);

            continue;
        }

        // A closer pops back to its opener.  Openers above it were left
        // unclosed and already recovered by recover_expect() (treated as
        // inserted), so they are dropped with it.  A stray closer is ignored.
        const auto match = std::find_if(open.rbegin(), open.rend(), [type](TokenType opener) {
            return closes_delimiter(opener, type);
        });

        if (match != open.rend()) {
            open.erase(std::prev(match.base()), open.end());
        }
    }

    OpenDelimiters depths;

    for (const TokenType opener : open) {
        if (opener == TokenType::LeftBrace) {
            ++depths.braces;
        } else {
            ++depths.groups;
        }
    }

    return depths;
}

void Parser::synchronize() {
    // Advance past the problematic token.
    advance();

    // Track delimiter nesting so we don't break out of a nested block
    // prematurely when scanning for a statement boundary.
    // Brace depth is tracked separately because '}' also signals a
    // statement boundary.  Parens and brackets share a single counter
    // since they are treated identically during error recovery.
    int brace_depth = 0;
    int paren_bracket_depth = 0;

    while (!is_at_end()) {
        switch (current().type) {
            case TokenType::LeftBrace:
                ++brace_depth;
                break;
            case TokenType::LeftParen:
            case TokenType::LeftBracket:
                ++paren_bracket_depth;
                break;
            case TokenType::RightParen:
            case TokenType::RightBracket:
                if (paren_bracket_depth > 0) {
                    --paren_bracket_depth;
                }
                break;
            default:
                break;
        }

        // Only synchronize at the top nesting level.
        if (brace_depth == 0 && paren_bracket_depth == 0) {
            // Stop at tokens that typically start a new statement or declaration.
            if (looks_like_statement_start()) {
                return;
            }

            // A closing brace likely ends a block — stop after it so the
            // next iteration of parse() sees whatever follows.
            if (current().type == TokenType::RightBrace) {
                advance();

                return;
            }
        } else if (current().type == TokenType::RightBrace) {
            if (brace_depth > 0) {
                --brace_depth;
            } else {
                // Unmatched closing brace at top level — stop after it.
                advance();

                return;
            }
        }

        advance();
    }
}

bool Parser::can_start_block_item(BlockItem item) const {
    const TokenType type = current().type;

    switch (item) {
        case BlockItem::Statement:
            return looks_like_statement_start() ||
                   (can_start_expression(type) && type != TokenType::LeftBrace);
        case BlockItem::MatchArm:
            return type == TokenType::Case || type == TokenType::Else ||
                   type == TokenType::Success || type == TokenType::Failure;
        case BlockItem::Declaration:
            return error_recovery::is_declaration_keyword(type);
        case BlockItem::Variant:
            return type == TokenType::Identifier;
    }

    return false;
}

void Parser::synchronize_in_block(BlockItem item, int item_start) {
    const int error_position = token_pos_;

    // Start inside whatever call, array or record literal the failed item
    // left open, so the skip runs to that construct's closer.
    const OpenDelimiters open = unclosed_delimiters_since(item_start);
    int brace_depth = open.braces;
    int group_depth = open.groups; // parentheses, brackets and interpolations

    while (!is_at_end()) {
        const TokenType type = current().type;

        // A '}' with no '{' open inside the skipped construct is the
        // enclosing block's closer (any group still open was never closed):
        // leave it for the block to consume.
        if (type == TokenType::RightBrace && brace_depth == 0) {
            return;
        }

        const bool at_block_level = brace_depth == 0 && group_depth == 0;

        // A new line that can begin the next item ends the skip.  The error
        // token itself never counts, so every recovery makes progress.
        if (at_block_level && token_pos_ > error_position && current().preceded_by_newline &&
            can_start_block_item(item)) {
            return;
        }

        switch (type) {
            case TokenType::LeftBrace:
                ++brace_depth;
                break;
            case TokenType::RightBrace:
                --brace_depth;
                break;
            case TokenType::LeftParen:
            case TokenType::LeftBracket:
            case TokenType::QuestionBracket:
            case TokenType::InterpolationStart:
                ++group_depth;
                break;
            case TokenType::RightParen:
            case TokenType::RightBracket:
            case TokenType::InterpolationEnd:
                if (group_depth > 0) {
                    --group_depth;
                }
                break;
            default:
                break;
        }

        advance();
    }
}

// ──────────── Type annotation parsing (delegating stubs) ────────────
// Full implementations live in type_annotation_parser.cpp.
// TypeAnnotationParser is a friend of Parser and accesses private state directly.

void Parser::close_generic() {
    TypeAnnotationParser::close_generic(*this);
}

TypeAnnotation Parser::parse_type_annotation() {
    return TypeAnnotationParser::parse(*this);
}

std::vector<TypeParam> Parser::parse_type_param_list() {
    return TypeAnnotationParser::parse_type_param_list(*this);
}

std::vector<TypeParam> Parser::parse_optional_type_params() {
    return TypeAnnotationParser::parse_optional_type_params(*this);
}

} // namespace luma
