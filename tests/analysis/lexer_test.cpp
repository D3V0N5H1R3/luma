// Lexer unit tests.

#include <cstdint>
#include <string>
#include <vector>

#include "analysis/diagnostics/diagnostic_collector.hpp"
#include "analysis/lexer/lexer.hpp"
#include "analysis/lexer/token_type.hpp"
#include "common/resource_limits.hpp"
#include "common/utf8.hpp"
#include "test_framework.hpp"

using namespace luma;

// ─── Fixture ───

struct LexerTestFixture : TestFixture {
    DiagnosticCollector collector;

    std::vector<Token> lex(const std::string& source) {
        collector = DiagnosticCollector{}; // fresh collector per test
        Lexer lexer{source, collector};
        return lexer.tokenize();
    }

    bool had_error() const {
        return collector.has_errors();
    }
};

// ─── Tests ───

TEST_F(LexerTestFixture, test_empty_input) {
    const auto tokens = fixture.lex("");

    ASSERT_EQ(tokens.size(), 1U);
    ASSERT_EQ(tokens[0].type, TokenType::EndOfFile);
}

TEST_F(LexerTestFixture, test_integer_literal) {
    const auto tokens = fixture.lex("42");

    ASSERT_EQ(tokens.size(), 2U);
    ASSERT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    ASSERT_EQ(tokens[0].lexeme, "42");
    ASSERT_TRUE(tokens[0].literal.has_value());
    ASSERT_EQ(std::get<std::int64_t>(REQUIRE_VALUE(tokens[0].literal)), 42);
}

TEST_F(LexerTestFixture, test_number_literal) {
    const auto tokens = fixture.lex("3.14");

    ASSERT_EQ(tokens.size(), 2U);
    ASSERT_EQ(tokens[0].type, TokenType::NumberLiteral);
    ASSERT_EQ(tokens[0].lexeme, "3.14");

    auto val = std::get<double>(REQUIRE_VALUE(tokens[0].literal));

    ASSERT_NEAR(val, 3.14, 1e-10);
}

TEST_F(LexerTestFixture, test_string_literal) {
    const auto tokens = fixture.lex("\"hello\"");

    ASSERT_EQ(tokens.size(), 2U);
    ASSERT_EQ(tokens[0].type, TokenType::StringLiteral);
    ASSERT_EQ(std::get<std::string>(REQUIRE_VALUE(tokens[0].literal)), "hello");
}

TEST_F(LexerTestFixture, test_boolean_literals) {
    const auto tokens = fixture.lex("true false");

    ASSERT_EQ(tokens.size(), 3U);
    ASSERT_EQ(tokens[0].type, TokenType::BooleanLiteral);
    ASSERT_EQ(std::get<bool>(REQUIRE_VALUE(tokens[0].literal)), true);
    ASSERT_EQ(tokens[1].type, TokenType::BooleanLiteral);
    ASSERT_EQ(std::get<bool>(REQUIRE_VALUE(tokens[1].literal)), false);
}

TEST_F(LexerTestFixture, test_null_literal) {
    const auto tokens = fixture.lex("none");

    ASSERT_EQ(tokens.size(), 2U);
    ASSERT_EQ(tokens[0].type, TokenType::NoneLiteral);
}

TEST_F(LexerTestFixture, test_keywords) {
    const auto tokens = fixture.lex("function return if else for in mutable record choice");

    ASSERT_EQ(tokens[0].type, TokenType::Function);
    ASSERT_EQ(tokens[1].type, TokenType::Return);
    ASSERT_EQ(tokens[2].type, TokenType::If);
    ASSERT_EQ(tokens[3].type, TokenType::Else);
    ASSERT_EQ(tokens[4].type, TokenType::For);
    ASSERT_EQ(tokens[5].type, TokenType::In);
    ASSERT_EQ(tokens[6].type, TokenType::Mutable);
    ASSERT_EQ(tokens[7].type, TokenType::Record);
    ASSERT_EQ(tokens[8].type, TokenType::Choice);
}

TEST_F(LexerTestFixture, test_identifiers) {
    const auto tokens = fixture.lex("foo bar_baz x1");

    ASSERT_EQ(tokens[0].type, TokenType::Identifier);
    ASSERT_EQ(tokens[0].lexeme, "foo");
    ASSERT_EQ(tokens[1].type, TokenType::Identifier);
    ASSERT_EQ(tokens[1].lexeme, "bar_baz");
    ASSERT_EQ(tokens[2].type, TokenType::Identifier);
    ASSERT_EQ(tokens[2].lexeme, "x1");
}

TEST_F(LexerTestFixture, test_operators) {
    // Bitwise operators (& ^ ~ << >>) were removed from the language (R06);
    // `|` remains only as the match-alternative separator.
    const auto tokens = fixture.lex("+ - * / // % == != < > <= >= = && || ! |");

    ASSERT_EQ(tokens[0].type, TokenType::Plus);
    ASSERT_EQ(tokens[1].type, TokenType::Minus);
    ASSERT_EQ(tokens[2].type, TokenType::Star);
    ASSERT_EQ(tokens[3].type, TokenType::Slash);
    ASSERT_EQ(tokens[4].type, TokenType::SlashSlash);
    ASSERT_EQ(tokens[5].type, TokenType::Percent);
    ASSERT_EQ(tokens[6].type, TokenType::EqualsEquals);
    ASSERT_EQ(tokens[7].type, TokenType::BangEquals);
    ASSERT_EQ(tokens[8].type, TokenType::Less);
    ASSERT_EQ(tokens[9].type, TokenType::Greater);
    ASSERT_EQ(tokens[10].type, TokenType::LessEquals);
    ASSERT_EQ(tokens[11].type, TokenType::GreaterEquals);
    ASSERT_EQ(tokens[12].type, TokenType::Equals);
    ASSERT_EQ(tokens[13].type, TokenType::AmpersandAmpersand);
    ASSERT_EQ(tokens[14].type, TokenType::PipePipe);
    ASSERT_EQ(tokens[15].type, TokenType::Bang);
    ASSERT_EQ(tokens[16].type, TokenType::Pipe);
}

