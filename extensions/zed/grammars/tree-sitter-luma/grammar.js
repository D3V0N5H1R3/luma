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
            seq("use", $.qualified_identifier),

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
                repeat($.interface_field),
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
                repeat($.choice_variant),
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
                $.if_statement,
                $.for_statement,
                $.while_statement,
                $.try_statement,
                $.break_statement,
                $.continue_statement,
                $.block,
            ),

        block: ($) => seq("{", repeat($._statement), "}"),

        variable_declaration: ($) =>
            seq(
                optional("mutable"),
                optional(choice("unique", "borrow")),
                $._type_expression,
                choice($.identifier, $.tuple_destructure),
                "=",
                $._expression,
            ),

        tuple_destructure: ($) =>
            seq(
                "(",
                commaSep2(seq(optional("mutable"), $._type_expression, $.identifier)),
                ")",
            ),

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
                    "&=",
                    "|=",
                    "^=",
                    "<<=",
                    ">>=",
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

        if_statement: ($) =>
            seq(
                "if",
                $._expression,
                $.block,
                optional(
                    repeat(seq("else", "if", $._expression, $.block)),
                ),
                optional(seq("else", $.block)),
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
                prec.left(7, seq($._expression, "|", $._expression)),
                prec.left(8, seq($._expression, "^", $._expression)),
                prec.left(9, seq($._expression, "&", $._expression)),
                prec.left(10, seq($._expression, choice("<<", ">>"), $._expression)),
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
            prec(13, seq(choice("!", "-", "~"), $._expression)),

        postfix_expression: ($) =>
            prec(14, seq($._expression, "?")),

        call_expression: ($) =>
            prec(
                15,
                seq(
                    field("function", choice($.identifier, $.qualified_identifier)),
                    optional(seq("::", $.generic_args)),
                    "(",
                    commaSep($.argument),
                    ")",
                ),
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

        match_expression: ($) =>
            seq(
                "match",
                $._expression,
                "{",
                repeat($.match_arm),
                optional($.else_arm),
                "}",
            ),

        match_arm: ($) =>
            choice(
                seq(
                    "case",
                    choice($.match_pattern, $._expression),
                    $.block,
                ),
                seq(
                    choice("success", "failure"),
                    "(",
                    $.identifier,
                    ")",
                    $.block,
                ),
            ),

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
            seq($._type_expression, $.identifier),

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

        record_literal: ($) =>
            seq(
                $.type_identifier,
                optional($.generic_args),
                "{",
                commaSep(seq($.identifier, "=", $._expression)),
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
                "binary_tree",
                "key_value_store",
                "set",
                "widget",
                "xml",
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

        string: ($) =>
            seq(
                '"',
                repeat(
                    choice(
                        $.string_content,
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
                        $.string_escape,
                        $.interpolation,
                    ),
                ),
                '"""',
            ),

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
                $.binary_literal,
                $.float_literal,
                $.integer_literal,
            ),

        hex_literal: (_$) =>
            token(/0[xX][0-9a-fA-F]+/),

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

        none_literal: (_$) => "none",

        // ─── Identifiers ──────────────────────────────────────────────────

        identifier: (_$) =>
            token(/[a-z_][a-zA-Z0-9_]*/),

        type_identifier: (_$) =>
            token(/[A-Z][a-zA-Z0-9_]*/),

        function_name: ($) => $.identifier,

        qualified_identifier: ($) =>
            seq($.type_identifier, ".", choice($.identifier, $.type_identifier)),
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

/**
 * Helper: two or more comma-separated items.
 * @param {RuleOrLiteral} rule
 * @returns {SeqRule}
 */
function commaSep2(rule) {
    return seq(rule, ",", rule, repeat(seq(",", rule)));
}
