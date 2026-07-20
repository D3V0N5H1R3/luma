#ifndef LUMA_PARSER_ERROR_RECOVERY_HPP
#define LUMA_PARSER_ERROR_RECOVERY_HPP

#include "analysis/lexer/token_type.hpp"

namespace luma {

/// Encapsulates the parser's error recovery strategies as free functions.
///
/// The parser uses three cooperating recovery mechanisms when it
/// encounters an unexpected token:
///
///   skip_and_retry  — The NEXT token matches what we expected, so skip
///                     the current (extraneous) token and consume the
///                     expected one.
///   insert_expected — For closing delimiters, assume the token was
///                     simply omitted and continue without consuming.
///   synchronize     — Advance past tokens until a likely statement or
///                     declaration boundary is found.
///
/// These free functions centralise the heuristics used by recover_expect()
/// and synchronize() so they can be tested and tuned independently of the
/// parser's grammar rules.
namespace error_recovery {

enum class Strategy {
    skip_and_retry,  // Skip current token and try again
    insert_expected, // Pretend the expected token was there
    synchronize      // Skip to next statement boundary
};

/// Check if a token is one of the top-level declaration keywords
/// (function, record, choice, interface, namespace, type, include, use,
/// internal, or an annotation).  Single source of truth for the declaration
/// keyword set shared by the parser's is_declaration_start() and the
/// is_statement_start() heuristic below.
[[nodiscard]] constexpr bool is_declaration_keyword(TokenType type) noexcept {
    switch (type) {
        case TokenType::Function:
        case TokenType::Record:
        case TokenType::Choice:
        case TokenType::Interface:
        case TokenType::Namespace:
        case TokenType::Type:
        case TokenType::Include:
        case TokenType::Use:
        case TokenType::Internal:
        case TokenType::Annotation:
            return true;
        default:
            return false;
    }
}

/// Check if a token is a closing delimiter ()/}/]).
[[nodiscard]] constexpr bool is_closing_delimiter(TokenType type) noexcept {
    return type == TokenType::RightParen || type == TokenType::RightBrace ||
           type == TokenType::RightBracket;
}

/// Check if a token looks like the start of a new statement or
/// declaration.  Used by both recover_expect and synchronize as
/// the "safe landing" test.
[[nodiscard]] constexpr bool is_statement_start(TokenType type) noexcept {
    if (is_type_keyword(type) || is_declaration_keyword(type)) {
        return true;
    }

    switch (type) {
        case TokenType::Mutable:
        case TokenType::Return:
        case TokenType::If:
        case TokenType::For:
        case TokenType::While:
        case TokenType::Match:
        case TokenType::Try:
        case TokenType::TaskScope:
        case TokenType::Spawn:
        case TokenType::Break:
        case TokenType::Continue:
        case TokenType::Identifier:
            return true;
        default:
            return false;
    }
}

/// Choose the best recovery strategy based on context.
///
/// @param expected       The token type the parser expected.
/// @param found          The token type actually present.
/// @param next           The token type after the current one.
/// @param at_end         True if the parser is at end-of-file.
/// @return The recommended recovery strategy.
[[nodiscard]] constexpr Strategy choose_strategy(TokenType expected, TokenType found,
                                                 TokenType next, bool at_end) noexcept {
    // Strategy 1: The next token matches — assume the current token is
    // extraneous.  Skip it and consume the expected one.
    if (!at_end && next == expected) {
        return Strategy::skip_and_retry;
    }

    // Strategy 2: For closing delimiters, assume the token was simply
    // omitted and continue without consuming anything.
    if (is_closing_delimiter(expected)) {
        return Strategy::insert_expected;
    }

    // Strategy 3: The current token looks like it could start the next
    // construct — let the caller continue (the missing token is
    // implicitly inserted).
    if (is_statement_start(found)) {
        return Strategy::insert_expected;
    }

    // Cannot recover locally — synchronize to a statement boundary.
    return Strategy::synchronize;
}

} // namespace error_recovery

} // namespace luma

#endif // LUMA_PARSER_ERROR_RECOVERY_HPP