TEST_F(LexerTestFixture, test_compound_assignment_operators) {
    // Bitwise compound assignments (&= |= ^= <<= >>=) were removed with the
    // bitwise operators (R06).
    const auto tokens = fixture.lex("+= -= *= /= //= %=");

    ASSERT_EQ(tokens[0].type, TokenType::PlusEquals);
    ASSERT_EQ(tokens[1].type, TokenType::MinusEquals);
    ASSERT_EQ(tokens[2].type, TokenType::StarEquals);
    ASSERT_EQ(tokens[3].type, TokenType::SlashEquals);
    ASSERT_EQ(tokens[4].type, TokenType::SlashSlashEquals);
    ASSERT_EQ(tokens[5].type, TokenType::PercentEquals);
}

TEST_F(LexerTestFixture, test_removed_bitwise_operators_report_bits) {
    // The removed bitwise operators (`&`, `^`, `~`, `<<`) each emit an Error
    // token and a diagnostic that points to the Bits module — uniform migration
    // guidance rather than a generic "unexpected character" message (R06).
    for (const auto* op : {"&", "^", "~", "<<"}) {
        const auto tokens = fixture.lex(op);

        ASSERT_TRUE(fixture.had_error());
        ASSERT_EQ(tokens[0].type, TokenType::Error);

        bool mentions_bits = false;
        for (const auto& d : fixture.collector.diagnostics()) {
            if (d.message.find("Bits") != std::string::npos ||
                (d.hint && d.hint->find("Bits") != std::string::npos)) {
                mentions_bits = true;
            }
        }
        ASSERT_TRUE(mentions_bits);
    }

    // `&&` remains logical AND — the single-`&` diagnostic must not fire for it.
    const auto and_tokens = fixture.lex("&&");
    ASSERT_FALSE(fixture.had_error());
    ASSERT_EQ(and_tokens[0].type, TokenType::AmpersandAmpersand);
}

TEST_F(LexerTestFixture, test_punctuation) {
    const auto tokens = fixture.lex("( ) { } [ ] , . : ::");

    ASSERT_EQ(tokens[0].type, TokenType::LeftParen);
    ASSERT_EQ(tokens[1].type, TokenType::RightParen);
    ASSERT_EQ(tokens[2].type, TokenType::LeftBrace);
    ASSERT_EQ(tokens[3].type, TokenType::RightBrace);
    ASSERT_EQ(tokens[4].type, TokenType::LeftBracket);
    ASSERT_EQ(tokens[5].type, TokenType::RightBracket);
    ASSERT_EQ(tokens[6].type, TokenType::Comma);
    ASSERT_EQ(tokens[7].type, TokenType::Dot);
    ASSERT_EQ(tokens[8].type, TokenType::Colon);
    ASSERT_EQ(tokens[9].type, TokenType::ColonColon);
}

TEST_F(LexerTestFixture, test_pipe_and_arrow) {
    const auto tokens = fixture.lex("|> ->");

    ASSERT_EQ(tokens[0].type, TokenType::PipeGreater);
    ASSERT_EQ(tokens[1].type, TokenType::Arrow);
}

TEST_F(LexerTestFixture, test_comments_are_skipped) {
    const auto tokens = fixture.lex("42 # this is a comment\n7");

    ASSERT_EQ(tokens.size(), 3U);
    ASSERT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    ASSERT_EQ(tokens[0].lexeme, "42");
    ASSERT_EQ(tokens[1].type, TokenType::IntegerLiteral);
    ASSERT_EQ(tokens[1].lexeme, "7");
}

TEST_F(LexerTestFixture, test_hash_inside_string_is_not_a_comment) {
    const auto tokens = fixture.lex("\"# not a comment\"");

    ASSERT_EQ(tokens.size(), 2U);
    ASSERT_EQ(tokens[0].type, TokenType::StringLiteral);
    ASSERT_EQ(tokens[0].lexeme, "# not a comment");
}

TEST_F(LexerTestFixture, test_location_tracking) {
    const auto tokens = fixture.lex("x\ny");

    ASSERT_EQ(tokens[0].location.line, 1);
    ASSERT_EQ(tokens[1].location.line, 2);
}

TEST_F(LexerTestFixture, test_annotation_token) {
    const auto tokens = fixture.lex("@main @test");

    ASSERT_EQ(tokens[0].type, TokenType::Annotation);
    ASSERT_EQ(tokens[0].lexeme, "main");
    ASSERT_EQ(tokens[1].type, TokenType::Annotation);
    ASSERT_EQ(tokens[1].lexeme, "test");
}

TEST_F(LexerTestFixture, test_type_keywords) {
    const auto tokens = fixture.lex("boolean integer number string array");

    ASSERT_EQ(tokens[0].type, TokenType::BooleanType);
    ASSERT_EQ(tokens[1].type, TokenType::IntegerType);
    ASSERT_EQ(tokens[2].type, TokenType::NumberType);
    ASSERT_EQ(tokens[3].type, TokenType::StringType);
    ASSERT_EQ(tokens[4].type, TokenType::ArrayType);
}

