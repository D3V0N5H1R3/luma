// parser_expr.cpp — Expression parsing methods for the Parser class.
//
// Sections:
//   1. Top-level expression (with, record-with)
//   2. Pipe expressions (|>, !>)
//   3. Binary expressions (precedence climbing: or → and → equality →
//      comparison → bitwise → shift → addition → multiplication)
//   4. Unary expressions (prefix operators)
//   5. Postfix expressions (field access, index, call, range, propagation)
//   6. Call expressions (regular, turbofish, argument lists)
//   7. Primary expressions (literals, identifiers, record creation)
//   8. String interpolation
//   9. Special forms (if-expression, match-expression)
//  10. Collection literals (array, dictionary, tuple)
//  11. Lambda expressions
//  12. Generic record creation lookahead
//  13. Record creation

#include <format>

#include "analysis/lexer/token_type.hpp"
#include "analysis/parser/operator_matcher.hpp"
#include "analysis/parser/parser.hpp"
#include "common/string_utils.hpp"

namespace luma {

// ═══════════════════════════════════════════════════════════════════════════════
// Top-level expression and record-with
// ═══════════════════════════════════════════════════════════════════════════════

ExpressionPtr Parser::parse_expression() {
    auto guard = make_recursion_guard(current().location);

    auto expr = parse_pipe();

    if (check(TokenType::With)) {
        const auto location = current().location;

        advance(); // consume 'with'
        expect(TokenType::LeftBrace);

        std::vector<RecordFieldInit> overrides;

        parse_delimited_comma_list(TokenType::RightBrace, [&] {
            RecordFieldInit field;
            field.name = expect(TokenType::Identifier).lexeme;

            expect(TokenType::Equals);

            field.value = parse_expression();

            overrides.push_back(std::move(field));
        });

        recover_expect(TokenType::RightBrace);

        expr =
            std::make_unique<RecordWithExpression>(location, std::move(expr), std::move(overrides));
    }

    return expr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Pipe expressions
// ═══════════════════════════════════════════════════════════════════════════════

ExpressionPtr Parser::parse_pipe() {
    auto left = parse_null_coalescing();

    std::size_t chain_length = 0;
    while (check(TokenType::PipeGreater) || check(TokenType::BangGreater)) {
        const auto location = current().location;
        const bool is_error_pipe = check(TokenType::BangGreater);

        advance();

        auto right = parse_null_coalescing();

        if (is_error_pipe) {
            left =
                std::make_unique<ErrorPipeExpression>(location, std::move(left), std::move(right));
        } else {
            left = std::make_unique<PipeExpression>(location, std::move(left), std::move(right));
        }

        ensure_chain_within_limit(++chain_length, location);
    }

    return left;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Binary expressions (precedence climbing)
// ═══════════════════════════════════════════════════════════════════════════════

ExpressionPtr Parser::parse_or() {
    return parse_binary_level(&Parser::parse_and, TokenType::PipePipe);
}

ExpressionPtr Parser::parse_null_coalescing() {
    return parse_binary_level(&Parser::parse_or, TokenType::QuestionQuestion);
}

ExpressionPtr Parser::parse_and() {
    return parse_binary_level(&Parser::parse_equality, TokenType::AmpersandAmpersand);
}

ExpressionPtr Parser::parse_bitwise_or() {
    return parse_binary_level(&Parser::parse_bitwise_xor, TokenType::Pipe);
}

ExpressionPtr Parser::parse_bitwise_xor() {
    return parse_binary_level(&Parser::parse_bitwise_and, TokenType::Caret);
}

ExpressionPtr Parser::parse_bitwise_and() {
    return parse_binary_level(&Parser::parse_shift, TokenType::Ampersand);
}

ExpressionPtr Parser::parse_equality() {
    return parse_binary_level(&Parser::parse_comparison, TokenType::EqualsEquals,
                              TokenType::BangEquals);
}

ExpressionPtr Parser::parse_comparison() {
    return parse_binary_level(&Parser::parse_bitwise_or, TokenType::Less, TokenType::Greater,
                              TokenType::LessEquals, TokenType::GreaterEquals, TokenType::In);
}

ExpressionPtr Parser::parse_shift() {
    return parse_binary_level(&Parser::parse_addition, TokenType::LessLess,
                              TokenType::GreaterGreater);
}

ExpressionPtr Parser::parse_addition() {
    return parse_binary_level(&Parser::parse_multiplication, TokenType::Plus, TokenType::Minus);
}

ExpressionPtr Parser::parse_multiplication() {
    return parse_binary_level(&Parser::parse_unary, TokenType::Star, TokenType::Slash,
                              TokenType::SlashSlash, TokenType::Percent);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Unary expressions
// ═══════════════════════════════════════════════════════════════════════════════

ExpressionPtr Parser::parse_unary() {
    if (is_prefix_unary_operator(current().type)) {
        // Stacked prefix operators (e.g. `~~~…`, `!!!…`, `---…`) recurse here
        // without re-entering the guarded parse_expression(), so bound them
        // explicitly to keep deeply-nested input from exhausting the stack.
        auto guard = make_recursion_guard(current().location);

        const auto location = current().location;
        const auto operator_type = advance().type;

        auto operand = parse_unary();

        return std::make_unique<UnaryExpression>(location, operator_type, std::move(operand));
    }

    return parse_postfix();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Postfix expressions (field access, index, call, range, propagation)
// ═══════════════════════════════════════════════════════════════════════════════

ExpressionPtr Parser::parse_postfix() {
    auto expression = parse_primary();

    std::size_t chain_length = 0;
    while (true) {
        if (check(TokenType::Dot)) {
            const auto location = current().location;

            advance();

            expression = parse_field_access(std::move(expression), location, /*is_optional=*/false);
        } else if (check(TokenType::QuestionDot)) {
            // Optional chaining: x?.field or x?.method()
            const auto location = current().location;

            advance();

            expression = parse_field_access(std::move(expression), location, /*is_optional=*/true);
        } else if (check(TokenType::QuestionBracket)) {
            // Optional index access: x?[i] — evaluates to null if x is null.
            const auto location = current().location;

            advance();

            auto index = parse_expression();

            recover_expect(TokenType::RightBracket);

            expression = std::make_unique<IndexAccessExpression>(location, std::move(expression),
                                                                 std::move(index), true);
        } else if (check(TokenType::LeftBracket)) {
            const auto location = current().location;

            advance();

            auto index = parse_expression();

            recover_expect(TokenType::RightBracket);

            expression = std::make_unique<IndexAccessExpression>(location, std::move(expression),
                                                                 std::move(index), false);
        } else if (check(TokenType::LeftParen) &&
                   (expression->kind == ExpressionKind::Variable ||
                    expression->kind == ExpressionKind::FieldAccess)) {
            expression = parse_call(std::move(expression));
        } else if (check(TokenType::ColonColon) && check_at(1, TokenType::Less) &&
                   (expression->kind == ExpressionKind::Variable ||
                    expression->kind == ExpressionKind::FieldAccess)) {
            expression = parse_turbofish_call(std::move(expression));
        } else if (check(TokenType::DotDot) || check(TokenType::DotDotEquals)) {
            const auto location = current().location;
            const bool inclusive = current().type == TokenType::DotDotEquals;

            advance();

            // The range end is parsed with parse_addition(), which re-descends
            // into parse_postfix() and greedily consumes any following `..`.
            // Unlike the iterative chain operators in this loop, chained ranges
            // (`1..2..3..…`) therefore recurse natively once per `..`, and the
            // ensure_chain_within_limit() counter below never catches them (each
            // parse_postfix invocation completes just one link).  Take the shared
            // recursion guard so deeply chained ranges raise a clean SyntaxError
            // instead of overflowing the native stack.
            auto guard = make_recursion_guard(location);

            auto end = parse_addition();

            expression = std::make_unique<RangeExpression>(location, std::move(expression),
                                                           std::move(end), inclusive);
        } else if (check(TokenType::QuestionMark) &&
                   (peek_at(1).location.line > current().location.line ||
                    !can_start_expression(peek_at(1).type))) {
            // Postfix ? for propagation on result/optional values. When the next
            // token sits on a later line it marks a statement boundary, so the ?
            // always propagates there; on the same line it only propagates when
            // the following token cannot itself begin an expression.
            const auto location = current().location;

            advance();

            expression = std::make_unique<UnaryExpression>(location, TokenType::QuestionMark,
                                                           std::move(expression));
        } else {
            break;
        }

        ensure_chain_within_limit(++chain_length, current().location);
    }

    return expression;
}

ExpressionPtr Parser::parse_field_access(ExpressionPtr receiver, const SourceLocation& op_loc,
                                         bool is_optional) {
    // After '.' / '?.', allow identifiers, keywords, and integer literals as
    // field names (e.g. Resource.with, tuple.0), but reject operators and
    // punctuation.
    const std::string_view op_spelling = is_optional ? "?." : ".";

    if (is_at_end() || !first_is_letter_or_digit(current().lexeme)) {
        syntax_error(
            std::format("expected field name after '{}', got '{}'", op_spelling, current().lexeme),
            current().location,
            std::format("field names must be identifiers (e.g. 'x{0}length') or "
                        "tuple indices (e.g. 'pair{0}0')",
                        op_spelling));
    }

    const std::string field_name = advance().lexeme;

    return std::make_unique<FieldAccessExpression>(op_loc, std::move(receiver), field_name,
                                                   is_optional);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Call expressions (regular, turbofish, argument lists)
// ═══════════════════════════════════════════════════════════════════════════════

ExpressionPtr Parser::parse_call(ExpressionPtr callee) {
    const auto location = current().location;

    std::vector<ExpressionPtr> arguments{};
    std::vector<NamedArgument> named_arguments{};

    parse_bracketed(TokenType::LeftParen, TokenType::RightParen, [&] {
        if (!check(TokenType::RightParen)) {
            parse_argument_list(arguments, named_arguments);
        }
    });

    return std::make_unique<CallExpression>(location, std::move(callee), std::move(arguments),
                                            std::move(named_arguments));
}

ExpressionPtr Parser::parse_turbofish_call(ExpressionPtr callee) {
    const auto location = current().location;

    expect(TokenType::ColonColon);
    expect(TokenType::Less);

    std::vector<TypeAnnotation> type_arguments;
    parse_comma_list(type_arguments, [this] { return parse_type_annotation(); });

    close_generic();

    expect(TokenType::LeftParen);

    std::vector<ExpressionPtr> arguments{};
    std::vector<NamedArgument> named_arguments{};

    if (!check(TokenType::RightParen)) {
        parse_argument_list(arguments, named_arguments);
    }

    recover_expect(TokenType::RightParen);

    return std::make_unique<CallExpression>(location, std::move(callee), std::move(arguments),
                                            std::move(named_arguments), std::move(type_arguments));
}

void Parser::parse_argument_list(std::vector<ExpressionPtr>& arguments,
                                 std::vector<NamedArgument>& named_arguments) {
    bool found_named_argument{false};

    const auto parse_one_argument = [&]() {
        if (check(TokenType::Identifier) && check_at(1, TokenType::Colon)) {
            found_named_argument = true;

            NamedArgument named_argument;
            named_argument.name = advance().lexeme;

            expect(TokenType::Colon);

            named_argument.value = parse_expression();

            named_arguments.push_back(std::move(named_argument));
        } else {
            if (found_named_argument) {
                syntax_error("positional argument after named argument", current().location,
                             "all positional arguments must come before named arguments");
            }

            arguments.push_back(parse_expression());
        }
    };

    parse_one_argument();

    while (consume_if(TokenType::Comma)) {
        parse_one_argument();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Primary expressions (literals, identifiers, record creation)
// ═══════════════════════════════════════════════════════════════════════════════

ExpressionPtr Parser::parse_primary_literal() {
    const auto location = current().location;

    if (check(TokenType::IntegerLiteral)) {
        const auto& token = advance();

        return std::make_unique<LiteralExpression>(location,
                                                   std::get<std::int64_t>(*token.literal));
    }

    if (check(TokenType::NumberLiteral)) {
        const auto& token = advance();

        return std::make_unique<LiteralExpression>(location, std::get<double>(*token.literal));
    }

    if (check(TokenType::BooleanLiteral)) {
        const bool value = (current().lexeme == "true");

        advance();

        return std::make_unique<LiteralExpression>(location, value);
    }

    if (check(TokenType::NoneLiteral)) {
        advance();

        return std::make_unique<LiteralExpression>(location);
    }

    if (check(TokenType::StringLiteral)) {
        return std::make_unique<LiteralExpression>(location, advance().lexeme);
    }

    if (check(TokenType::StringStart)) {
        return parse_string_interpolation();
    }

    return nullptr;
}

ExpressionPtr Parser::parse_primary() {
    if (auto lit = parse_primary_literal()) {
        return lit;
    }

    const auto location = current().location;

    if (check(TokenType::Success)) {
        return parse_wrapper_expression<SuccessExpression>();
    }

    if (check(TokenType::Failure)) {
        return parse_wrapper_expression<FailureExpression>();
    }

    if (check(TokenType::Some)) {
        return parse_wrapper_expression<SomeExpression>();
    }

    if (check(TokenType::Downcast) || check(TokenType::TrustedDowncast)) {
        const bool is_trusted = check(TokenType::TrustedDowncast);

        return parse_typed_expression<DowncastExpression>(is_trusted);
    }

    if (check(TokenType::Is)) {
        return parse_typed_expression<IsExpression>();
    }

    if (check(TokenType::Spawn)) {
        // `spawn` recurses into parse_postfix() without re-entering the guarded
        // parse_expression(), so bound stacked `spawn spawn …` chains here.
        auto guard = make_recursion_guard(location);

        advance();

        auto call = parse_postfix();

        return std::make_unique<SpawnExpression>(location, std::move(call));
    }

    if (check(TokenType::TaskScope)) {
        advance();

        auto body = parse_statement_block();

        return std::make_unique<TaskScopeExpression>(location, std::move(body));
    }

    if (check(TokenType::Await)) {
        // `await` recurses into parse_unary() without re-entering the guarded
        // parse_expression(), so bound stacked `await await …` chains here.
        auto guard = make_recursion_guard(location);

        advance();

        auto operand = parse_unary();

        return std::make_unique<AwaitExpression>(location, std::move(operand));
    }

    if (check(TokenType::If)) {
        return parse_if_expression();
    }

    if (check(TokenType::Match)) {
        return parse_match_expression();
    }

    if (check(TokenType::LeftBracket)) {
        return parse_array_literal();
    }

    if (check(TokenType::LeftBrace)) {
        return parse_dict_or_block_expr();
    }

    if (check(TokenType::LeftParen)) {
        return parse_paren_or_lambda_or_tuple();
    }

    if (check(TokenType::Identifier)) {
        return parse_identifier_or_record_creation();
    }

    syntax_error(std::format("unexpected token '{}'", current().lexeme), current().location,
                 "expected an expression (literal, variable, function call, etc.)");
}

ExpressionPtr Parser::parse_identifier_or_record_creation() {
    const auto location = current().location;
    const auto name = advance().lexeme;

    // Detect qualified record creation: Namespace.TypeName { ... }
    if (starts_with_uppercase(name) && check(TokenType::Dot) &&
        check_at(1, TokenType::Identifier)) {
        const auto& next_lexeme = peek_at(1).lexeme;

        if (starts_with_uppercase(next_lexeme) && check_at(2, TokenType::LeftBrace)) {
            advance(); // consume '.'

            const auto type_name = name + '.' + advance().lexeme;

            return parse_record_creation(location, type_name, {});
        }
    }

    if (starts_with_uppercase(name) && check(TokenType::LeftBrace)) {
        return parse_record_creation(location, name, {});
    }

    // Generic record creation: TypeName<T1, T2> { ... }
    if (starts_with_uppercase(name) && check(TokenType::Less) && is_generic_record_creation()) {
        advance(); // consume '<'

        std::vector<TypeAnnotation> type_args;
        parse_comma_list(type_args, [this] { return parse_type_annotation(); });

        close_generic();

        return parse_record_creation(location, name, std::move(type_args));
    }

    return std::make_unique<VariableExpression>(location, name);
}

// ═══════════════════════════════════════════════════════════════════════════════
// String interpolation
// ═══════════════════════════════════════════════════════════════════════════════

ExpressionPtr Parser::parse_string_interpolation() {
    const auto location = current().location;

    auto interpolation = std::make_unique<StringInterpolationExpression>(location);

    interpolation->parts.push_back(advance().lexeme);

    while (true) {
        expect(TokenType::InterpolationStart);

        interpolation->expressions.push_back(parse_expression());

        expect(TokenType::InterpolationEnd);

        if (check(TokenType::StringMiddle)) {
            interpolation->parts.push_back(advance().lexeme);
        } else if (check(TokenType::StringEnd)) {
            interpolation->parts.push_back(advance().lexeme);

            break;
        } else {
            syntax_error("expected string continuation", current().location,
                         "this may be caused by a mismatched interpolation bracket");
        }
    }

    return interpolation;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Special forms (if-expression, match-expression)
// ═══════════════════════════════════════════════════════════════════════════════

// Parses either a braced block or a single expression for an if/else branch.
IfExpression::Branch Parser::parse_if_branch() {
    expect(TokenType::LeftBrace);

    IfExpression::Branch result{std::vector<StatementPtr>{}};

    if (!check(TokenType::RightBrace)) {
        auto first_statement = parse_statement();

        if (check(TokenType::RightBrace) && first_statement->kind == StatementKind::Expression) {
            result =
                std::move(static_cast<ExpressionStatement*>(first_statement.get())->expression);
        } else {
            auto& body = std::get<std::vector<StatementPtr>>(result);
            body.push_back(std::move(first_statement));

            while (!check(TokenType::RightBrace) && !is_at_end()) {
                body.push_back(parse_statement());
            }
        }
    }

    recover_expect(TokenType::RightBrace);

    return result;
}

ExpressionPtr Parser::parse_if_expression() {
    const auto location = current().location;

    expect(TokenType::If);

    auto condition = parse_expression();
    auto expression = std::make_unique<IfExpression>(location, std::move(condition));

    expression->then_branch = parse_if_branch();

    expect(TokenType::Else);

    // else if → recursively parse as a nested if expression.
    if (check(TokenType::If)) {
        // This recurses without re-entering the guarded parse_expression(), so
        // bound the else-if chain explicitly to keep deeply-chained input from
        // exhausting the stack.
        auto guard = make_recursion_guard(current().location);

        expression->else_branch = parse_if_expression();

        return expression;
    }

    expression->else_branch = parse_if_branch();

    return expression;
}

ExpressionPtr Parser::parse_match_expression() {
    const auto location = current().location;

    expect(TokenType::Match);

    auto subject = parse_expression();
    auto expression = std::make_unique<MatchExpression>(location, std::move(subject));

    expression->arms = parse_bracketed(TokenType::LeftBrace, TokenType::RightBrace,
                                       [this] { return parse_match_arms(); });

    return expression;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Collection literals (array, dictionary, tuple)
// ═══════════════════════════════════════════════════════════════════════════════

ExpressionPtr Parser::parse_array_literal() {
    const auto location = current().location;

    auto elements = parse_bracketed(TokenType::LeftBracket, TokenType::RightBracket, [&] {
        std::vector<ExpressionPtr> elems{};

        parse_trailing_comma_list(TokenType::RightBracket,
                                  [&] { elems.push_back(parse_expression()); });

        return elems;
    });

    return std::make_unique<ArrayLiteralExpression>(location, std::move(elements));
}

ExpressionPtr Parser::parse_dict_or_block_expr() {
    const auto location = current().location;

    expect(TokenType::LeftBrace);

    if (check(TokenType::RightBrace)) {
        advance();

        return std::make_unique<DictionaryLiteralExpression>(location,
                                                             std::vector<DictionaryEntry>{});
    }

    if (check(TokenType::StringLiteral) && check_at(1, TokenType::Colon)) {
        std::vector<DictionaryEntry> entries{};

        do {
            DictionaryEntry entry;

            const auto key_loc = current().location;
            const auto key = expect(TokenType::StringLiteral).lexeme;

            entry.key = std::make_unique<LiteralExpression>(key_loc, key);

            expect(TokenType::Colon);

            entry.value = parse_expression();

            entries.push_back(std::move(entry));
        } while (consume_if(TokenType::Comma));

        recover_expect(TokenType::RightBrace);

        return std::make_unique<DictionaryLiteralExpression>(location, std::move(entries));
    }

    syntax_error("unexpected '{' — block expressions are not "
                 "supported outside of control flow",
                 location, "use if, match, for, or while to introduce a block");
}

ExpressionPtr Parser::parse_paren_or_lambda_or_tuple() {
    const auto location = current().location;

    if (looks_like_lambda()) {
        return parse_inline_lambda();
    }

    expect(TokenType::LeftParen);

    auto first = parse_expression();

    if (consume_if(TokenType::Comma)) {
        std::vector<ExpressionPtr> elements{};
        elements.push_back(std::move(first));
        elements.push_back(parse_expression());

        while (consume_if(TokenType::Comma)) {
            elements.push_back(parse_expression());
        }

        recover_expect(TokenType::RightParen);

        return std::make_unique<TupleLiteralExpression>(location, std::move(elements));
    }

    recover_expect(TokenType::RightParen);

    return first;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lambda expressions
// ═══════════════════════════════════════════════════════════════════════════════

bool Parser::looks_like_lambda() const {
    if (check(TokenType::LeftParen) && check_at(1, TokenType::RightParen) &&
        check_at(2, TokenType::Arrow)) {
        return true;
    }

    if (!check(TokenType::LeftParen)) {
        return false;
    }

    // Skip past the matching ')'.
    const int offset = find_matching_paren(1);

    return check_at(offset, TokenType::Arrow);
}

ExpressionPtr Parser::parse_inline_lambda() {
    const auto location = current().location;

    expect(TokenType::LeftParen);

    std::vector<Parameter> parameters{};

    if (!check(TokenType::RightParen)) {
        parameters = parse_parameter_list();
    }

    recover_expect(TokenType::RightParen);
    expect(TokenType::Arrow);

    auto lambda = std::make_unique<LambdaExpression>(location, std::move(parameters), std::nullopt);

    if (check(TokenType::LeftBrace)) {
        lambda->body = parse_statement_block();
    } else {
        lambda->body = parse_expression();
    }

    return lambda;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Generic record creation lookahead
// ═══════════════════════════════════════════════════════════════════════════════

bool Parser::is_generic_record_creation() const {
    // Precondition: current token is '<'.
    int depth = 1;
    int offset = 1;

    while (depth > 0) {
        const TokenType type = token_type_at(offset);

        if (type == TokenType::EndOfFile) {
            return false;
        }

        if (type == TokenType::Less) {
            ++depth;
        } else if (type == TokenType::Greater) {
            --depth;
        } else if (type == TokenType::GreaterGreater) {
            depth -= 2;
        }

        ++offset;
    }

    // A '<...>' that closes exactly (depth == 0) is a generic argument list only
    // when a '{' follows. An over-close such as `A<B>>` drives depth negative, so
    // it is not a record creation.
    return depth == 0 && token_type_at(offset) == TokenType::LeftBrace;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Record creation
// ═══════════════════════════════════════════════════════════════════════════════

ExpressionPtr Parser::parse_record_creation(const SourceLocation& location,
                                            std::string_view type_name,
                                            std::vector<TypeAnnotation> type_args) {
    expect(TokenType::LeftBrace);

    std::vector<RecordFieldInit> fields{};

    const auto parse_field = [&]() {
        RecordFieldInit field_init;
        const auto& name_token = expect(TokenType::Identifier);
        field_init.name = name_token.lexeme;

        // Shorthand: `Record { x }` is equivalent to `Record { x = x }`
        if (consume_if(TokenType::Equals)) {
            field_init.value = parse_expression();
        } else {
            // Generate a variable reference with the same name as the field.
            field_init.value = std::make_unique<VariableExpression>(name_token.location,
                                                                    std::string{field_init.name});
        }

        fields.push_back(std::move(field_init));
    };

    parse_trailing_comma_list(TokenType::RightBrace, parse_field);

    recover_expect(TokenType::RightBrace);

    return std::make_unique<RecordCreationExpression>(location, std::string{type_name},
                                                      std::move(type_args), std::move(fields));
}

} // namespace luma
