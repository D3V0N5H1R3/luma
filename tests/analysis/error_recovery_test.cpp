// error_recovery namespace unit tests.

#include <array>

#include "analysis/lexer/token_type.hpp"
#include "analysis/parser/error_recovery.hpp"
#include "test_framework.hpp"

using luma::TokenType;
using luma::error_recovery::choose_strategy;
using luma::error_recovery::is_closing_delimiter;
using luma::error_recovery::is_statement_start;
using luma::error_recovery::Strategy;

// ─── choose_strategy tests ───

static void test_skip_and_retry_when_next_matches() {
    const auto strategy =
        choose_strategy(TokenType::RightParen, TokenType::Comma, TokenType::RightParen, false);
    ASSERT_EQ(strategy, Strategy::SkipAndRetry);
}

static void test_skip_and_retry_not_at_end() {
    // At EOF, skip_and_retry should not be chosen.
    const auto strategy =
        choose_strategy(TokenType::RightParen, TokenType::Comma, TokenType::RightParen, true);
    // at_end == true, so skip_and_retry is not selected even if next matches.
    ASSERT_NE(strategy, Strategy::SkipAndRetry);
}

static void test_insert_expected_for_closing_delimiter_rparen() {
    const auto strategy =
        choose_strategy(TokenType::RightParen, TokenType::IntegerLiteral, TokenType::Comma, false);
    ASSERT_EQ(strategy, Strategy::InsertExpected);
}

static void test_insert_expected_for_closing_delimiter_rbrace() {
    const auto strategy =
        choose_strategy(TokenType::RightBrace, TokenType::Identifier, TokenType::EndOfFile, false);
    ASSERT_EQ(strategy, Strategy::InsertExpected);
}

static void test_insert_expected_for_closing_delimiter_rbracket() {
    const auto strategy =
        choose_strategy(TokenType::RightBracket, TokenType::Identifier, TokenType::Plus, false);
    ASSERT_EQ(strategy, Strategy::InsertExpected);
}

static void test_insert_expected_when_found_is_statement_start() {
    // Expected something else, but found a statement-starting token.
    const auto strategy =
        choose_strategy(TokenType::Equals, TokenType::If, TokenType::LeftBrace, false);
    ASSERT_EQ(strategy, Strategy::InsertExpected);
}

static void test_synchronize_as_fallback() {
    // None of the special conditions match — fall back to synchronize.
    const auto strategy =
        choose_strategy(TokenType::Colon, TokenType::Plus, TokenType::Star, false);
    ASSERT_EQ(strategy, Strategy::Synchronize);
}

static void test_synchronize_at_eof() {
    const auto strategy =
        choose_strategy(TokenType::Colon, TokenType::Plus, TokenType::EndOfFile, true);
    ASSERT_EQ(strategy, Strategy::Synchronize);
}

// ─── is_statement_start tests ───

// Declaration keywords, statement keywords, identifiers, and type keywords
// all begin a statement.
constexpr std::array statement_start_tokens{
    TokenType::Function,   TokenType::Record,      TokenType::Choice,      TokenType::Interface,
    TokenType::Namespace,  TokenType::Type,        TokenType::Include,     TokenType::Use,
    TokenType::Internal,   TokenType::Annotation,  TokenType::Mutable,     TokenType::Return,
    TokenType::If,         TokenType::For,         TokenType::While,       TokenType::Match,
    TokenType::Try,        TokenType::TaskScope,   TokenType::Spawn,       TokenType::Break,
    TokenType::Continue,   TokenType::Identifier,  TokenType::IntegerType, TokenType::NumberType,
    TokenType::StringType, TokenType::BooleanType,
};

// Operators, literals, and delimiters never begin a statement.
constexpr std::array non_statement_start_tokens{
    TokenType::Plus,          TokenType::Star,           TokenType::Equals,
    TokenType::Comma,         TokenType::IntegerLiteral, TokenType::NumberLiteral,
    TokenType::StringLiteral, TokenType::LeftParen,      TokenType::RightParen,
    TokenType::LeftBrace,     TokenType::RightBrace,
};

static void test_is_statement_start(TokenType type) {
    ASSERT_TRUE(is_statement_start(type));
}

static void test_not_statement_start(TokenType type) {
    ASSERT_FALSE(is_statement_start(type));
}

// ─── is_closing_delimiter tests ───

constexpr std::array closing_delimiter_tokens{
    TokenType::RightParen,
    TokenType::RightBrace,
    TokenType::RightBracket,
};

constexpr std::array non_closing_delimiter_tokens{
    TokenType::LeftParen, TokenType::LeftBrace,  TokenType::LeftBracket,
    TokenType::EndOfFile, TokenType::Identifier,
};

static void test_is_closing_delimiter(TokenType type) {
    ASSERT_TRUE(is_closing_delimiter(type));
}

static void test_not_closing_delimiter(TokenType type) {
    ASSERT_FALSE(is_closing_delimiter(type));
}

int main() {
    // choose_strategy
    RUN(test_skip_and_retry_when_next_matches);
    RUN(test_skip_and_retry_not_at_end);
    RUN(test_insert_expected_for_closing_delimiter_rparen);
    RUN(test_insert_expected_for_closing_delimiter_rbrace);
    RUN(test_insert_expected_for_closing_delimiter_rbracket);
    RUN(test_insert_expected_when_found_is_statement_start);
    RUN(test_synchronize_as_fallback);
    RUN(test_synchronize_at_eof);
    // is_statement_start
    RUN_PARAMETERIZED(test_is_statement_start, statement_start_tokens);
    RUN_PARAMETERIZED(test_not_statement_start, non_statement_start_tokens);
    // is_closing_delimiter
    RUN_PARAMETERIZED(test_is_closing_delimiter, closing_delimiter_tokens);
    RUN_PARAMETERIZED(test_not_closing_delimiter, non_closing_delimiter_tokens);
    return SUMMARY();
}