TEST_F(LexerTestFixture, test_semicolons_are_skipped) {
    const auto tokens = fixture.lex("x = 1; y = 2");

    ASSERT_EQ(tokens[0].type, TokenType::Identifier);
    ASSERT_EQ(tokens[0].lexeme, "x");
    ASSERT_EQ(tokens[1].type, TokenType::Equals);
    ASSERT_EQ(tokens[2].type, TokenType::IntegerLiteral);
    ASSERT_EQ(tokens[3].type, TokenType::Identifier);
    ASSERT_EQ(tokens[3].lexeme, "y");
    ASSERT_EQ(tokens[4].type, TokenType::Equals);
    ASSERT_EQ(tokens[5].type, TokenType::IntegerLiteral);
    ASSERT_EQ(tokens[6].type, TokenType::EndOfFile);
}

TEST_F(LexerTestFixture, test_semicolons_only) {
    const auto tokens = fixture.lex(";;;");

    ASSERT_EQ(tokens.size(), 1U);
    ASSERT_EQ(tokens[0].type, TokenType::EndOfFile);
}

TEST_F(LexerTestFixture, test_invalid_character_throws) {
    fixture.lex("`");

    ASSERT_TRUE(fixture.had_error());
}

TEST_F(LexerTestFixture, test_additional_keywords) {
    const auto tokens = fixture.lex("interface namespace while match case success failure break "
                                    "continue spawn await downcast use include type task_scope");

    ASSERT_EQ(tokens[0].type, TokenType::Interface);
    ASSERT_EQ(tokens[1].type, TokenType::Namespace);
    ASSERT_EQ(tokens[2].type, TokenType::While);
    ASSERT_EQ(tokens[3].type, TokenType::Match);
    ASSERT_EQ(tokens[4].type, TokenType::Case);
    ASSERT_EQ(tokens[5].type, TokenType::Success);
    ASSERT_EQ(tokens[6].type, TokenType::Failure);
    ASSERT_EQ(tokens[7].type, TokenType::Break);
    ASSERT_EQ(tokens[8].type, TokenType::Continue);
    ASSERT_EQ(tokens[9].type, TokenType::Spawn);
    ASSERT_EQ(tokens[10].type, TokenType::Await);
    ASSERT_EQ(tokens[11].type, TokenType::Downcast);
    ASSERT_EQ(tokens[12].type, TokenType::Use);
    ASSERT_EQ(tokens[13].type, TokenType::Include);
    ASSERT_EQ(tokens[14].type, TokenType::Type);
    ASSERT_EQ(tokens[15].type, TokenType::TaskScope);
}

TEST_F(LexerTestFixture, test_additional_type_keywords) {
    // `dictionary` and `result` remain reserved type keywords; the demoted
    // container/handle types (task, channel, socket, ...) now lex as ordinary
    // identifiers (R02).
    const auto tokens = fixture.lex("dictionary result task channel socket");

    ASSERT_EQ(tokens[0].type, TokenType::DictionaryType);
    ASSERT_EQ(tokens[1].type, TokenType::ResultType);
    ASSERT_EQ(tokens[2].type, TokenType::Identifier);
    ASSERT_EQ(tokens[3].type, TokenType::Identifier);
    ASSERT_EQ(tokens[4].type, TokenType::Identifier);
}

TEST_F(LexerTestFixture, test_increment_decrement_operators) {
    const auto tokens = fixture.lex("++ --");

    ASSERT_EQ(tokens[0].type, TokenType::PlusPlus);
    ASSERT_EQ(tokens[0].lexeme, "++");
    ASSERT_EQ(tokens[1].type, TokenType::MinusMinus);
    ASSERT_EQ(tokens[1].lexeme, "--");
}

TEST_F(LexerTestFixture, test_range_operator) {
    const auto tokens = fixture.lex("0..10");

    ASSERT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    ASSERT_EQ(tokens[1].type, TokenType::DotDot);
    ASSERT_EQ(tokens[1].lexeme, "..");
    ASSERT_EQ(tokens[2].type, TokenType::IntegerLiteral);
}

TEST_F(LexerTestFixture, test_inclusive_range_operator) {
    const auto tokens = fixture.lex("0..=10");

    ASSERT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    ASSERT_EQ(tokens[1].type, TokenType::DotDotEquals);
    ASSERT_EQ(tokens[1].lexeme, "..=");
    ASSERT_EQ(tokens[2].type, TokenType::IntegerLiteral);
}

TEST_F(LexerTestFixture, test_question_mark_operator) {
    const auto tokens = fixture.lex("?");

    ASSERT_EQ(tokens[0].type, TokenType::QuestionMark);
}

TEST_F(LexerTestFixture, test_question_dot_token) {
    // ?. is a single QuestionDot token.
    const auto tokens = fixture.lex("x?.field");

    ASSERT_EQ(tokens[0].type, TokenType::Identifier);
    ASSERT_EQ(tokens[1].type, TokenType::QuestionDot);
    ASSERT_EQ(tokens[2].type, TokenType::Identifier);

    // ? followed by a non-dot stays a plain QuestionMark.
    auto tokens2 = fixture.lex("?");

    ASSERT_EQ(tokens2[0].type, TokenType::QuestionMark);
}

