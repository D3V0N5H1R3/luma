#include "analysis/parser/type_annotation_parser.hpp"

#include <format>

#include "analysis/lexer/token_type.hpp"
#include "analysis/parser/parser.hpp"

namespace luma {

void TypeAnnotationParser::close_generic(Parser& parser) {
    if (parser.check(TokenType::Greater)) {
        parser.advance();

        return;
    }

    if (parser.check(TokenType::GreaterGreater)) {
        // Split '>>' — consume only the first '>'
        if (parser.token_pos_ >= 0 && parser.token_pos_ < parser.token_count_) {
            const auto idx = static_cast<std::size_t>(parser.token_pos_);

            parser.tokens_[idx].type = TokenType::Greater;
            parser.tokens_[idx].lexeme = ">";
        }

        return; // don't advance; outer call will consume the remaining '>'
    }

    parser.syntax_error(std::format("expected '>', got '{}' '{}'",
                                    token_type_to_string(parser.current().type),
                                    parser.current().lexeme),
                        parser.current().location, "close the generic type argument list with '>'");
}

TypeAnnotation TypeAnnotationParser::parse(Parser& parser) {
    // Bound the recursion so deeply-nested type annotations (for example
    // array<array<array<…>>> or nested tuple/function types) report a
    // diagnostic instead of exhausting the native stack.  Shares the parser's
    // depth budget with expression and statement nesting, so a single
    // max_parse_depth caps total syntactic nesting on every recursive path.
    auto guard = parser.make_recursion_guard(parser.current().location);

    // Handle unique/borrow modifiers.
    TypeOwnership ownership;

    if (parser.consume_if(TokenType::Unique)) {
        ownership.is_unique = true;
    } else if (parser.consume_if(TokenType::Borrow)) {
        ownership.is_borrow = true;
    }

    // Tuple type: (T, T, ...)
    if (parser.check(TokenType::LeftParen)) {
        return parse_tuple_type(parser, ownership);
    }

    // function(T,...) -> T
    if (parser.check(TokenType::Function)) {
        return parse_function_type(parser, ownership);
    }

    return parse_named_type(parser, ownership);
}

TypeAnnotation TypeAnnotationParser::parse_tuple_type(Parser& parser, TypeOwnership ownership) {
    parser.advance();

    // Tuple — use factory method to enforce shape invariants.
    auto annotation = TypeAnnotation::make_tuple({});
    annotation.is_unique = ownership.is_unique;
    annotation.is_borrow = ownership.is_borrow;

    annotation.tuple_elements().push_back(parse(parser));

    parser.expect(TokenType::Comma);

    annotation.tuple_elements().push_back(parse(parser));

    while (parser.consume_if(TokenType::Comma)) {
        annotation.tuple_elements().push_back(parse(parser));
    }

    parser.recover_expect(TokenType::RightParen);

    return annotation;
}

TypeAnnotation TypeAnnotationParser::parse_function_type(Parser& parser, TypeOwnership ownership) {
    parser.advance();

    parser.expect(TokenType::LeftParen);

    std::vector<TypeAnnotation> params;

    if (!parser.check(TokenType::RightParen)) {
        parser.parse_comma_list(params, [&] { return parse(parser); });
    }

    parser.recover_expect(TokenType::RightParen);
    parser.expect(TokenType::Arrow);

    TypeAnnotation ret = parse(parser);
    TypeAnnotation annotation = TypeAnnotation::make_function(std::move(params), std::move(ret));
    annotation.is_unique = ownership.is_unique;
    annotation.is_borrow = ownership.is_borrow;

    return annotation;
}

TypeAnnotation TypeAnnotationParser::parse_named_type(Parser& parser, TypeOwnership ownership) {
    std::string name{};

    // Built-in type keywords (array, integer, …) and `none` map to their
    // canonical spelling via token_type_to_string().  `function` is also a type
    // keyword but is dispatched to parse_function_type() in parse() before this
    // branch is reached, so it never appears here.
    if (is_type_keyword(parser.current().type)) {
        name = token_type_to_string(parser.current().type);
        parser.advance();
    } else if (parser.current().type == TokenType::Identifier) {
        name = parser.current().lexeme;
        parser.advance();
        // Support namespace-qualified type names: Namespace.TypeName.
        while (parser.check(TokenType::Dot) && parser.check_at(1, TokenType::Identifier)) {
            parser.advance(); // consume '.'

            name += '.';
            name += parser.advance().lexeme; // append next segment
        }
    } else {
        parser.syntax_error(
            std::format("expected type annotation, got '{}'", parser.current().lexeme),
            parser.current().location,
            "valid types include boolean, integer, number, string, array<T>, etc.");
    }

    TypeAnnotation annotation{name};
    annotation.is_unique = ownership.is_unique;
    annotation.is_borrow = ownership.is_borrow;

    if (parser.consume_if(TokenType::Less)) {
        parser.parse_comma_list(annotation.type_params(), [&] { return parse(parser); });

        close_generic(parser);

        // Validate type argument count for built-in generic types.
        const auto count = annotation.type_params().size();

        if ((name == "array" || name == "optional" || name == "reference" || name == "queue" ||
             name == "stack" || name == "channel" || name == "task" || name == "set" ||
             name == "binary_tree" || name == "key_value_store") &&
            count != 1) {
            parser.syntax_error(
                std::format("type '{}' expects 1 type argument, got {}", name, count),
                parser.current().location, std::format("use {}<T>", name));
        }

        if (name == "dictionary" && count != 1) {
            parser.syntax_error(
                std::format("type 'dictionary' expects 1 type argument, got {}", count),
                parser.current().location, "use dictionary<V> (keys are always strings)");
        }

        if (name == "result" && count != 1 && count != 2) {
            parser.syntax_error(
                std::format("type 'result' expects 1 or 2 type arguments, got {}", count),
                parser.current().location, "use result<T> or result<T, E>");
        }
    }

    return annotation;
}

// Parses a generic type parameter list: <T>, <T: Bound>, <T: A + B>, <T, U: C>
// Returns the list of type parameters with optional interface bounds.
// Expects current token to be '<' on entry.
std::vector<TypeParam> TypeAnnotationParser::parse_type_param_list(Parser& parser) {
    parser.expect(TokenType::Less);

    std::vector<TypeParam> params;

    if (!parser.check(TokenType::Greater)) {
        parser.parse_comma_list(params, [&]() -> TypeParam {
            auto name = parser.expect(TokenType::Identifier).lexeme;

            std::vector<std::string> bounds;

            // Optional bounds: T: Interface1 + Interface2
            if (parser.consume_if(TokenType::Colon)) {
                bounds.push_back(parser.expect(TokenType::Identifier).lexeme);

                while (parser.consume_if(TokenType::Plus)) {
                    bounds.push_back(parser.expect(TokenType::Identifier).lexeme);
                }
            }

            return TypeParam{std::move(name), std::move(bounds)};
        });
    }

    parser.expect(TokenType::Greater);

    return params;
}

std::vector<TypeParam> TypeAnnotationParser::parse_optional_type_params(Parser& parser) {
    if (!parser.check(TokenType::Less)) {
        return {};
    }

    return parse_type_param_list(parser);
}

} // namespace luma
