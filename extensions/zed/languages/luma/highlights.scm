; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Highlight Queries
;
; Maps tree-sitter node types to standard highlight names.
; These names correspond to Zed theme highlight categories.
;
; Canonical source: extensions/shared/queries/highlights.scm
; This file adapts highlight groups to Zed conventions.
; When the grammar changes, update the canonical file first.
;
; Precedence: when several patterns capture the same node, Zed applies the
; LAST one. General fallbacks therefore come first and specific patterns
; after them; a fallback at the end of the file would override everything.
; ═══════════════════════════════════════════════════════════════════

; ─── Fallbacks ─────────────────────────────────────────────────────

(identifier) @variable

(type_identifier) @type

; ─── Comments ──────────────────────────────────────────────────────

(comment) @comment

; ─── Annotations ───────────────────────────────────────────────────

(annotation) @attribute

; ─── Punctuation ───────────────────────────────────────────────────

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

[
  ","
  ":"
  "."
] @punctuation.delimiter

; ─── Strings ───────────────────────────────────────────────────────

(string) @string
(triple_string) @string

(string_content) @string
(triple_string_content) @string

(string_escape) @string.escape

(interpolation
  "${" @string.special
  "}" @string.special)

; ─── Numbers ───────────────────────────────────────────────────────

(hex_literal) @number
(octal_literal) @number
(binary_literal) @number
(float_literal) @number
(integer_literal) @number

; ─── Boolean and Null Constants ────────────────────────────────────

(boolean_literal) @constant.builtin
(none_literal) @constant.builtin

; ─── Keywords — Control Flow ───────────────────────────────────────

[
  "if"
  "else"
  "for"
  "while"
  "in"
  "return"
  "match"
  "case"
  "when"
  "try"
  "catch"
  "finally"
] @keyword.control

; break and continue are named nodes (break_statement / continue_statement),
; not anonymous keyword terminals, so they must be matched separately.

(break_statement) @keyword.control
(continue_statement) @keyword.control

; ─── Keywords — Declarations ───────────────────────────────────────

[
  "function"
  "record"
  "choice"
  "interface"
  "namespace"
  "type"
  "include"
  "use"
  "spawn"
  "await"
  "task_scope"
] @keyword

; ─── Keywords — Modifiers ──────────────────────────────────────────

[
  "mutable"
  "unique"
  "borrow"
  "internal"
] @keyword.modifier

; ─── Keywords — Other ──────────────────────────────────────────────

"with" @keyword

; downcast / trusted_downcast / is act as operator-style keywords

(downcast_expression "downcast" @keyword.operator)
(trusted_downcast_expression "trusted_downcast" @keyword.operator)
(is_expression "is" @keyword.operator)

; ─── Operators ─────────────────────────────────────────────────────

[
  "|>"
  "!>"
] @operator

[
  ".."
  "..="
] @operator

"->" @operator

[
  "??"
  "?."
  "?["
  "?"
] @operator

[
  "&&"
  "||"
  "!"
] @operator

[
  "=="
  "!="
  "<"
  ">"
  "<="
  ">="
] @operator

[
  "="
  "+="
  "-="
  "*="
  "/="
  "%="
  "//="
  "++"
  "--"
] @operator

[
  "+"
  "-"
  "*"
  "/"
  "//"
  "%"
] @operator

; `|` separates alternative patterns in a match arm (e.g. `case A | B`).
; Luma has no bitwise operators — bitwise work uses the `Bits` module.
"|" @operator

; ─── Constructors ──────────────────────────────────────────────────

(constructor_expression
  [
    "success"
    "failure"
    "some"
  ] @constructor)

; Result/optional match arms: success(binding) / failure(binding)
(match_arm
  [
    "success"
    "failure"
  ] @constructor)

; ─── Built-in Types ────────────────────────────────────────────────

(builtin_type) @type.builtin

; ─── Generic Parameters ────────────────────────────────────────────

(generic_params
  "<" @punctuation.bracket
  (type_identifier) @type.parameter
  ">" @punctuation.bracket)

(generic_args
  "<" @punctuation.bracket
  ">" @punctuation.bracket)

; ─── Entity Names — Type Declarations ──────────────────────────────

(record_declaration
  (type_identifier) @type)

(choice_declaration
  (type_identifier) @type)

(interface_declaration
  (type_identifier) @type)

(namespace_declaration
  (type_identifier) @type)

(type_alias
  (type_identifier) @type)

; ─── Choice Variant Names ──────────────────────────────────────────

(choice_variant
  (type_identifier) @constructor)

; ─── Variables ─────────────────────────────────────────────────────

(variable_declaration
  (identifier) @variable)

(tuple_destructure
  (identifier) @variable)

; Record destructuring binds fields by name: Line { start, finish } = segment
(record_destructure
  (identifier) @variable)

; Record pattern bindings in a match arm: case Point { x, y } { }
(record_pattern
  (identifier) @variable)

(parameter
  (identifier) @variable.parameter)

(lambda_parameter
  (identifier) @variable.parameter)

; Catch clause variable: catch(error) { }
(try_statement
  "catch"
  "("
  (identifier) @variable
  ")")

; Match arm result binding: success(val) / failure(err)
(match_arm
  "("
  (identifier) @variable
  ")")

; Single loop variable: for x in iterable
(for_statement
  "for" .
  (identifier) @variable.other.loop
  . "in")

; Two loop variables: for key, value in dictionary
(for_statement
  "for" .
  (identifier) @variable.other.loop
  . ","
  . (identifier) @variable.other.loop
  . "in")

; ─── Record Fields ─────────────────────────────────────────────────

(record_field
  (identifier) @property)

(interface_field
  (identifier) @property)

(field_expression
  "." .
  (identifier) @property)

(optional_chain_expression
  "?." .
  (identifier) @property)

; Record literal field names: Point { x = 1 }
(record_literal
  (identifier) @property . "=")

; With-expression field names: p with { x = 1 }
(with_expression
  (identifier) @property . "=")

; Named arguments: f(name: value)
(argument
  (identifier) @variable.parameter
  ":")

; ─── Module / Qualified Identifiers ────────────────────────────────

(qualified_type
  (type_identifier) @type)

(qualified_identifier
  (type_identifier) @type)

(qualified_identifier
  (identifier) @function.call)

; ─── Call Expressions ──────────────────────────────────────────────

(call_expression
  function: (identifier) @function.call)

(call_expression
  function: (qualified_identifier
    (type_identifier) @type
    (identifier) @function.call))

; A closure stored in a record field: config.handler(x)
(call_expression
  function: (field_expression
    "." .
    (identifier) @function.call))

; Built-in functions
((call_expression
  function: (identifier) @function.builtin)
  (#match? @function.builtin "^(print|assert|type_of)$"))

; ─── Entity Names — Function Declarations ──────────────────────────

; function_name wraps an identifier; capture both so the inner identifier is
; not left to the @variable fallback.
(function_declaration
  name: (function_name) @function)

(function_name
  (identifier) @function)