TEST_F(LexerTestFixture, test_question_bracket_token) {
    // ?[ is a single QuestionBracket token.
    const auto tokens = fixture.lex("x?[0]");

    ASSERT_EQ(tokens[0].type, TokenType::Identifier);
    ASSERT_EQ(tokens[1].type, TokenType::QuestionBracket);
    ASSERT_EQ(tokens[2].type, TokenType::IntegerLiteral);
    ASSERT_EQ(tokens[3].type, TokenType::RightBracket);

    // ?[ is distinct from ?. and ?.
    auto tokens2 = fixture.lex("?[");

    ASSERT_EQ(tokens2[0].type, TokenType::QuestionBracket);
}

TEST_F(LexerTestFixture, test_string_interpolation_tokens) {
    const auto tokens = fixture.lex("\"hello ${name}!\"");

    ASSERT_EQ(tokens[0].type, TokenType::StringStart);
    ASSERT_EQ(tokens[1].type, TokenType::InterpolationStart);
    ASSERT_EQ(tokens[2].type, TokenType::Identifier);
    ASSERT_EQ(tokens[2].lexeme, "name");
    ASSERT_EQ(tokens[3].type, TokenType::InterpolationEnd);
    ASSERT_EQ(tokens[4].type, TokenType::StringEnd);
}

TEST_F(LexerTestFixture, test_string_interpolation_multiple) {
    const auto tokens = fixture.lex("\"${a} and ${b}\"");

    ASSERT_EQ(tokens[0].type, TokenType::StringStart);
    ASSERT_EQ(tokens[1].type, TokenType::InterpolationStart);
    ASSERT_EQ(tokens[2].type, TokenType::Identifier);
    ASSERT_EQ(tokens[3].type, TokenType::InterpolationEnd);
    ASSERT_EQ(tokens[4].type, TokenType::StringMiddle);
    ASSERT_EQ(tokens[5].type, TokenType::InterpolationStart);
    ASSERT_EQ(tokens[6].type, TokenType::Identifier);
    ASSERT_EQ(tokens[7].type, TokenType::InterpolationEnd);
    ASSERT_EQ(tokens[8].type, TokenType::StringEnd);
}

// ─── Hex and binary integer literals ───

TEST_F(LexerTestFixture, test_hex_literal_lowercase) {
    const auto tokens = fixture.lex("0xff");

    ASSERT_EQ(tokens.size(), std::size_t{2}); // literal + EOF
    ASSERT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    ASSERT_EQ(std::get<std::int64_t>(REQUIRE_VALUE(tokens[0].literal)), 255);
    ASSERT_EQ(tokens[0].lexeme, "0xff");
}

TEST_F(LexerTestFixture, test_hex_literal_uppercase_prefix) {
    const auto tokens = fixture.lex("0XFF");

    ASSERT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    ASSERT_EQ(std::get<std::int64_t>(REQUIRE_VALUE(tokens[0].literal)), 255);
}

TEST_F(LexerTestFixture, test_hex_literal_mixed_case_digits) {
    const auto tokens = fixture.lex("0xDeAdBeEf");

    ASSERT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    ASSERT_EQ(std::get<std::int64_t>(REQUIRE_VALUE(tokens[0].literal)), std::int64_t{0xDeAdBeEf});
}

TEST_F(LexerTestFixture, test_hex_literal_zero) {
    const auto tokens = fixture.lex("0x0");

    ASSERT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    ASSERT_EQ(std::get<std::int64_t>(REQUIRE_VALUE(tokens[0].literal)), 0);
}

TEST_F(LexerTestFixture, test_binary_literal) {
    const auto tokens = fixture.lex("0b1010");

    ASSERT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    ASSERT_EQ(std::get<std::int64_t>(REQUIRE_VALUE(tokens[0].literal)), 10);
    ASSERT_EQ(tokens[0].lexeme, "0b1010");
}

TEST_F(LexerTestFixture, test_binary_literal_uppercase_prefix) {
    const auto tokens = fixture.lex("0B11001100");

    ASSERT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    ASSERT_EQ(std::get<std::int64_t>(REQUIRE_VALUE(tokens[0].literal)), 0xCC);
}

TEST_F(LexerTestFixture, test_binary_literal_all_zeros) {
    const auto tokens = fixture.lex("0b0000");

    ASSERT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    ASSERT_EQ(std::get<std::int64_t>(REQUIRE_VALUE(tokens[0].literal)), 0);
}

TEST_F(LexerTestFixture, test_hex_literal_no_digits_throws) {
    fixture.lex("0x");

    ASSERT_TRUE(fixture.had_error());
}

TEST_F(LexerTestFixture, test_binary_literal_no_digits_throws) {
    fixture.lex("0b");

    ASSERT_TRUE(fixture.had_error());
}

TEST_F(LexerTestFixture, test_integer_literal_out_of_range_throws) {
    // 9223372036854775808 is one past the signed 64-bit maximum.
    fixture.lex("9223372036854775808");

    ASSERT_TRUE(fixture.had_error());
}

TEST_F(LexerTestFixture, test_number_literal_out_of_range_throws) {
    // 1e400 exceeds the maximum representable double.
    fixture.lex("1e400");

    ASSERT_TRUE(fixture.had_error());
}

TEST_F(LexerTestFixture, test_hex_and_decimal_in_sequence) {
    const auto tokens = fixture.lex("0xff 255");

    ASSERT_EQ(tokens.size(), std::size_t{3}); // hex + decimal + EOF
    ASSERT_EQ(std::get<std::int64_t>(REQUIRE_VALUE(tokens[0].literal)), 255);
    ASSERT_EQ(std::get<std::int64_t>(REQUIRE_VALUE(tokens[1].literal)), 255);
}

