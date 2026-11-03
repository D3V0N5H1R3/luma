/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

/**
 * Tree-sitter grammar for the Luma programming language.
 *
 * Grammar rules are ordered from lowest to highest precedence where relevant.
 * Key design decisions:
 *  - String interpolation is handled inline via the `interpolation` rule.
 *  - Keywords are matched with word boundaries to avoid false positives.
 *  - Operators follow Luma's precedence table from the User Manual.
 */
module.exports = grammar({
    name: "luma",

    extras: ($) => [/\s/, $.comment],

    word: ($) => $.identifier,

    conflicts: ($) => [
        [$.return_statement],
        [$.record_literal, $._type_expression],
        [$.assignment_statement],
        [$.generic_type, $.function_type],
        [$.tuple_type],
        [$.block, $.dictionary_literal],
        [$._tuple_binding, $.lambda_parameter],
        [$.qualified_identifier, $.qualified_type],
        [$.record_pattern, $.record_literal, $._type_expression],
        [$.record_pattern, $._type_expression],
        [$.record_pattern, $.record_literal],
        [$._expression, $.call_expression],
    ],

    rules: {
        source_file: ($) => repeat($._top_level_statement),

        // ─── Top-level statements ─────────────────────────────────────────

        _top_level_statement: ($) =>
            choice(
                $.annotation_declaration,
                $.function_declaration,
                $.record_declaration,
                $.choice_declaration,
                $.interface_declaration,
                $.namespace_declaration,
                $.type_alias,
                $.include_statement,
                $.use_statement,
                $._statement,
            ),

        // ─── Comments ─────────────────────────────────────────────────────

        comment: (_$) => token(/#.*/),

        // ─── Annotations ──────────────────────────────────────────────────

        annotation: (_$) =>
            token(seq("@", choice("main", "test"))),

        annotation_declaration: ($) =>
            seq(
                $.annotation,
                $.function_declaration,
            ),

        // ─── Include / Use ────────────────────────────────────────────────

        include_statement: ($) =>
            seq("include", $.string),

        use_statement: ($) =>
            seq("use", choice($.qualified_identifier, $.type_identifier)),

        // ─── Type Alias ───────────────────────────────────────────────────

        type_alias: ($) =>
            seq(
                optional("internal"),
                "type",
                $.type_identifier,
                optional($.generic_params),
                "=",
                $._type_expression,
            ),

        // ─── Namespace ────────────────────────────────────────────────────

        namespace_declaration: ($) =>
            seq(
                optional("internal"),
                "namespace",
                $.type_identifier,
                "{",
                repeat($._top_level_statement),
                "}",
            ),

        // ─── Interface ────────────────────────────────────────────────────

        interface_declaration: ($) =>
            seq(
                optional("internal"),
                "interface",
                $.type_identifier,
                optional($.generic_params),
                "{",
                commaSep($.interface_field),
                optional(","),
                "}",
            ),

        interface_field: ($) =>
            seq($._type_expression, $.identifier),

        // ─── Record ───────────────────────────────────────────────────────

        record_declaration: ($) =>
            seq(
                optional("internal"),
                "record",
                $.type_identifier,
                optional($.generic_params),
                "{",
                commaSep($.record_field),
                optional(","),
                "}",
            ),

        record_field: ($) =>
            seq(
                $._type_expression,
                $.identifier,
                optional(seq("=", $._expression)),
            ),

        // ─── Choice ───────────────────────────────────────────────────────

        choice_declaration: ($) =>
            seq(
                optional("internal"),
                "choice",
                $.type_identifier,
                optional($.generic_params),
                "{",
                repeat(seq($.choice_variant, optional(","))),
                "}",
            ),

        choice_variant: ($) =>
            seq(
                $.type_identifier,
                optional(
                    seq("(", commaSep1(seq($._type_expression, $.identifier)), ")"),
                ),
            ),

        // ─── Function ─────────────────────────────────────────────────────

        function_declaration: ($) =>
            seq(
                optional("internal"),
                "function",
                optional($.generic_params),
                optional(choice("unique", "borrow")),
                $._type_expression,
                field("name", $.function_name),
                $.parameter_list,
                $.block,
            ),

        parameter_list: ($) =>
            seq("(", commaSep($.parameter), ")"),

        parameter: ($) =>
            seq(
                optional("mutable"),
                optional(choice("unique", "borrow")),
                $._type_expression,
                $.identifier,
                optional(seq("=", $._expression)),
            ),

        // ─── Statements ───────────────────────────────────────────────────

        _statement: ($) =>
            choice(
                $.variable_declaration,
                $.assignment_statement,
                $.expression_statement,
                $.return_statement,
                $.for_statement,
                $.while_statement,
                $.try_statement,
                $.break_statement,
                $.continue_statement,
                $.block,
            ),

        block: ($) => seq("{", repeat($._statement), "}"),

        // A tuple destructuring takes `mutable` once, before the parenthesised
        // bindings, never a type in front of them.
        variable_declaration: ($) =>
            seq(
                optional("mutable"),
                choice(
                    seq(
                        optional(choice("unique", "borrow")),
                        $._type_expression,
                        $.identifier,
                    ),
                    $.tuple_destructure,
                    alias($.record_pattern, $.record_destructure),
                ),
                "=",
                $._expression,
            ),

        tuple_destructure: ($) =>
            seq(
                "(",
                $._tuple_binding,
                repeat1(seq(",", $._tuple_binding)),
                ")",
            ),

        _tuple_binding: ($) => seq($._type_expression, $.identifier),

        assignment_statement: ($) =>
            seq(
                $._assignable,
                choice(
                    "=",
                    "+=",
                    "-=",
                    "*=",
                    "/=",
                    "%=",
                    "//=",
                    "++",
                    "--",
                ),
                optional($._expression),
            ),

        _assignable: ($) =>
            choice(
                $.identifier,
                $.index_expression,
                $.field_expression,
            ),

        expression_statement: ($) => $._expression,

        return_statement: ($) =>
            seq("return", optional($._expression)),

        break_statement: (_$) => "break",

        continue_statement: (_$) => "continue",

        // ─── Control Flow ─────────────────────────────────────────────────

        // `if` is an expression (User Manual §32.7 `if_expr`); at statement
        // level it appears as an expression_statement.
        if_expression: ($) =>
            prec.right(
                seq(
                    "if",
                    $._expression,
                    $.block,
                    repeat(seq("else", "if", $._expression, $.block)),
                    optional(seq("else", $.block)),
                ),
            ),

        for_statement: ($) =>
            seq(
                "for",
                choice(
                    seq($.identifier, ",", $.identifier, "in", $._expression),
                    seq($.identifier, "in", $._expression),
                ),
                $.block,
            ),

        while_statement: ($) =>
            seq("while", $._expression, $.block),

        try_statement: ($) =>
            seq(
                "try",
                $.block,
                optional(seq("catch", "(", $.identifier, ")", $.block)),
                optional(seq("finally", $.block)),
            ),

        // ─── Expressions ──────────────────────────────────────────────────

        _expression: ($) =>
            choice(
                $.pipe_expression,
                $.binary_expression,
                $.unary_expression,
                $.postfix_expression,
                $.call_expression,
                $.index_expression,
                $.field_expression,
                $.optional_chain_expression,
                $.if_expression,
                $.match_expression,
                $.lambda_expression,
                $.spawn_expression,
                $.await_expression,
                $.task_scope_expression,
                $.with_expression,
                $.range_expression,
                $.string,
                $.triple_string,
                $.number_literal,
                $.boolean_literal,
                $.none_literal,
                $.array_literal,
                $.dictionary_literal,
                $.tuple_literal,
                $.record_literal,
                $.constructor_expression,
                $.downcast_expression,
                $.trusted_downcast_expression,
                $.is_expression,
                $.qualified_identifier,
                $.identifier,
                seq("(", $._expression, ")"),
            ),

        pipe_expression: ($) =>
            prec.left(
                1,
                seq(
                    $._expression,
                    choice("|>", "!>"),
                    $._expression,
                ),
            ),

        binary_expression: ($) =>
            choice(
                prec.left(2, seq($._expression, "??", $._expression)),
                prec.left(3, seq($._expression, "||", $._expression)),
                prec.left(4, seq($._expression, "&&", $._expression)),
                prec.left(5, seq($._expression, choice("==", "!="), $._expression)),
                prec.left(
                    6,
                    seq(
                        $._expression,
                        choice("<", ">", "<=", ">=", "in"),
                        $._expression,
                    ),
                ),
                prec.left(
                    11,
                    seq($._expression, choice("+", "-"), $._expression),
                ),
                prec.left(
                    12,
                    seq(
                        $._expression,
                        choice("*", "/", "//", "%"),
                        $._expression,
                    ),
                ),
            ),

        unary_expression: ($) =>
            prec(13, seq(choice("!", "-"), $._expression)),

        postfix_expression: ($) =>
            prec(14, seq($._expression, "?")),

        // A stored closure can be called through a field (`config.handler(x)`),
        // and a returned function can be called in place (`make_adder(1)(2)`).
        // The interpreter starts a new statement at a `(` that begins a line,
        // but the grammar cannot see line breaks without an external scanner.
        // So these calls carry no static precedence: the parser keeps both
        // readings and drops the call when the next line is, say, a tuple
        // destructuring.
        call_expression: ($) =>
            choice(
                prec(
                    15,
                    seq(
                        field("function", choice($.identifier, $.qualified_identifier)),
                        $._call_arguments,
                    ),
                ),
                prec.dynamic(
                    1,
                    seq(
                        field("function", choice($.field_expression, $.call_expression)),
                        $._call_arguments,
                    ),
                ),
            ),

        _call_arguments: ($) =>
            seq(
                optional(seq("::", $.generic_args)),
                "(",
                commaSep($.argument),
                ")",
            ),

        argument: ($) =>
            choice(
                seq($.identifier, ":", $._expression),
                $._expression,
            ),

        index_expression: ($) =>
            prec(15, seq($._expression, "[", $._expression, "]")),

        field_expression: ($) =>
            prec(
                15,
                seq(
                    $._expression,
                    ".",
                    choice($.identifier, $.integer_literal),
                ),
            ),

        optional_chain_expression: ($) =>
            prec(
                15,
                seq(
                    $._expression,
                    choice(
                        seq("?.", choice($.identifier, $.integer_literal)),
                        seq("?[", $._expression, "]"),
                    ),
                ),
            ),

        // The interpreter accepts `else` anywhere among the arms (it always
        // wins), so the grammar does not force it last.
        match_expression: ($) =>
            seq(
                "match",
                $._expression,
                "{",
                repeat(choice($.match_arm, $.else_arm)),
                "}",
            ),

        match_arm: ($) =>
            choice(
                seq(
                    "case",
                    $._case_pattern,
                    repeat(seq("|", $._case_pattern)),
                    optional($.match_guard),
                    $.block,
                ),
                seq(
                    choice("success", "failure"),
                    "(",
                    $.identifier,
                    ")",
                    optional($.match_guard),
                    $.block,
                ),
            ),

        _case_pattern: ($) =>
            choice(
                $.match_pattern,
                $.comparison_pattern,
                $.record_pattern,
                $._expression,
            ),

        // `case >= 90 { … }` — compares the match subject with an expression.
        comparison_pattern: ($) =>
            seq(choice("==", "!=", "<", ">", "<=", ">="), $._expression),

        // `case Point { x, y } { … }` — binds record fields by name. The same
        // tokens also read as a shorthand record literal, so prefer the pattern.
        record_pattern: ($) =>
            prec.dynamic(
                1,
                seq(
                    choice($.type_identifier, $.qualified_type),
                    optional($.generic_args),
                    "{",
                    commaSep($.identifier),
                    optional(","),
                    "}",
                ),
            ),

        // `when` is a contextual keyword: it only refines a match arm.
        match_guard: ($) => seq("when", $._expression),

        match_pattern: ($) =>
            prec(
                1,
                seq(
                    $.qualified_identifier,
                    optional(seq("(", commaSep($.identifier), ")")),
                ),
            ),

        else_arm: ($) =>
            seq("else", $.block),

        lambda_expression: ($) =>
            prec.right(
                -1,
                seq(
                    "(",
                    commaSep($.lambda_parameter),
                    ")",
                    "->",
                    choice($._expression, $.block),
                ),
            ),

        lambda_parameter: ($) =>
            seq(
                optional("mutable"),
                optional(choice("unique", "borrow")),
                $._type_expression,
                $.identifier,
            ),

        spawn_expression: ($) =>
            prec(20, seq("spawn", $.call_expression)),

        await_expression: ($) =>
            prec(20, seq("await", $._expression)),

        task_scope_expression: ($) =>
            seq("task_scope", "{", repeat($._statement), "}"),

        with_expression: ($) =>
            prec.left(
                16,
                seq(
                    $._expression,
                    "with",
                    "{",
                    commaSep(seq($.identifier, "=", $._expression)),
                    optional(","),
                    "}",
                ),
            ),

        range_expression: ($) =>
            prec.left(
                seq(
                    $._expression,
                    choice("..", "..="),
                    $._expression,
                ),
            ),

        constructor_expression: ($) =>
            seq(
                choice("success", "failure", "some"),
                "(",
                optional($._expression),
                ")",
            ),

        downcast_expression: ($) =>
            seq("downcast", "<", $._type_expression, ">", "(", $._expression, ")"),

        trusted_downcast_expression: ($) =>
            seq("trusted_downcast", "<", $._type_expression, ">", "(", $._expression, ")"),

        is_expression: ($) =>
            seq("is", "<", $._type_expression, ">", "(", $._expression, ")"),

        // A field initialiser may use the shorthand `Vector { x, y }` for
        // `Vector { x = x, y = y }`.
        record_literal: ($) =>
            seq(
                choice($.type_identifier, $.qualified_type),
                optional($.generic_args),
                "{",
                commaSep(choice(seq($.identifier, "=", $._expression), $.identifier)),
                optional(","),
                "}",
            ),

        array_literal: ($) =>
            seq("[", commaSep($._expression), optional(","), "]"),

        dictionary_literal: ($) =>
            seq(
                "{",
                commaSep(seq($.string, ":", $._expression)),
                optional(","),
                "}",
            ),

        tuple_literal: ($) =>
            seq(
                "(",
                $._expression,
                ",",
                $._expression,
                optional(repeat(seq(",", $._expression))),
                ")",
            ),

        // ─── Type Expressions ─────────────────────────────────────────────

        _type_expression: ($) =>
            choice(
                $.builtin_type,
                $.generic_type,
                $.function_type,
                $.tuple_type,
                $.qualified_type,
                $.type_identifier,
            ),

        builtin_type: (_$) =>
            choice(
                "boolean",
                "integer",
                "number",
                "decimal",
                "string",
                "void",
                "array",
                "dictionary",
                "optional",
                "reference",
                "result",
                "task",
                "channel",
                "socket",
                "queue",
                "stack",
                "key_value_store",
                "set",
                "xml",
                "none",
            ),

        generic_type: ($) =>
            seq($._type_expression, "<", commaSep1($._type_expression), ">"),

        function_type: ($) =>
            seq(
                "function",
                "(",
                commaSep($._type_expression),
                ")",
                "->",
                $._type_expression,
            ),

        tuple_type: ($) =>
            seq("(", $._type_expression, ",", commaSep1($._type_expression), ")"),

        generic_params: ($) =>
            seq(
                "<",
                commaSep1(
                    seq(
                        $.type_identifier,
                        optional(
                            seq(
                                ":",
                                $.type_identifier,
                                repeat(seq("+", $.type_identifier)),
                            ),
                        ),
                    ),
                ),
                ">",
            ),

        generic_args: ($) =>
            seq("<", commaSep1($._type_expression), ">"),

        // ─── Literals ─────────────────────────────────────────────────────

        // A `$` that does not start `${` is literal text (the interpreter only
        // interpolates on `${`). The lexer prefers the longer `${` token, so this
        // single-character alternative never swallows an interpolation start.
        string: ($) =>
            seq(
                '"',
                repeat(
                    choice(
                        $.string_content,
                        alias($._string_dollar, $.string_content),
                        $.string_escape,
                        $.interpolation,
                    ),
                ),
                '"',
            ),

        triple_string: ($) =>
            seq(
                '"""',
                repeat(
                    choice(
                        $.triple_string_content,
                        alias(token.immediate('"'), $.triple_string_content),
                        alias($._string_dollar, $.triple_string_content),
                        $.string_escape,
                        $.interpolation,
                    ),
                ),
                '"""',
            ),

        _string_dollar: (_$) => token.immediate("$"),

        string_content: (_$) =>
            token.immediate(prec(1, /[^"\\$\n]+/)),

        triple_string_content: (_$) =>
            token.immediate(prec(1, /[^"\\$]+/)),

        string_escape: (_$) =>
            token.immediate(seq("\\", /[ntr0\\"$]/)),

        interpolation: ($) =>
            seq("${", $._expression, "}"),

        number_literal: ($) =>
            choice(
                $.hex_literal,
                $.octal_literal,
                $.binary_literal,
                $.float_literal,
                $.integer_literal,
            ),

        hex_literal: (_$) =>
            token(/0[xX][0-9a-fA-F]+/),

        octal_literal: (_$) =>
            token(/0[oO][0-7]+/),

        binary_literal: (_$) =>
            token(/0[bB][01]+/),

        float_literal: (_$) =>
            token(
                choice(
                    /[0-9]+\.[0-9]+([eE][+\-]?[0-9]+)?/,
                    /[0-9]+[eE][+\-]?[0-9]+/,
                ),
            ),

        integer_literal: (_$) =>
            token(/[0-9]+/),

        boolean_literal: (_$) =>
            choice("true", "false"),

        // `none` is also a type name (`result<none>`); where both readings fit
        // (statement start), the literal wins.
        none_literal: (_$) => prec(1, "none"),

        // ─── Identifiers ──────────────────────────────────────────────────

        identifier: (_$) =>
            token(/[a-z_][a-zA-Z0-9_]*/),

        type_identifier: (_$) =>
            token(/[A-Z][a-zA-Z0-9_]*/),

        function_name: ($) => $.identifier,

        // `Module.member`, `Type.Variant`, or `Namespace.Type.Variant`.
        qualified_identifier: ($) =>
            seq(
                $.type_identifier,
                ".",
                optional(seq($.type_identifier, ".")),
                choice($.identifier, $.type_identifier),
            ),

        // A namespace-qualified type name such as `DateTime.TimeParts`.
        qualified_type: ($) =>
            seq(
                $.type_identifier,
                ".",
                $.type_identifier,
                optional(seq(".", $.type_identifier)),
            ),
    },
});

/**
 * Helper: zero or more comma-separated items.
 * @param {RuleOrLiteral} rule
 * @returns {SeqRule}
 */
function commaSep(rule) {
    return optional(commaSep1(rule));
}

/**
 * Helper: one or more comma-separated items.
 * @param {RuleOrLiteral} rule
 * @returns {SeqRule}
 */
function commaSep1(rule) {
    return seq(rule, repeat(seq(",", rule)));
}
