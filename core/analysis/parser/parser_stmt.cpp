// parser_stmt.cpp — Statement parsing methods for the Parser class.
//
// Sections:
//   1. Statement dispatch (parse_statement — routes to specific parsers)
//   2. Variable declarations (mutable, immutable, tuple destructuring)
//   3. Lookahead helpers (skip_type_at, looks_like_variable_decl,
//      looks_like_tuple_destructuring)
//   4. Control flow (return, for, while, try/catch/finally)
//   5. Conditional statements (if/else, match)
//   6. Match arm parsing (patterns, guards, alternatives, destructuring)
//   7. Expression statements (assignment, compound assignment, increment)
//   8. Block body

#include <format>

#include "analysis/lexer/token_type.hpp"
#include "analysis/parser/operator_matcher.hpp"
#include "analysis/parser/parser.hpp"
#include "analysis/parser/parser_messages.hpp"
#include "common/string_utils.hpp"

namespace luma {

// ═══════════════════════════════════════════════════════════════════════════════
// Statement dispatch
// ═══════════════════════════════════════════════════════════════════════════════

StatementPtr Parser::parse_statement() {
    auto guard = make_recursion_guard(current().location);

    // Mutable variable declaration or mutable tuple destructuring.
    if (check(TokenType::Mutable)) {
        return parse_mutable_decl();
    }

    // Tuple destructuring: (type name, type name) = expr
    if (check(TokenType::LeftParen) && looks_like_tuple_destructuring()) {
        return parse_tuple_destructuring(false);
    }

    // Return.
    if (check(TokenType::Return)) {
        return parse_return_statement();
    }

    // For.
    if (check(TokenType::For)) {
        return parse_for_statement();
    }

    // While.
    if (check(TokenType::While)) {
        return parse_while_statement();
    }

    // Try.
    if (check(TokenType::Try)) {
        return parse_try_statement();
    }

    // If (as statement).
    if (check(TokenType::If)) {
        return parse_if_statement();
    }

    // Match (as statement).
    if (check(TokenType::Match)) {
        return parse_match_statement();
    }

    // Break.
    if (check(TokenType::Break)) {
        const auto location = current().location;

        advance();

        return std::make_unique<BreakStatement>(location);
    }

    // Continue.
    if (check(TokenType::Continue)) {
        const auto location = current().location;

        advance();

        return std::make_unique<ContinueStatement>(location);
    }

    // Variable declaration: type name = expr
    if (looks_like_variable_decl()) {
        return parse_variable_decl(false);
    }

    // Detect keywords from other languages used as statements.
    // Only trigger when the pattern looks like a declaration attempt
    // (e.g. "var x = 5", "fn greet()", "class Point {").
    if (current().type == TokenType::Identifier) {
        auto hint = foreign_keyword_hint(current().lexeme);

        if (hint.has_value() &&
            (check_at(1, TokenType::Identifier) || check_at(1, TokenType::LeftParen) ||
             check_at(1, TokenType::LeftBrace))) {
            syntax_error(std::format("'{}' is not a Luma keyword", current().lexeme),
                         current().location, *hint);
        }
    }

    // Expression statement (may become assignment/compound/incr/decr).
    return parse_expression_statement();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Variable declarations
// ═══════════════════════════════════════════════════════════════════════════════

StatementPtr Parser::parse_mutable_decl() {
    expect(TokenType::Mutable);

    // mutable (T1 name1, T2 name2) = expr  →  tuple destructuring
    // mutable (T1, T2) name = expr         →  typed variable declaration
    if (check(TokenType::LeftParen)) {
        if (looks_like_tuple_destructuring()) {
            return parse_tuple_destructuring(true);
        }

        return parse_variable_decl(true);
    }

    return parse_variable_decl(true);
}

StatementPtr Parser::parse_variable_decl(bool is_mutable) {
    const auto location = current().location;

    auto type = parse_type_annotation();

    const auto name = expect(TokenType::Identifier).lexeme;

    expect(TokenType::Equals);

    auto initializer = parse_expression();

    return std::make_unique<VariableDeclStatement>(location, std::move(type), name, is_mutable,
                                                   std::move(initializer));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lookahead helpers (type skipping, declaration detection)
// ═══════════════════════════════════════════════════════════════════════════════

int Parser::skip_type_at(int offset, int depth) const {
    // Guard against offset overflow and runaway recursion.  This lookahead runs
    // *before* the depth-tracked parse path, so without its own bound a deeply
    // nested type-like construct (e.g. thousands of '(') would overflow the
    // native stack here instead of yielding the "maximum nesting depth
    // exceeded" diagnostic the main parser emits.  Bailing out returns the
    // current offset, routing the construct to the bounded expression parser.
    if (offset > token_count_ || depth > ResourceLimits::max_parse_depth) {
        return offset;
    }

    // Tuple type: (T, T, ...)
    if (token_type_at(offset) == TokenType::LeftParen) {
        ++offset;

        offset = skip_type_at(offset, depth + 1);

        while (token_type_at(offset) == TokenType::Comma) {
            ++offset;

            offset = skip_type_at(offset, depth + 1);
        }

        if (token_type_at(offset) == TokenType::RightParen) {
            ++offset;
        }

        return offset;
    }

    // function(T, ...) -> R
    if (token_type_at(offset) == TokenType::Function) {
        ++offset;

        if (token_type_at(offset) == TokenType::LeftParen) {
            ++offset;

            if (token_type_at(offset) != TokenType::RightParen) {
                offset = skip_type_at(offset, depth + 1);

                while (token_type_at(offset) == TokenType::Comma) {
                    ++offset;

                    offset = skip_type_at(offset, depth + 1);
                }
            }

            if (token_type_at(offset) == TokenType::RightParen) {
                ++offset;
            }
        }

        if (token_type_at(offset) == TokenType::Arrow) {
            ++offset;

            offset = skip_type_at(offset, depth + 1);
        }

        return offset;
    }

    // Named type (primitives, generics, qualified identifiers).
    if (is_type_keyword(token_type_at(offset)) || token_type_at(offset) == TokenType::Identifier) {
        ++offset;

        // Namespace-qualified: Namespace.TypeName
        while (token_type_at(offset) == TokenType::Dot &&
               token_type_at(offset + 1) == TokenType::Identifier) {
            offset += 2;
        }

        // Generic params: Type<T, U>
        if (token_type_at(offset) == TokenType::Less) {
            ++offset;

            int angle_depth = 1;

            while (angle_depth > 0 && token_pos_ + offset < token_count_) {
                if (token_type_at(offset) == TokenType::Less) {
                    ++angle_depth;
                }

                if (token_type_at(offset) == TokenType::Greater) {
                    --angle_depth;
                }

                // '>>' is tokenised as a single token for nested generics.
                if (token_type_at(offset) == TokenType::GreaterGreater) {
                    angle_depth -= 2;
                }

                ++offset;
            }
        }
    }

    return offset;
}

bool Parser::looks_like_variable_decl() const {
    // Helper: check whether the token at `after_type` is an identifier
    // (or a keyword mistakenly used as one) followed by '=' (not '==').
    auto name_followed_by_equals = [&](int after_type) {
        const bool has_equals = check_at(after_type + 1, TokenType::Equals) &&
                                !check_at(after_type + 1, TokenType::EqualsEquals);

        if (!has_equals) {
            return false;
        }

        if (check_at(after_type, TokenType::Identifier)) {
            return true;
        }

        // Also match `type keyword =` so we route to parse_variable_decl(),
        // which will produce a descriptive "reserved keyword" error.
        const int idx = token_pos_ + after_type;

        return idx >= 0 && idx < token_count_ &&
               is_keyword_token_type(tokens_[static_cast<std::size_t>(idx)].type);
    };

    // Tuple type annotation: (T, T) name = expr
    if (check(TokenType::LeftParen)) {
        const int after_type = skip_type_at(0);

        return name_followed_by_equals(after_type);
    }

    // Unique/borrow modifier before a type: unique T name = expr
    if (check(TokenType::Unique) || check(TokenType::Borrow)) {
        const int after_type = skip_type_at(1); // skip the modifier, then the type

        return name_followed_by_equals(after_type);
    }

    if (is_type_keyword(current().type)) {
        const int after_type = skip_type_at(0);

        return name_followed_by_equals(after_type);
    }

    // Uppercase identifier could be a record/enum type, possibly generic
    // (e.g. Box<integer> b = ...) or namespace-qualified.
    if (check(TokenType::Identifier) && starts_with_uppercase(current().lexeme)) {
        const int after_type = skip_type_at(0);

        return name_followed_by_equals(after_type);
    }

    return false;
}

bool Parser::looks_like_tuple_destructuring() const {
    if (!check(TokenType::LeftParen)) {
        return false;
    }

    // Skip past the matching ')'.
    const int offset = find_matching_paren(1);

    // After ) should be =
    return check_at(offset, TokenType::Equals) && !check_at(offset, TokenType::EqualsEquals);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tuple destructuring
// ═══════════════════════════════════════════════════════════════════════════════

StatementPtr Parser::parse_tuple_destructuring(bool is_mutable) {
    const auto location = current().location;

    expect(TokenType::LeftParen);

    std::vector<std::pair<TypeAnnotation, std::string>> bindings;

    do {
        auto type = parse_type_annotation();
        auto name = expect(TokenType::Identifier).lexeme;

        bindings.emplace_back(std::move(type), std::move(name));
    } while (consume_if(TokenType::Comma));

    recover_expect(TokenType::RightParen);
    expect(TokenType::Equals);

    auto initializer = parse_expression();

    return std::make_unique<TupleDestructuringStatement>(location, std::move(bindings), is_mutable,
                                                         std::move(initializer));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Control flow (return, for, while, try/catch/finally)
// ═══════════════════════════════════════════════════════════════════════════════

StatementPtr Parser::parse_return_statement() {
    const auto location = current().location;

    advance(); // consume 'return'

    ExpressionPtr value{};

    if (!check(TokenType::RightBrace) && !is_at_end()) {
        value = parse_expression();
    }

    return std::make_unique<ReturnStatement>(location, std::move(value));
}

StatementPtr Parser::parse_for_statement() {
    const auto location = current().location;

    expect(TokenType::For);

    std::string loop_variable{};
    std::string index_variable{};
    std::vector<std::string> destructure_variables{};

    // Check for tuple destructuring: for (a, b) in ...
    if (consume_if(TokenType::LeftParen)) {
        destructure_variables.push_back(expect(TokenType::Identifier).lexeme);

        while (consume_if(TokenType::Comma)) {
            destructure_variables.push_back(expect(TokenType::Identifier).lexeme);
        }

        recover_expect(TokenType::RightParen);
    } else {
        loop_variable = expect(TokenType::Identifier).lexeme;

        if (consume_if(TokenType::Comma)) {
            index_variable = loop_variable;
            loop_variable = expect(TokenType::Identifier).lexeme;
        }
    }

    expect(TokenType::In);

    auto iterable = parse_expression();

    auto statement = std::make_unique<ForStatement>(location, std::move(loop_variable),
                                                    std::move(index_variable), std::move(iterable));
    statement->destructure_variables = std::move(destructure_variables);

    statement->body = parse_statement_block();

    return statement;
}

StatementPtr Parser::parse_while_statement() {
    const auto location = current().location;

    expect(TokenType::While);

    auto condition = parse_expression();
    auto statement = std::make_unique<WhileStatement>(location, std::move(condition));

    statement->body = parse_statement_block();

    return statement;
}

StatementPtr Parser::parse_try_statement() {
    const auto location = current().location;

    expect(TokenType::Try);

    auto statement = std::make_unique<TryStatement>(location);

    statement->try_body = parse_statement_block();

    bool has_catch{false};
    bool has_finally{false};

    // Optional catch block: catch(varname) { ... }
    if (consume_if(TokenType::Catch)) {
        has_catch = true;

        expect(TokenType::LeftParen);

        statement->catch_var = expect(TokenType::Identifier).lexeme;

        recover_expect(TokenType::RightParen);

        statement->catch_body = parse_statement_block();
    }

    // Optional finally block: finally { ... }
    if (consume_if(TokenType::Finally)) {
        has_finally = true;

        statement->finally_body = parse_statement_block();
    }

    // At least one of catch or finally must be present.
    if (!has_catch && !has_finally) {
        syntax_error("try must have at least a 'catch' or 'finally' block", location,
                     "add 'catch error { ... }' or 'finally { ... }' after the try block");
    }

    return statement;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Conditional statements (if/else, match)
// ═══════════════════════════════════════════════════════════════════════════════

StatementPtr Parser::parse_if_statement() {
    const auto location = current().location;

    expect(TokenType::If);

    auto condition = parse_expression();
    auto statement = std::make_unique<IfStatement>(location, std::move(condition));

    statement->then_body = parse_statement_block();

    if (consume_if(TokenType::Else)) {
        if (check(TokenType::If)) {
            // else if → wrap in a single-statement else body.  This recurses
            // without re-entering the guarded parse_statement(), so bound the
            // else-if chain explicitly to keep deeply-chained input from
            // exhausting the stack.
            auto guard = make_recursion_guard(current().location);

            auto nested = parse_if_statement();

            statement->else_body.push_back(std::move(nested));
        } else {
            statement->else_body = parse_statement_block();
        }
    }

    return statement;
}

StatementPtr Parser::parse_match_statement() {
    const auto location = current().location;

    expect(TokenType::Match);

    auto subject = parse_expression();
    auto statement = std::make_unique<MatchStatement>(location, std::move(subject));

    statement->arms = parse_bracketed(TokenType::LeftBrace, TokenType::RightBrace,
                                      [this] { return parse_match_arms(); });

    return statement;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Match arm parsing (patterns, guards, alternatives, destructuring)
// ═══════════════════════════════════════════════════════════════════════════════

std::vector<MatchArm> Parser::parse_match_arms() {
    std::vector<MatchArm> arms{};

    while (!check(TokenType::RightBrace) && !is_at_end()) {
        arms.push_back(parse_match_arm());
    }

    return arms;
}

Parser::QualifiedVariant Parser::parse_qualified_variant_name() {
    const auto first = advance().lexeme;

    expect(TokenType::Dot);

    std::string type_name;

    if (check(TokenType::Identifier) && check_at(1, TokenType::Dot)) {
        // Three-part: Namespace.Type.Variant — store the qualified type name
        // ("Namespace.Type").  This matches how a namespaced choice is keyed in
        // the type checker (choices_["Namespace.Type"]), so exhaustiveness and
        // field binding do a direct, unambiguous lookup even when two choices
        // share a bare name (e.g. Terminal.Color vs a top-level Color).
        type_name = first + "." + advance().lexeme;

        expect(TokenType::Dot);
    } else {
        type_name = first;
    }

    auto variant_name = expect(TokenType::Identifier).lexeme;

    return {.type_name = std::move(type_name), .variant_name = std::move(variant_name)};
}

void Parser::parse_pattern_variant_into(MatchArm::AlternativePattern& alt) {
    auto [type_name, variant_name] = parse_qualified_variant_name();

    alt.pattern = MatchArm::VariantPatternData{.enum_type = std::move(type_name),
                                               .enum_variant = std::move(variant_name)};
}

MatchArm::AlternativePattern Parser::parse_alternative_pattern() {
    MatchArm::AlternativePattern alt;

    if (check(TokenType::BooleanLiteral) || check(TokenType::IntegerLiteral) ||
        check(TokenType::NoneLiteral) || check(TokenType::StringLiteral)) {
        parse_pattern_literal(alt.pattern);
    } else if (is_comparison_op(current().type)) {
        parse_pattern_comparison(alt.pattern);
    } else if (check(TokenType::Identifier) && check_at(1, TokenType::Dot)) {
        parse_pattern_variant_into(alt);

        if (check(TokenType::LeftParen)) {
            syntax_error("choice destructuring is not allowed in '|' alternatives",
                         current().location, parser_msg::hint_split_match_arms);
        }
    } else {
        syntax_error("invalid alternative pattern after '|'", current().location,
                     "expected a literal, choice variant, or comparison");
    }

    return alt;
}

// ── Shared pattern helpers ──────────────────────────────────────────────────

void Parser::parse_pattern_literal(MatchPattern::PatternData& pattern) {
    if (check(TokenType::BooleanLiteral)) {
        pattern = MatchArm::BooleanPatternData{current().lexeme == "true"};

        advance();
    } else if (check(TokenType::IntegerLiteral)) {
        const auto location = current().location;
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the lexer always sets the payload.
        const auto lo = std::get<std::int64_t>(*current().literal);

        advance();

        // A range pattern: `case lo..hi` (half-open) or `case lo..=hi` (closed).
        if (check(TokenType::DotDot) || check(TokenType::DotDotEquals)) {
            const bool inclusive = current().type == TokenType::DotDotEquals;

            advance();

            const auto& hi_token = expect(TokenType::IntegerLiteral);
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the lexer always sets the payload.
            const auto hi = std::get<std::int64_t>(*hi_token.literal);

            pattern = MatchArm::RangePatternData{
                .lo = lo, .hi = hi, .inclusive = inclusive, .location = location};
        } else {
            pattern = MatchArm::IntegerPatternData{lo};
        }
    } else if (check(TokenType::NoneLiteral)) {
        pattern = MatchArm::NonePatternData{};

        advance();
    } else if (check(TokenType::StringLiteral)) {
        pattern = MatchArm::StringPatternData{std::string(advance().lexeme)};
    }
}

void Parser::parse_pattern_comparison(MatchPattern::PatternData& pattern) {
    const auto op = current().type;

    advance();

    pattern = MatchArm::ComparisonPatternData{.comparison_op = op,
                                              .comparison_value = parse_bitwise_xor()};
}

template <typename PatternData> void Parser::parse_single_binding_pattern(MatchArm& arm) {
    advance(); // consume the keyword (success / failure / some)
    expect(TokenType::LeftParen);

    arm.pattern = PatternData{std::string(expect(TokenType::Identifier).lexeme)};

    recover_expect(TokenType::RightParen);
}

void Parser::parse_pattern_choice_destructure(MatchArm& arm, std::string_view type_name,
                                              std::string_view variant_name) {
    MatchArm::ChoicePatternData choice;
    choice.enum_type = type_name;
    choice.enum_variant = variant_name;

    advance(); // consume '('

    if (!check(TokenType::RightParen)) {
        choice.choice_bindings.push_back(expect(TokenType::Identifier).lexeme);

        while (consume_if(TokenType::Comma)) {
            choice.choice_bindings.push_back(expect(TokenType::Identifier).lexeme);
        }
    }

    recover_expect(TokenType::RightParen);

    arm.pattern = std::move(choice);
}

void Parser::parse_pattern_guard(MatchArm& arm) {
    if (check(TokenType::Identifier) && current().lexeme == "when") {
        advance(); // consume 'when'
        arm.guard = parse_expression();
    }
}

MatchArm Parser::parse_match_arm() {
    MatchArm arm;

    if (consume_if(TokenType::Else)) {
        arm.pattern = MatchArm::ElsePatternData{};
    } else if (check(TokenType::Success)) {
        parse_single_binding_pattern<MatchArm::SuccessPatternData>(arm);

        parse_pattern_guard(arm);
    } else if (check(TokenType::Failure)) {
        parse_single_binding_pattern<MatchArm::FailurePatternData>(arm);

        parse_pattern_guard(arm);
    } else {
        parse_case_pattern(arm);
    }

    arm.body = parse_statement_block();

    return arm;
}

void Parser::parse_case_pattern(MatchArm& arm) {
    expect(TokenType::Case);

    if (check(TokenType::BooleanLiteral) || check(TokenType::IntegerLiteral) ||
        check(TokenType::NoneLiteral) || check(TokenType::StringLiteral)) {
        parse_pattern_literal(arm.pattern);
    } else if (check(TokenType::Some)) {
        parse_single_binding_pattern<MatchArm::SomePatternData>(arm);
    } else if (is_comparison_op(current().type)) {
        parse_pattern_comparison(arm.pattern);
    } else if (check(TokenType::Identifier) && check_at(1, TokenType::Dot)) {
        // Enum/Choice case: [Namespace.]Type.Variant[(bindings)]
        auto [type_name, variant_name] = parse_qualified_variant_name();

        // Check for destructuring: case Type.Variant(a, b) { ... }
        if (check(TokenType::LeftParen)) {
            parse_pattern_choice_destructure(arm, type_name, variant_name);
        } else {
            arm.pattern = MatchArm::VariantPatternData{.enum_type = std::move(type_name),
                                                       .enum_variant = std::move(variant_name)};
        }
    } else {
        syntax_error("invalid match arm", current().location,
                     "expected a literal, variable binding, "
                     "choice variant, or '_' wildcard");
    }

    // Parse alternative patterns: case A | B | C { ... }
    if (check(TokenType::Pipe)) {
        const bool has_binding =
            (arm.kind() == MatchArm::Kind::SuccessResult ||
             arm.kind() == MatchArm::Kind::FailureResult || arm.kind() == MatchArm::Kind::SomeCase);
        const bool has_choice = (arm.kind() == MatchArm::Kind::ChoiceCase);
        if (has_binding || has_choice) {
            syntax_error("'|' alternatives cannot be used with patterns that "
                         "bind variables",
                         current().location, parser_msg::hint_split_match_arms);
        }
        while (consume_if(TokenType::Pipe)) {
            arm.alternatives.push_back(parse_alternative_pattern());
        }
    }

    // Parse optional guard.
    parse_pattern_guard(arm);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Expression statements (assignment, compound assignment, increment/decrement)
// ═══════════════════════════════════════════════════════════════════════════════

StatementPtr Parser::parse_expression_statement() {
    const auto location = current().location;

    auto expression = parse_expression();

    // Check for assignment.
    if (consume_if(TokenType::Equals)) {
        auto value = parse_expression();

        return std::make_unique<AssignmentStatement>(location, std::move(expression),
                                                     std::move(value));
    }

    // Compound assignment.
    if (is_compound_assignment_operator(current().type)) {
        const auto operator_type = advance().type;

        auto value = parse_expression();

        return std::make_unique<CompoundAssignmentStatement>(location, std::move(expression),
                                                             operator_type, std::move(value));
    }

    // Increment / decrement.
    if (consume_if(TokenType::PlusPlus)) {
        return std::make_unique<IncrementStatement>(location, std::move(expression));
    }

    if (consume_if(TokenType::MinusMinus)) {
        return std::make_unique<DecrementStatement>(location, std::move(expression));
    }

    return std::make_unique<ExpressionStatement>(location, std::move(expression));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Block body
// ═══════════════════════════════════════════════════════════════════════════════

std::vector<StatementPtr> Parser::parse_block_body() {
    std::vector<StatementPtr> body{};

    while (!check(TokenType::RightBrace) && !is_at_end()) {
        body.push_back(parse_statement());
    }

    return body;
}

std::vector<StatementPtr> Parser::parse_statement_block() {
    return parse_bracketed(TokenType::LeftBrace, TokenType::RightBrace,
                           [this] { return parse_block_body(); });
}

} // namespace luma