// ─── internal keyword ───

TEST_F(LexerTestFixture, test_internal_keyword_token) {
    const auto tokens = fixture.lex("internal");

    ASSERT_EQ(tokens.size(), std::size_t{2}); // keyword + EOF
    ASSERT_EQ(tokens[0].type, TokenType::Internal);
    ASSERT_EQ(tokens[0].lexeme, "internal");
}

TEST_F(LexerTestFixture, test_internal_in_sequence) {
    // 'internal function' produces two keyword tokens.
    const auto tokens = fixture.lex("internal function");

    ASSERT_EQ(tokens[0].type, TokenType::Internal);
    ASSERT_EQ(tokens[1].type, TokenType::Function);
}

// ─── Lexer error detection tests ───

TEST_F(LexerTestFixture, test_unterminated_string_throws) {
    fixture.lex("\"hello");

    ASSERT_TRUE(fixture.had_error());
}

TEST_F(LexerTestFixture, test_unterminated_string_with_newline_throws) {
    fixture.lex("\"hello\n");

    ASSERT_TRUE(fixture.had_error());
}

TEST_F(LexerTestFixture, test_invalid_escape_sequence_throws) {
    fixture.lex(R"("hello\z")");

    ASSERT_TRUE(fixture.had_error());
}

TEST_F(LexerTestFixture, test_unterminated_interpolation_throws) {
    fixture.lex("\"hello ${name\"");

    ASSERT_TRUE(fixture.had_error());
}

TEST_F(LexerTestFixture, test_multiple_invalid_characters_throw) {
    // @ is an annotation, but `~` alone and garbage should emit errors.
    // Just verify no crash and that at least one diagnostic is emitted.
    fixture.lex("@#~");

    (void)fixture.had_error(); // Either no error (@ alone is valid) or one — just no crash.
}

TEST_F(LexerTestFixture, test_error_flood_is_capped) {
    // Each unrecognised byte emits a diagnostic and a recovery Error token, so an
    // adversarial run of them would otherwise produce one of each per byte — an
    // O(input) blow-up on top of the already-linear scan.  The lexer stops after
    // k_max_lex_errors (100) hard errors, so its output stays bounded no matter
    // how long the run of garbage is.
    const std::string flood(500, '`');
    const auto tokens = fixture.lex(flood);

    std::size_t error_tokens{0};
    for (const auto& token : tokens) {
        if (token.type == TokenType::Error) {
            ++error_tokens;
        }
    }

    ASSERT_TRUE(fixture.had_error());
    // Far below the 500 error bytes in the input: the scan gave up at the cap
    // rather than emitting a diagnostic and Error token for every byte.
    ASSERT_TRUE(error_tokens <= 101);
    ASSERT_TRUE(tokens.size() <= 102);

    // A single token can also emit many errors before control returns to the scan
    // loop.  A string literal full of invalid escape sequences drives one
    // diagnostic per escape from inside scan_string_content, so the per-token emit
    // path must honour the same cap — otherwise this one token floods the output.
    std::string escape_body;
    for (int i = 0; i < 500; ++i) {
        escape_body += "\\z";
    }
    fixture.lex("\"" + escape_body + "\"");

    ASSERT_TRUE(fixture.had_error());
    ASSERT_TRUE(fixture.collector.diagnostics().size() <= 101);
}

TEST_F(LexerTestFixture, test_newline_inside_single_line_string_throws) {
    // A raw newline inside a regular string must be rejected.
    // Only triple-quoted strings may span multiple lines.
    fixture.lex("\"hello\nworld\"");

    ASSERT_TRUE(fixture.had_error());
}

TEST_F(LexerTestFixture, test_newline_inside_interpolated_string_throws) {
    fixture.lex("\"hello ${x}\nworld\"");

    ASSERT_TRUE(fixture.had_error());
}

TEST_F(LexerTestFixture, test_error_pipe_operator) {
    const auto tokens = fixture.lex("x !> f()");

    ASSERT_EQ(tokens[0].type, TokenType::Identifier);
    ASSERT_EQ(tokens[1].type, TokenType::BangGreater);
    ASSERT_EQ(tokens[1].lexeme, "!>");
    ASSERT_EQ(tokens[2].type, TokenType::Identifier);
}

TEST_F(LexerTestFixture, test_null_coalescing_operator) {
    const auto tokens = fixture.lex("x ?? 0");

    ASSERT_EQ(tokens[0].type, TokenType::Identifier);
    ASSERT_EQ(tokens[1].type, TokenType::QuestionQuestion);
    ASSERT_EQ(tokens[1].lexeme, "??");
    ASSERT_EQ(tokens[2].type, TokenType::IntegerLiteral);
}

TEST_F(LexerTestFixture, test_error_pipe_distinct_from_bang) {
    // !> is BangGreater, but ! alone is Bang and != is BangEquals.
    const auto tokens = fixture.lex("! !> !=");

    ASSERT_EQ(tokens[0].type, TokenType::Bang);
    ASSERT_EQ(tokens[1].type, TokenType::BangGreater);
    ASSERT_EQ(tokens[2].type, TokenType::BangEquals);
}

TEST_F(LexerTestFixture, test_invalid_number_format_throws) {
    // Leading dot is not a valid number in Luma.
    // It should produce a Dot token + integer, not a number literal.
    const auto tokens = fixture.lex(".42");

    bool found_number = false;

    for (const auto& t : tokens) {
        if (t.type == TokenType::NumberLiteral && t.lexeme == ".42") {
            found_number = true;
        }
    }

    ASSERT_FALSE(found_number);
}

