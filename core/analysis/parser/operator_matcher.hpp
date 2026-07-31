// ─────────────────────────────────────────────────────────────────────────────
// Operator Matcher
// ─────────────────────────────────────────────────────────────────────────────
// Responsibility: Centralised mapping from token types to operator categories,
//                 with precedence lookup and classification helpers.
//
// The parser, type checker, and compiler all need to classify operator tokens.
// This header-only module provides a single source of truth for:
//   - Which token types are binary/unary/compound-assignment operators
//   - Operator precedence levels for binary operators
//   - Operator category classification (arithmetic, comparison, etc.)
//
// Dependencies:
//   - analysis/lexer/token_type.hpp: TokenType enum
// ─────────────────────────────────────────────────────────────────────────────

#ifndef LUMA_PARSER_OPERATOR_MATCHER_HPP
#define LUMA_PARSER_OPERATOR_MATCHER_HPP

#include <optional>

#include "analysis/lexer/token_type.hpp"

namespace luma {

// Precedence levels for binary operators, ordered from lowest to highest.
// Higher numeric values bind more tightly.
enum class Precedence : int {
    None = 0,
    NullCoalescing = 1, // ??
    LogicalOr = 2,      // ||
    LogicalAnd = 3,     // &&
    Equality = 4,       // ==  !=
    Comparison = 5,     // <  >  <=  >=  in
    Additive = 6,       // +  -
    Multiplicative = 7, // *  /  //  %
};

// Category of operator for semantic classification.
enum class OperatorCategory {
    Arithmetic,
    Comparison,
    Logical,
    NullCoalescing,
    Membership,
};

// ──────────── Binary operator queries ────────────

// Returns the precedence of a binary operator token, or std::nullopt if the
// token is not a binary operator.
[[nodiscard]] constexpr std::optional<Precedence>
binary_operator_precedence(TokenType type) noexcept {
    switch (type) {
        case TokenType::QuestionQuestion:
            return Precedence::NullCoalescing;

        case TokenType::PipePipe:
            return Precedence::LogicalOr;

        case TokenType::AmpersandAmpersand:
            return Precedence::LogicalAnd;

        case TokenType::EqualsEquals:
        case TokenType::BangEquals:
            return Precedence::Equality;

        case TokenType::Less:
        case TokenType::Greater:
        case TokenType::LessEquals:
        case TokenType::GreaterEquals:
        case TokenType::In:
            return Precedence::Comparison;

        case TokenType::Plus:
        case TokenType::Minus:
            return Precedence::Additive;

        case TokenType::Star:
        case TokenType::Slash:
        case TokenType::SlashSlash:
        case TokenType::Percent:
            return Precedence::Multiplicative;

        default:
            return std::nullopt;
    }
}

// Returns true if the token type is a binary operator.
[[nodiscard]] constexpr bool is_binary_operator(TokenType type) noexcept {
    return binary_operator_precedence(type).has_value();
}

// ──────────── Unary operator queries ────────────

// Returns true if the token type is a prefix unary operator (!, -).
[[nodiscard]] constexpr bool is_prefix_unary_operator(TokenType type) noexcept {
    switch (type) {
        case TokenType::Bang:
        case TokenType::Minus:
            return true;
        default:
            return false;
    }
}

// Returns true if the token type is a postfix unary operator (?).
[[nodiscard]] constexpr bool is_postfix_unary_operator(TokenType type) noexcept {
    return type == TokenType::QuestionMark;
}

// ──────────── Compound assignment queries ────────────

// Returns true if the token type is a compound assignment operator
// (+=, -=, *=, /=, //=, %=).
[[nodiscard]] constexpr bool is_compound_assignment_operator(TokenType type) noexcept {
    switch (type) {
        case TokenType::PlusEquals:
        case TokenType::MinusEquals:
        case TokenType::StarEquals:
        case TokenType::SlashEquals:
        case TokenType::SlashSlashEquals:
        case TokenType::PercentEquals:
            return true;
        default:
            return false;
    }
}

// Returns the base binary operator for a compound assignment token.
// For example, PlusEquals → Plus, StarEquals → Star.
// Returns std::nullopt if the token is not a compound assignment.
[[nodiscard]] constexpr std::optional<TokenType>
compound_assignment_base_operator(TokenType type) noexcept {
    switch (type) {
        case TokenType::PlusEquals:
            return TokenType::Plus;
        case TokenType::MinusEquals:
            return TokenType::Minus;
        case TokenType::StarEquals:
            return TokenType::Star;
        case TokenType::SlashEquals:
            return TokenType::Slash;
        case TokenType::SlashSlashEquals:
            return TokenType::SlashSlash;
        case TokenType::PercentEquals:
            return TokenType::Percent;
        default:
            return std::nullopt;
    }
}

// ──────────── Operator category classification ────────────

// Returns the semantic category of a binary operator, or std::nullopt if the
// token is not a binary operator.
[[nodiscard]] constexpr std::optional<OperatorCategory>
binary_operator_category(TokenType type) noexcept {
    switch (type) {
        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Star:
        case TokenType::Slash:
        case TokenType::SlashSlash:
        case TokenType::Percent:
            return OperatorCategory::Arithmetic;

        case TokenType::EqualsEquals:
        case TokenType::BangEquals:
        case TokenType::Less:
        case TokenType::Greater:
        case TokenType::LessEquals:
        case TokenType::GreaterEquals:
            return OperatorCategory::Comparison;

        case TokenType::PipePipe:
        case TokenType::AmpersandAmpersand:
            return OperatorCategory::Logical;

        case TokenType::QuestionQuestion:
            return OperatorCategory::NullCoalescing;

        case TokenType::In:
            return OperatorCategory::Membership;

        default:
            return std::nullopt;
    }
}

// ──────────── Convenience predicates ────────────

[[nodiscard]] constexpr bool is_arithmetic_operator(TokenType type) noexcept {
    return binary_operator_category(type) == OperatorCategory::Arithmetic;
}

[[nodiscard]] constexpr bool is_logical_operator(TokenType type) noexcept {
    return binary_operator_category(type) == OperatorCategory::Logical;
}

} // namespace luma

#endif // LUMA_PARSER_OPERATOR_MATCHER_HPP
