// error_recovery namespace unit tests.

#include "analysis/lexer/token_type.hpp"
#include "analysis/parser/error_recovery.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── choose_strategy tests ───

static void test_skip_and_retry_when_next_matches() {
    auto strategy = error_recovery::choose_strategy(TokenType::RightParen, TokenType::Comma,
                                                    TokenType::RightParen, false);
    ASSERT_EQ(strategy, error_recovery::Strategy::skip_and_retry);
}

static void test_skip_and_retry_not_at_end() {
    // At EOF, skip_and_retry should not be chosen.
    auto strategy = error_recovery::choose_strategy(TokenType::RightParen, TokenType::Comma,
                                                    TokenType::RightParen, true);
    // at_end == true, so skip_and_retry is not selected even if next matches.
    ASSERT_NE(static_cast<int>(strategy),
              static_cast<int>(error_recovery::Strategy::skip_and_retry));
}

static void test_insert_expected_for_closing_delimiter_rparen() {
    auto strategy = error_recovery::choose_strategy(
        TokenType::RightParen, TokenType::IntegerLiteral, TokenType::Comma, false);
    ASSERT_EQ(strategy, error_recovery::Strategy::insert_expected);
}

static void test_insert_expected_for_closing_delimiter_rbrace() {
    auto strategy = error_recovery::choose_strategy(TokenType::RightBrace, TokenType::Identifier,
                                                    TokenType::EndOfFile, false);
    ASSERT_EQ(strategy, error_recovery::Strategy::insert_expected);
}

static void test_insert_expected_for_closing_delimiter_rbracket() {
    auto strategy = error_recovery::choose_strategy(TokenType::RightBracket, TokenType::Identifier,
                                                    TokenType::Plus, false);
    ASSERT_EQ(strategy, error_recovery::Strategy::insert_expected);
}

static void test_insert_expected_when_found_is_statement_start() {
    // Expected something else, but found a statement-starting token.
    auto strategy = error_recovery::choose_strategy(TokenType::Equals, TokenType::If,
                                                    TokenType::LeftBrace, false);
    ASSERT_EQ(strategy, error_recovery::Strategy::insert_expected);
}

static void test_synchronize_as_fallback() {
    // None of the special conditions match — fall back to synchronize.
    auto strategy =
        error_recovery::choose_strategy(TokenType::Colon, TokenType::Plus, TokenType::Star, false);
    ASSERT_EQ(strategy, error_recovery::Strategy::synchronize);
}

static void test_synchronize_at_eof() {
    auto strategy = error_recovery::choose_strategy(TokenType::Colon, TokenType::Plus,
                                                    TokenType::EndOfFile, true);
    ASSERT_EQ(strategy, error_recovery::Strategy::synchronize);
}

// ─── is_statement_start tests ───

static void test_is_statement_start_function() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Function));
}

static void test_is_statement_start_record() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Record));
}

static void test_is_statement_start_choice() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Choice));
}

static void test_is_statement_start_interface() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Interface));
}

static void test_is_statement_start_namespace() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Namespace));
}

static void test_is_statement_start_type() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Type));
}

static void test_is_statement_start_include() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Include));
}

static void test_is_statement_start_use() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Use));
}

static void test_is_statement_start_internal() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Internal));
}

static void test_is_statement_start_annotation() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Annotation));
}

static void test_is_statement_start_mutable() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Mutable));
}

static void test_is_statement_start_return() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Return));
}

static void test_is_statement_start_if() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::If));
}

static void test_is_statement_start_for() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::For));
}

static void test_is_statement_start_while() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::While));
}

static void test_is_statement_start_match() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Match));
}

static void test_is_statement_start_try() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Try));
}

static void test_is_statement_start_task_scope() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::TaskScope));
}

static void test_is_statement_start_spawn() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Spawn));
}

static void test_is_statement_start_break() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Break));
}

static void test_is_statement_start_continue() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Continue));
}

static void test_is_statement_start_identifier() {
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::Identifier));
}

