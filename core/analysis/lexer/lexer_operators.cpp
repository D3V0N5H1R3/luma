#include <array>
#include <format>
#include <string_view>

#include "analysis/lexer/lexer.hpp"
#include "analysis/lexer/token_type.hpp"
#include "analysis/source/source_location.hpp"

namespace luma {

// ──────────── Operator helpers ────────────
//
// Operator scanning uses a hybrid dispatch strategy:
//
//   1. Two dispatch tables cover operators with regular structure:
//      - compound_assign_ops: char → char= (e.g. * → *=)
//      - double_or_assign_ops: char → char-char | char= (e.g. + → ++ | +=)
//
//   2. A switch statement handles operators with unique multi-character
//      lookahead logic (-, /, <, >, |, ?, .) that don't fit the tables.
//
//   3. Named helpers (emit_minus_op, emit_less_op, …) encapsulate the
//      lookahead logic for each complex operator, keeping scan_operator
//      flat and readable.

void Lexer::emit_compound_assign(TokenType single, std::string_view single_lex, TokenType compound,
                                 std::string_view compound_lex) {
    if (match('=')) {
        add_token(compound, compound_lex);
    } else {
        add_token(single, single_lex);
    }
}

void Lexer::emit_double_then_compound(char second, TokenType single, std::string_view single_lex,
                                      TokenType doubled, std::string_view doubled_lex,
                                      TokenType assign, std::string_view assign_lex) {
    if (match(second)) {
        add_token(doubled, doubled_lex);
    } else if (match('=')) {
        add_token(assign, assign_lex);
    } else {
        add_token(single, single_lex);
    }
}

bool Lexer::try_scan_compound_assign(char character) {
    // Compound-assign operators: char → char= (e.g. * → *=)
    struct CompoundAssignOp {
        char ch;
        TokenType single;
        std::string_view single_lex;
        TokenType compound;
        std::string_view compound_lex;
    };

    static constexpr std::array compound_assign_ops = {
        CompoundAssignOp{.ch = '*',
                         .single = TokenType::Star,
                         .single_lex = "*",
                         .compound = TokenType::StarEquals,
                         .compound_lex = "*="},
        CompoundAssignOp{.ch = '%',
                         .single = TokenType::Percent,
                         .single_lex = "%",
                         .compound = TokenType::PercentEquals,
                         .compound_lex = "%="},
        CompoundAssignOp{.ch = '=',
                         .single = TokenType::Equals,
                         .single_lex = "=",
                         .compound = TokenType::EqualsEquals,
                         .compound_lex = "=="},
        CompoundAssignOp{.ch = '^',
                         .single = TokenType::Caret,
                         .single_lex = "^",
                         .compound = TokenType::CaretEquals,
                         .compound_lex = "^="},
    };

    for (const auto& entry : compound_assign_ops) {
        if (character == entry.ch) {
            emit_compound_assign(entry.single, entry.single_lex, entry.compound,
                                 entry.compound_lex);
            return true;
        }
    }

    return false;
}

bool Lexer::try_scan_double_or_assign(char character) {
    // Double-or-assign operators: char → char-char | char= (e.g. + → ++ | +=)
    struct DoubleOrAssignOp {
        char ch;
        char second_char;
        TokenType single;
        std::string_view single_lex;
        TokenType doubled;
        std::string_view doubled_lex;
        TokenType assign;
        std::string_view assign_lex;
    };

    static constexpr std::array double_or_assign_ops = {
        DoubleOrAssignOp{.ch = '+',
                         .second_char = '+',
                         .single = TokenType::Plus,
                         .single_lex = "+",
                         .doubled = TokenType::PlusPlus,
                         .doubled_lex = "++",
                         .assign = TokenType::PlusEquals,
                         .assign_lex = "+="},
        DoubleOrAssignOp{.ch = '!',
                         .second_char = '>',
                         .single = TokenType::Bang,
                         .single_lex = "!",
                         .doubled = TokenType::BangGreater,
                         .doubled_lex = "!>",
                         .assign = TokenType::BangEquals,
                         .assign_lex = "!="},
        DoubleOrAssignOp{.ch = '&',
                         .second_char = '&',
                         .single = TokenType::Ampersand,
                         .single_lex = "&",
                         .doubled = TokenType::AmpersandAmpersand,
                         .doubled_lex = "&&",
                         .assign = TokenType::AmpersandEquals,
                         .assign_lex = "&="},
    };

    for (const auto& entry : double_or_assign_ops) {
        if (character == entry.ch) {
            emit_double_then_compound(entry.second_char, entry.single, entry.single_lex,
                                      entry.doubled, entry.doubled_lex, entry.assign,
                                      entry.assign_lex);
            return true;
        }
    }

    return false;
}

void Lexer::scan_operator(char character, SourceLocation location) {
    if (try_scan_compound_assign(character)) {
        return;
    }

    if (try_scan_double_or_assign(character)) {
        return;
    }

    switch (character) {
        // ── Arithmetic (complex: arrow, decrement, compound-assign) ──
        case '-':
            emit_minus_op();
            break;

        // ── Division (includes integer division //) ──
        case '/':
            if (match('/')) {
                emit_compound_assign(TokenType::SlashSlash, "//", TokenType::SlashSlashEquals,
                                     "//=");
            } else {
                emit_compound_assign(TokenType::Slash, "/", TokenType::SlashEquals, "/=");
            }
            break;

        // ── Shift and relational (complex multi-character lookahead) ──
        case '<':
            emit_less_op();
            break;

        case '>':
            emit_greater_op();
            break;

        // ── Bitwise and logical ──
        case '|':
            emit_pipe_op();
            break;

        case '~':
            add_token(TokenType::Tilde, "~");
            break;

        // ── Optional chaining and null coalescing (?., ??, ?[) ──
        case '?':
            emit_question_op();
            break;

        // ── Range (.., ..=) ──
        case '.':
            emit_dot_op();
            break;

        // ── Single-character delimiters and punctuation ──
        case '(':
            add_token(TokenType::LeftParen, "(");
            break;

        case ')':
            add_token(TokenType::RightParen, ")");
            break;

        case '{':
            if (!interpolation_state_.empty()) {
                ++interpolation_state_.back().brace_depth;
            }
            add_token(TokenType::LeftBrace, "{");
            break;

        case '}':
            add_token(TokenType::RightBrace, "}");
            break;

        case '[':
            add_token(TokenType::LeftBracket, "[");
            break;

        case ']':
            add_token(TokenType::RightBracket, "]");
            break;

        case ',':
            add_token(TokenType::Comma, ",");
            break;

        case ':':
            if (match(':')) {
                add_token(TokenType::ColonColon, "::");
            } else {
                add_token(TokenType::Colon, ":");
            }
            break;

        default:
            emit_syntax_error(std::format("unexpected character '{}'", character), location,
                              "only letters, digits, and standard operators are allowed",
                              DiagnosticCode::UnexpectedToken);
            add_token(TokenType::Error, std::string_view{&character, 1});
            break;
    }
}

void Lexer::emit_minus_op() {
    if (match('-')) {
        add_token(TokenType::MinusMinus, "--");
    } else if (match('=')) {
        add_token(TokenType::MinusEquals, "-=");
    } else if (match('>')) {
        add_token(TokenType::Arrow, "->");
    } else {
        add_token(TokenType::Minus, "-");
    }
}

void Lexer::emit_less_op() {
    if (match('<')) {
        emit_compound_assign(TokenType::LessLess, "<<", TokenType::LessLessEquals, "<<=");
    } else {
        emit_compound_assign(TokenType::Less, "<", TokenType::LessEquals, "<=");
    }
}

void Lexer::emit_greater_op() {
    if (match('>')) {
        emit_compound_assign(TokenType::GreaterGreater, ">>", TokenType::GreaterGreaterEquals,
                             ">>=");
    } else {
        emit_compound_assign(TokenType::Greater, ">", TokenType::GreaterEquals, ">=");
    }
}

void Lexer::emit_pipe_op() {
    if (match('>')) {
        add_token(TokenType::PipeGreater, "|>");
    } else {
        emit_double_then_compound('|', TokenType::Pipe, "|", TokenType::PipePipe, "||",
                                  TokenType::PipeEquals, "|=");
    }
}

void Lexer::emit_question_op() {
    if (match('.')) {
        add_token(TokenType::QuestionDot, "?.");
    } else if (match('?')) {
        add_token(TokenType::QuestionQuestion, "??");
    } else if (match('[')) {
        add_token(TokenType::QuestionBracket, "?[");
    } else {
        add_token(TokenType::QuestionMark, "?");
    }
}

void Lexer::emit_dot_op() {
    if (match('.')) {
        emit_compound_assign(TokenType::DotDot, "..", TokenType::DotDotEquals, "..=");
    } else {
        add_token(TokenType::Dot, ".");
    }
}

} // namespace luma
