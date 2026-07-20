#include <format>

#include "analysis/lexer/token_type.hpp"
#include "analysis/parser/parser.hpp"

namespace luma {

namespace {
constexpr std::string_view k_annotation_main = "main";
constexpr std::string_view k_annotation_test = "test";
} // namespace

// ──────────── Declaration parsing ────────────

DeclarationPtr Parser::parse_declaration() {
    auto guard = make_recursion_guard(current().location);

    if (check(TokenType::Annotation)) {
        return parse_annotated_function();
    }

    if (check(TokenType::Choice)) {
        return parse_choice_decl();
    }

    if (check(TokenType::Function)) {
        return parse_function_decl();
    }

    if (check(TokenType::Include)) {
        return parse_include_decl();
    }

    if (check(TokenType::Interface)) {
        return parse_interface_decl();
    }

    if (check(TokenType::Namespace)) {
        return parse_namespace_decl();
    }

    if (check(TokenType::Record)) {
        return parse_record_decl();
    }

    if (check(TokenType::Type)) {
        return parse_type_alias_decl();
    }

    if (check(TokenType::Use)) {
        return parse_use_decl();
    }

    if (check(TokenType::Internal)) {
        syntax_error("'internal' keyword is only allowed inside a namespace", current().location,
                     "wrap the declaration in a 'namespace Name { ... }' block");
    }

    syntax_error("expected declaration", current().location,
                 "a file must contain function, record, choice, or other declarations");
}

DeclarationPtr Parser::parse_annotated_function() {
    const auto& annotation_token = expect(TokenType::Annotation);
    const auto& annotation = annotation_token.lexeme;

    auto declaration = parse_function_decl();

    if (declaration->kind != DeclarationKind::Function) {
        syntax_error("expected a function declaration after annotation", annotation_token.location,
                     "annotations like @main and @test can only be applied to functions");
    }

    // Safe: the invariant check above throws if kind is not Function.
    auto* function = static_cast<FunctionDeclaration*>(declaration.get());

    if (annotation == k_annotation_main) {
        function->is_main = true;
    } else if (annotation == k_annotation_test) {
        function->is_test = true;
    } else {
        syntax_error(std::format("unknown annotation @{}", annotation), annotation_token.location,
                     "valid annotations are @main and @test");
    }

    return declaration;
}

DeclarationPtr Parser::parse_function_decl() {
    const auto location = current().location;

    expect(TokenType::Function);

    // Generic type parameter list: function<T, U> ...
    std::vector<TypeParam> type_params = parse_optional_type_params();

    // Return type (before name): function Type name(params) { ... }
    // A return type is always required. If the current token is an Identifier
    // immediately followed by '(' it means the return type was omitted.
    const bool missing_return_type =
        check(TokenType::Identifier) && check_at(1, TokenType::LeftParen);

    if (missing_return_type) {
        syntax_error(std::format("missing return type for function '{}'", current().lexeme),
                     current().location,
                     "add a return type before the function name, e.g. 'function void " +
                         std::string{current().lexeme} + "(...)'");
    }

    auto return_type = parse_type_annotation();

    const auto name = expect(TokenType::Identifier).lexeme;

    auto declaration = std::make_unique<FunctionDeclaration>(location, name);
    declaration->type_params = std::move(type_params);
    declaration->return_type = std::move(return_type);

    expect(TokenType::LeftParen);

    if (!check(TokenType::RightParen)) {
        declaration->parameters = parse_parameter_list();
    }

    recover_expect(TokenType::RightParen);

    declaration->body = parse_statement_block();

    return declaration;
}

std::vector<Parameter> Parser::parse_parameter_list() {
    std::vector<Parameter> parameters{};
    parse_comma_list(parameters, [this] { return parse_parameter(); });
    return parameters;
}

Parameter Parser::parse_parameter() {
    Parameter parameter;

    if (consume_if(TokenType::Mutable)) {
        parameter.is_mutable = true;
    }

    parameter.type = parse_type_annotation();
    parameter.name = expect(TokenType::Identifier).lexeme;

    if (consume_if(TokenType::Equals)) {
        parameter.default_value = parse_expression();
    }

    return parameter;
}

DeclarationPtr Parser::parse_record_decl() {
    const auto location = current().location;

    expect(TokenType::Record);

    const auto name = expect(TokenType::Identifier).lexeme;

    auto declaration = std::make_unique<RecordDeclaration>(location, name);

    // Generic type parameter list: record Box<T> { ... }
    declaration->type_params = parse_optional_type_params();

    declaration->fields = parse_bracketed(TokenType::LeftBrace, TokenType::RightBrace,
                                          [this] { return parse_field_list(true); });

    return declaration;
}

DeclarationPtr Parser::parse_choice_decl() {
    const auto location = current().location;

    expect(TokenType::Choice);

    const auto name = expect(TokenType::Identifier).lexeme;

    auto declaration = std::make_unique<ChoiceDeclaration>(location, name);

    // Generic type parameter list: choice Option<T> { ... }
    declaration->type_params = parse_optional_type_params();

    expect(TokenType::LeftBrace);

    while (!check(TokenType::RightBrace) && !is_at_end()) {
        ChoiceVariant variant;
        variant.name = expect(TokenType::Identifier).lexeme;

        // Variant with associated data: Variant(Type name, ...)
        if (consume_if(TokenType::LeftParen)) {
            if (!check(TokenType::RightParen)) {
                parse_comma_list(variant.fields, [this] { return parse_parameter(); });
            }

            recover_expect(TokenType::RightParen);
        }

        declaration->variants.push_back(std::move(variant));

        (void)consume_if(TokenType::Comma); // optional comma between variants
    }

    recover_expect(TokenType::RightBrace);

    return declaration;
}

DeclarationPtr Parser::parse_interface_decl() {
    const auto location = current().location;

    expect(TokenType::Interface);

    const auto name = expect(TokenType::Identifier).lexeme;

    auto declaration = std::make_unique<InterfaceDeclaration>(location, name);

    // Generic type parameter list: interface Container<T> { ... }
    declaration->type_params = parse_optional_type_params();

    declaration->fields = parse_bracketed(TokenType::LeftBrace, TokenType::RightBrace,
                                          [this] { return parse_field_list(false); });

    return declaration;
}

DeclarationPtr Parser::parse_namespace_decl() {
    const auto location = current().location;

    expect(TokenType::Namespace);

    const auto name = expect(TokenType::Identifier).lexeme;

    auto declaration = std::make_unique<NamespaceDeclaration>(location, name);

    expect(TokenType::LeftBrace);

    while (!check(TokenType::RightBrace) && !is_at_end()) {
        bool is_internal_member = false;

        if (check(TokenType::Internal)) {
            const auto internal_loc = current().location;

            advance(); // consume 'internal'

            is_internal_member = true;

            if (!check(TokenType::Function) && !check(TokenType::Record) &&
                !check(TokenType::Choice) && !check(TokenType::Interface) &&
                !check(TokenType::Type)) {
                syntax_error("'internal' must be followed by 'function', 'record', "
                             "'choice', 'interface', or 'type'",
                             internal_loc,
                             "use 'internal' only before a declaration inside a namespace");
            }
        }

        auto decl = parse_declaration();
        decl->is_internal_to_namespace = is_internal_member;

        declaration->declarations.push_back(std::move(decl));
    }

    recover_expect(TokenType::RightBrace);

    return declaration;
}

DeclarationPtr Parser::parse_type_alias_decl() {
    const auto location = current().location;

    expect(TokenType::Type);

    const auto name = expect(TokenType::Identifier).lexeme;

    // Generic type parameter list: type Option<T> = result<T>
    std::vector<TypeParam> type_params = parse_optional_type_params();

    expect(TokenType::Equals);

    auto target = parse_type_annotation();

    auto declaration = std::make_unique<TypeAliasDeclaration>(location, name, std::move(target));
    declaration->type_params = std::move(type_params);

    return declaration;
}

DeclarationPtr Parser::parse_include_decl() {
    const auto location = current().location;

    expect(TokenType::Include);

    const auto path = expect(TokenType::StringLiteral).lexeme;

    return std::make_unique<IncludeDeclaration>(location, path);
}

DeclarationPtr Parser::parse_use_decl() {
    const auto location = current().location;

    expect(TokenType::Use);

    std::string path{expect(TokenType::Identifier).lexeme};

    while (consume_if(TokenType::Dot)) {
        path += ".";
        path += expect(TokenType::Identifier).lexeme;
    }

    return std::make_unique<UseDeclaration>(location, path);
}

// ──────────── Shared helpers ────────────

std::vector<RecordField> Parser::parse_field_list(bool allow_defaults) {
    std::vector<RecordField> fields;

    parse_delimited_comma_list(TokenType::RightBrace, [&] {
        RecordField field;
        field.type = parse_type_annotation();
        field.name = expect(TokenType::Identifier).lexeme;

        if (allow_defaults && consume_if(TokenType::Equals)) {
            field.default_value = parse_expression();
        }

        fields.push_back(std::move(field));
    });

    return fields;
}

} // namespace luma