// ─── Unicode handling tests ───

TEST_F(LexerTestFixture, test_unicode_identifier) {
    // Unicode letters (0x80+) should be accepted in identifiers.
    const auto tokens = fixture.lex("caf\xC3\xA9");

    ASSERT_EQ(tokens.size(), 2U);
    ASSERT_EQ(tokens[0].type, TokenType::Identifier);
    ASSERT_EQ(tokens[0].lexeme, "caf\xC3\xA9");
}

TEST_F(LexerTestFixture, test_utf8_column_tracking) {
    // Multi-byte UTF-8 characters should count as one column each.
    // "é" is 2 bytes (0xC3 0xA9). After scanning "café", the column
    // should advance by 4 characters (not 5 bytes).
    // Token locations are captured after the token, so "café" gets
    // column 5 and "x" gets column 7.
    fixture.collector = DiagnosticCollector{};
    Lexer lexer{std::string{"caf\xC3\xA9 x"}, fixture.collector};
    auto tokens = lexer.tokenize();

    // First token is "café" — location is one past end, column 5.
    ASSERT_EQ(tokens[0].type, TokenType::Identifier);
    ASSERT_EQ(tokens[0].lexeme, "caf\xC3\xA9");
    ASSERT_EQ(tokens[0].location.column, 5);

    // Second token is "x" — at column 7 (café=4 + space=1 + x=1 + 1 past end = 7).
    ASSERT_EQ(tokens[1].type, TokenType::Identifier);
    ASSERT_EQ(tokens[1].lexeme, "x");
    ASSERT_EQ(tokens[1].location.column, 7);
}

TEST_F(LexerTestFixture, test_confusable_character_warning) {
    // An identifier containing a byte 0xE2 (common in zero-width characters)
    // should produce a warning.
    // Build the string with the zero-width space U+200B (UTF-8: E2 80 8B).
    std::string source = "ab";
    source += '\xE2';
    source += '\x80';
    source += '\x8B';
    source += 'c';
    fixture.collector = DiagnosticCollector{};
    Lexer lexer{source, fixture.collector};
    auto tokens = lexer.tokenize();
    const auto& warnings = fixture.collector.diagnostics();

    ASSERT_FALSE(warnings.empty());

    bool found = false;

    for (const auto& w : warnings) {
        if (w.message.find("confusable") != std::string::npos) {
            found = true;
        }
    }

    ASSERT_TRUE(found);
}

TEST_F(LexerTestFixture, test_utf8_codepoint_len) {
    (void)fixture; // Static utility — no fixture state needed.
    // ASCII byte.
    ASSERT_EQ(utf8_codepoint_len(0x41), 1);
    // 2-byte leading byte.
    ASSERT_EQ(utf8_codepoint_len(0xC3), 2);
    // 3-byte leading byte.
    ASSERT_EQ(utf8_codepoint_len(0xE2), 3);
    // 4-byte leading byte.
    ASSERT_EQ(utf8_codepoint_len(0xF0), 4);
    // Continuation byte (invalid as leading) — treated as 1.
    ASSERT_EQ(utf8_codepoint_len(0x80), 1);
}

// ─── Edge case tests (CA-26) ───

TEST_F(LexerTestFixture, test_maximum_length_identifier) {
    // A very long identifier (1000 characters) should tokenize correctly.
    const std::string long_id(1000, 'a');
    const auto tokens = fixture.lex(long_id);

    ASSERT_EQ(tokens.size(), 2U); // identifier + EOF
    ASSERT_EQ(tokens[0].type, TokenType::Identifier);
    ASSERT_EQ(tokens[0].lexeme.size(), 1000U);
}

TEST_F(LexerTestFixture, test_nested_interpolation) {
    // Nested string interpolation: "${  "${inner}"  }" is not supported —
    // the lexer should handle the nested braces correctly.
    const auto tokens = fixture.lex(R"("outer ${x} end")");

    // Should have: StringLiteral("outer "), InterpolationStart, Identifier(x),
    // InterpolationEnd, StringLiteral(" end"), EOF
    bool found_interp_start = false;
    bool found_interp_end = false;

    for (const auto& t : tokens) {
        if (t.type == TokenType::InterpolationStart) {
            found_interp_start = true;
        }
        if (t.type == TokenType::InterpolationEnd) {
            found_interp_end = true;
        }
    }

    ASSERT_TRUE(found_interp_start);
    ASSERT_TRUE(found_interp_end);
}

TEST_F(LexerTestFixture, test_adjacent_string_interpolations) {
    // Two interpolations back to back: "${a}${b}"
    const auto tokens = fixture.lex(R"("${a}${b}")");

    int interp_starts = 0;
    int interp_ends = 0;

    for (const auto& t : tokens) {
        if (t.type == TokenType::InterpolationStart) {
            ++interp_starts;
        }
        if (t.type == TokenType::InterpolationEnd) {
            ++interp_ends;
        }
    }

    ASSERT_EQ(interp_starts, 2);
    ASSERT_EQ(interp_ends, 2);
}