static void test_is_statement_start_type_keywords() {
    // Type keywords should be recognised as statement starters.
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::IntegerType));
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::NumberType));
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::StringType));
    ASSERT_TRUE(error_recovery::is_statement_start(TokenType::BooleanType));
}

static void test_not_statement_start_operators() {
    ASSERT_FALSE(error_recovery::is_statement_start(TokenType::Plus));
    ASSERT_FALSE(error_recovery::is_statement_start(TokenType::Star));
    ASSERT_FALSE(error_recovery::is_statement_start(TokenType::Equals));
    ASSERT_FALSE(error_recovery::is_statement_start(TokenType::Comma));
}

static void test_not_statement_start_literals() {
    ASSERT_FALSE(error_recovery::is_statement_start(TokenType::IntegerLiteral));
    ASSERT_FALSE(error_recovery::is_statement_start(TokenType::NumberLiteral));
    ASSERT_FALSE(error_recovery::is_statement_start(TokenType::StringLiteral));
}

static void test_not_statement_start_delimiters() {
    ASSERT_FALSE(error_recovery::is_statement_start(TokenType::LeftParen));
    ASSERT_FALSE(error_recovery::is_statement_start(TokenType::RightParen));
    ASSERT_FALSE(error_recovery::is_statement_start(TokenType::LeftBrace));
    ASSERT_FALSE(error_recovery::is_statement_start(TokenType::RightBrace));
}

// ─── is_closing_delimiter tests ───

static void test_is_closing_rparen() {
    ASSERT_TRUE(error_recovery::is_closing_delimiter(TokenType::RightParen));
}

static void test_is_closing_rbrace() {
    ASSERT_TRUE(error_recovery::is_closing_delimiter(TokenType::RightBrace));
}

static void test_is_closing_rbracket() {
    ASSERT_TRUE(error_recovery::is_closing_delimiter(TokenType::RightBracket));
}

static void test_not_closing_lparen() {
    ASSERT_FALSE(error_recovery::is_closing_delimiter(TokenType::LeftParen));
}

static void test_not_closing_lbrace() {
    ASSERT_FALSE(error_recovery::is_closing_delimiter(TokenType::LeftBrace));
}

static void test_not_closing_lbracket() {
    ASSERT_FALSE(error_recovery::is_closing_delimiter(TokenType::LeftBracket));
}

static void test_not_closing_eof() {
    ASSERT_FALSE(error_recovery::is_closing_delimiter(TokenType::EndOfFile));
}

static void test_not_closing_identifier() {
    ASSERT_FALSE(error_recovery::is_closing_delimiter(TokenType::Identifier));
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
    RUN(test_is_statement_start_function);
    RUN(test_is_statement_start_record);
    RUN(test_is_statement_start_choice);
    RUN(test_is_statement_start_interface);
    RUN(test_is_statement_start_namespace);
    RUN(test_is_statement_start_type);
    RUN(test_is_statement_start_include);
    RUN(test_is_statement_start_use);
    RUN(test_is_statement_start_internal);
    RUN(test_is_statement_start_annotation);
    RUN(test_is_statement_start_mutable);
    RUN(test_is_statement_start_return);
    RUN(test_is_statement_start_if);
    RUN(test_is_statement_start_for);
    RUN(test_is_statement_start_while);
    RUN(test_is_statement_start_match);
    RUN(test_is_statement_start_try);
    RUN(test_is_statement_start_task_scope);
    RUN(test_is_statement_start_spawn);
    RUN(test_is_statement_start_break);
    RUN(test_is_statement_start_continue);
    RUN(test_is_statement_start_identifier);
    RUN(test_is_statement_start_type_keywords);
    RUN(test_not_statement_start_operators);
    RUN(test_not_statement_start_literals);
    RUN(test_not_statement_start_delimiters);
    // is_closing_delimiter
    RUN(test_is_closing_rparen);
    RUN(test_is_closing_rbrace);
    RUN(test_is_closing_rbracket);
    RUN(test_not_closing_lparen);
    RUN(test_not_closing_lbrace);
    RUN(test_not_closing_lbracket);
    RUN(test_not_closing_eof);
    RUN(test_not_closing_identifier);
    return SUMMARY();
}
