; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Highlight Queries
;
; Maps tree-sitter node types to standard highlight names.
; These names correspond to Zed theme highlight categories.
;
; Canonical source: extensions/shared/queries/highlights.scm
; This file adapts highlight groups to Zed conventions.
; When the grammar changes, update the canonical file first.
; ═══════════════════════════════════════════════════════════════════

; ─── Comments ──────────────────────────────────────────────────────

(comment) @comment

; ─── Annotations ───────────────────────────────────────────────────

(annotation) @attribute

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
(binary_literal) @number
(float_literal) @number
(integer_literal) @number

; ─── Boolean and Null Constants ────────────────────────────────────

(boolean_literal) @constant.builtin
(none_literal) @constant.builtin

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

; ─── Built-in Types ────────────────────────────────────────────────

(builtin_type) @type.builtin

; ─── Entity Names — Function Declarations ──────────────────────────

(function_declaration
  name: (function_name) @function)

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

; ─── Module / Qualified Identifiers ────────────────────────────────

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

; Built-in functions
((call_expression
  function: (identifier) @function.builtin)
  (#match? @function.builtin "^(print|assert|type_of)$"))

; ─── Variables ─────────────────────────────────────────────────────

(variable_declaration
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
  "&="
  "|="
  "^="
  "<<="
  ">>="
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

[
  "&"
  "|"
  "^"
  "~"
  "<<"
  ">>"
] @operator

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

; ─── Generic Parameters ────────────────────────────────────────────

(generic_params
  "<" @punctuation.bracket
  (type_identifier) @type.parameter
  ">" @punctuation.bracket)

(generic_args
  "<" @punctuation.bracket
  ">" @punctuation.bracket)

; ─── Type Identifiers ──────────────────────────────────────────────

(type_identifier) @type

; ─── Named Arguments ──────────────────────────────────────────────

(argument
  (identifier) @variable.parameter
  ":")

; ─── Record Literal Field Names ────────────────────────────────────

(record_literal
  (identifier) @property . "=")

; ─── With-expression Field Names ────────────────────────────────────

(with_expression
  (identifier) @property . "=")

; ─── Tuple Destructure Variables ────────────────────────────────────

(tuple_destructure
  (identifier) @variable)

; ─── Fallback ──────────────────────────────────────────────────────

(identifier) @variable