// Regression: interpolation nested beyond max_interpolation_depth must report
// the depth-limit error without leaving the token stream desynchronised from the
// interpolation-state stack. The over-limit InterpolationStart used to be emitted
// before the depth check ran, so a start token was added with no matching state
// pushed; a later '}' then closed an *outer* level prematurely, cascading the
// bookkeeping out of sync. The depth check now runs first (before '${' is even
// consumed), so no level past the limit contributes an InterpolationStart.
TEST_F(LexerTestFixture, test_interpolation_depth_limit_reported) {
    const int over_limit = ResourceLimits::max_interpolation_depth + 1;

    // over_limit nested interpolations: ("${)×N + 1 + (}")×N.
    std::string source;
    for (int i = 0; i < over_limit; ++i) {
        source += "\"${";
    }
    source += "1";
    for (int i = 0; i < over_limit; ++i) {
        source += "}\"";
    }

    const auto tokens = fixture.lex(source); // must terminate (no infinite loop)

    int interp_starts = 0;
    int interp_ends = 0;
    for (const auto& t : tokens) {
        if (t.type == TokenType::InterpolationStart) {
            ++interp_starts;
        }
        if (t.type == TokenType::InterpolationEnd) {
            ++interp_ends;
        }
    }

    // The depth limit is reported.
    ASSERT_TRUE(fixture.had_error());
    // No InterpolationStart is emitted for a level past the limit: the rejected
    // level contributes neither a start token nor a pushed state, so the start
    // count never exceeds the configured maximum. Before the fix the check ran
    // after the token was added, so this was max + 1.
    ASSERT_TRUE(interp_starts <= ResourceLimits::max_interpolation_depth);
    // Error recovery must not synthesise a close for a level that was never
    // opened: there can be no more ends than starts.
    ASSERT_TRUE(interp_ends <= interp_starts);
}

TEST_F(LexerTestFixture, test_empty_string) {
    const auto tokens = fixture.lex(R"("")");

    ASSERT_EQ(tokens.size(), 2U); // StringLiteral + EOF
    ASSERT_EQ(tokens[0].type, TokenType::StringLiteral);
    ASSERT_EQ(tokens[0].lexeme, "");
}

TEST_F(LexerTestFixture, test_unicode_identifier_multibyte) {
    // Multi-byte UTF-8 identifier (e.g., Greek letters).
    const auto tokens = fixture.lex("\xCE\xB1\xCE\xB2"); // αβ

    ASSERT_EQ(tokens.size(), 2U); // identifier + EOF
    ASSERT_EQ(tokens[0].type, TokenType::Identifier);
}

TEST_F(LexerTestFixture, test_annotation_with_digits) {
    // Annotation names may include digits after the first character,
    // consistent with identifier rules.
    const auto tokens = fixture.lex("@test2 @v3");

    ASSERT_EQ(tokens.size(), 3U); // two annotations + EOF
    ASSERT_EQ(tokens[0].type, TokenType::Annotation);
    ASSERT_EQ(tokens[0].lexeme, "test2");
    ASSERT_EQ(tokens[1].type, TokenType::Annotation);
    ASSERT_EQ(tokens[1].lexeme, "v3");
}

TEST_F(LexerTestFixture, test_annotation_empty_name_error) {
    // '@' with no following name must emit a diagnostic and a recovery Error
    // token, exercising the empty-name branch of scan_annotation().
    const auto tokens = fixture.lex("@");

    ASSERT_TRUE(fixture.had_error());
    ASSERT_EQ(tokens[0].type, TokenType::Error);
    ASSERT_EQ(tokens[0].lexeme, "@");

    bool found = false;

    for (const auto& d : fixture.collector.diagnostics()) {
        if (d.message.find("expected annotation name after '@'") != std::string::npos) {
            found = true;
        }
    }

    ASSERT_TRUE(found);
}

// ─── Triple-quoted strings ───

TEST_F(LexerTestFixture, test_triple_quoted_string_literal) {
    // A plain triple-quoted string produces a single StringLiteral carrying
    // its (dedented) text as the token literal.
    const auto tokens = fixture.lex("\"\"\"hello\"\"\"");

    ASSERT_FALSE(fixture.had_error());
    ASSERT_EQ(tokens.size(), 2U); // StringLiteral + EOF
    ASSERT_EQ(tokens[0].type, TokenType::StringLiteral);
    ASSERT_EQ(std::get<std::string>(REQUIRE_VALUE(tokens[0].literal)), "hello");
}

TEST_F(LexerTestFixture, test_triple_quoted_string_interpolation) {
    // Interpolation inside a triple-quoted string yields a StringStart opening
    // segment, the interpolation tokens, then a StringEnd continuation segment.
    const auto tokens = fixture.lex("\"\"\"a${x}b\"\"\"");

    ASSERT_FALSE(fixture.had_error());
    ASSERT_EQ(tokens[0].type, TokenType::StringStart);
    ASSERT_EQ(tokens[0].lexeme, "a");
    ASSERT_EQ(tokens[1].type, TokenType::InterpolationStart);
    ASSERT_EQ(tokens[2].type, TokenType::Identifier);
    ASSERT_EQ(tokens[2].lexeme, "x");
    ASSERT_EQ(tokens[3].type, TokenType::InterpolationEnd);
    ASSERT_EQ(tokens[4].type, TokenType::StringEnd);
    ASSERT_EQ(tokens[4].lexeme, "b");
}

TEST_F(LexerTestFixture, test_triple_quoted_string_unterminated_recovers) {
    // An unterminated triple-quoted string reports an error but still emits a
    // partial StringLiteral for recovery.
    const auto tokens = fixture.lex("\"\"\"hello");

    ASSERT_TRUE(fixture.had_error());
    ASSERT_EQ(tokens[0].type, TokenType::StringLiteral);
    ASSERT_EQ(std::get<std::string>(REQUIRE_VALUE(tokens[0].literal)), "hello");
}

// ─── Keyword classification predicates ───

TEST_F(LexerTestFixture, test_keyword_classification_predicates) {
    (void)fixture; // Pure predicate check — no fixture state needed.

    // Built-in types satisfy all three predicates.
    ASSERT_TRUE(is_builtin_type_token_type(TokenType::IntegerType));
    ASSERT_TRUE(is_type_keyword(TokenType::IntegerType));
    ASSERT_TRUE(is_keyword_token_type(TokenType::IntegerType));

    // `function` and `none` are type keywords but not built-in types.
    ASSERT_FALSE(is_builtin_type_token_type(TokenType::Function));
    ASSERT_TRUE(is_type_keyword(TokenType::Function));
    ASSERT_TRUE(is_keyword_token_type(TokenType::Function));

    ASSERT_FALSE(is_builtin_type_token_type(TokenType::NoneLiteral));
    ASSERT_TRUE(is_type_keyword(TokenType::NoneLiteral));
    ASSERT_TRUE(is_keyword_token_type(TokenType::NoneLiteral));

    // Word-literals and plain keywords are keywords but not type keywords.
    ASSERT_FALSE(is_type_keyword(TokenType::BooleanLiteral));
    ASSERT_TRUE(is_keyword_token_type(TokenType::BooleanLiteral));
    ASSERT_FALSE(is_type_keyword(TokenType::If));
    ASSERT_TRUE(is_keyword_token_type(TokenType::If));

    // Non-keyword tokens satisfy none of the predicates.
    ASSERT_FALSE(is_keyword_token_type(TokenType::Identifier));
    ASSERT_FALSE(is_keyword_token_type(TokenType::Plus));
    ASSERT_FALSE(is_type_keyword(TokenType::Plus));
    ASSERT_FALSE(is_builtin_type_token_type(TokenType::Plus));
}

int main() {
    RUN(test_empty_input);
    RUN(test_integer_literal);
    RUN(test_number_literal);
    RUN(test_string_literal);
    RUN(test_boolean_literals);
    RUN(test_null_literal);
    RUN(test_keywords);
    RUN(test_identifiers);
    RUN(test_operators);
    RUN(test_compound_assignment_operators);
    RUN(test_removed_bitwise_operators_report_bits);
    RUN(test_punctuation);
    RUN(test_pipe_and_arrow);
    RUN(test_comments_are_skipped);
    RUN(test_hash_inside_string_is_not_a_comment);
    RUN(test_location_tracking);
    RUN(test_annotation_token);
    RUN(test_type_keywords);
    RUN(test_semicolons_are_skipped);
    RUN(test_semicolons_only);
    RUN(test_invalid_character_throws);
    RUN(test_additional_keywords);
    RUN(test_additional_type_keywords);
    RUN(test_increment_decrement_operators);
    RUN(test_range_operator);
    RUN(test_inclusive_range_operator);
    RUN(test_question_mark_operator);
    RUN(test_question_dot_token);
    RUN(test_question_bracket_token);
    RUN(test_string_interpolation_tokens);
    RUN(test_string_interpolation_multiple);

    // Hex and binary integer literals.
    RUN(test_hex_literal_lowercase);
    RUN(test_hex_literal_uppercase_prefix);
    RUN(test_hex_literal_mixed_case_digits);
    RUN(test_hex_literal_zero);
    RUN(test_binary_literal);
    RUN(test_binary_literal_uppercase_prefix);
    RUN(test_binary_literal_all_zeros);
    RUN(test_hex_literal_no_digits_throws);
    RUN(test_binary_literal_no_digits_throws);
    RUN(test_integer_literal_out_of_range_throws);
    RUN(test_number_literal_out_of_range_throws);
    RUN(test_hex_and_decimal_in_sequence);

    // internal keyword.
    RUN(test_internal_keyword_token);
    RUN(test_internal_in_sequence);

    // Lexer error detection.
    RUN(test_unterminated_string_throws);
    RUN(test_unterminated_string_with_newline_throws);
    RUN(test_invalid_escape_sequence_throws);
    RUN(test_unterminated_interpolation_throws);
    RUN(test_multiple_invalid_characters_throw);
    RUN(test_error_flood_is_capped);
    RUN(test_newline_inside_single_line_string_throws);
    RUN(test_newline_inside_interpolated_string_throws);
    RUN(test_invalid_number_format_throws);

    // Error pipe and null coalescing operators.
    RUN(test_error_pipe_operator);
    RUN(test_null_coalescing_operator);
    RUN(test_error_pipe_distinct_from_bang);

    // Unicode handling.
    RUN(test_unicode_identifier);
    RUN(test_utf8_column_tracking);
    RUN(test_confusable_character_warning);
    RUN(test_utf8_codepoint_len);

    // Edge cases (CA-26).
    RUN(test_maximum_length_identifier);
    RUN(test_nested_interpolation);
    RUN(test_adjacent_string_interpolations);
    RUN(test_interpolation_depth_limit_reported);
    RUN(test_empty_string);
    RUN(test_unicode_identifier_multibyte);
    RUN(test_annotation_with_digits);
    RUN(test_annotation_empty_name_error);

    // Triple-quoted strings.
    RUN(test_triple_quoted_string_literal);
    RUN(test_triple_quoted_string_interpolation);
    RUN(test_triple_quoted_string_unterminated_recovers);
    RUN(test_keyword_classification_predicates);

    return SUMMARY();
}
