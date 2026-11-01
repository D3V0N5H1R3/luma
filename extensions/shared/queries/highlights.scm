; ═══════════════════════════════════════════════════════════════════
; Luma — Canonical Tree-sitter Highlight Queries
;
; This is the canonical source of truth for Luma syntax highlighting.
; The editor-specific copy (Zed) adapts highlight group names
; to its native conventions but should match this file structurally.
;
; The editor-specific copy is validated against this file by sync-queries.py.
; Run: python sync-queries.py --check  (fails on structural drift)
; Run: python sync-queries.py --force  (creates a missing editor copy only;
;                                        it never overwrites an existing one)
;
; When the Luma grammar changes, update this file first, then
; propagate to:
;   - extensions/zed/languages/luma/highlights.scm
;
; Precedence: when several patterns capture the same node, the LAST one
; wins (the Zed and Neovim convention). General fallbacks therefore come
; first and specific patterns after them.
; ═══════════════════════════════════════════════════════════════════

; ─── Fallbacks ─────────────────────────────────────────────────────

(identifier) @variable
(type_identifier) @type

; ─── Comments ──────────────────────────────────────────────────────

(comment) @comment

; ─── Annotations ───────────────────────────────────────────────────

(annotation) @attribute

; ─── Punctuation ───────────────────────────────────────────────────

["(" ")" "[" "]" "{" "}"] @punctuation.bracket
["," ":" "."] @punctuation.delimiter

; ─── Strings ───────────────────────────────────────────────────────

(string) @string
(triple_string) @string

(string_content) @string
(triple_string_content) @string

(string_escape) @string.escape

; Interpolation delimiters — use editor-appropriate capture:
;   Zed: @string.special
(interpolation
  "${" @punctuation.special
  "}" @punctuation.special)

; ─── Numbers ───────────────────────────────────────────────────────

(integer_literal) @number
(float_literal) @number.float
(hex_literal) @number
(octal_literal) @number
(binary_literal) @number

; ─── Booleans ──────────────────────────────────────────────────────

(boolean_literal) @boolean

; ─── None ──────────────────────────────────────────────────────────

(none_literal) @constant.builtin

; ─── Keywords — Control Flow ───────────────────────────────────────

; Zed uses @keyword.control for all of these.

["if" "else" "match" "case" "when"] @keyword.conditional
["for" "while" "in"] @keyword.repeat
(break_statement) @keyword.repeat
(continue_statement) @keyword.repeat
["return"] @keyword.return
["try" "catch" "finally"] @keyword.exception
["spawn" "await" "task_scope"] @keyword.coroutine

; ─── Keywords — Declarations ───────────────────────────────────────

["function" "record" "choice" "interface" "namespace" "type" "include" "use"] @keyword

; ─── Keywords — Modifiers ──────────────────────────────────────────

["mutable" "unique" "borrow" "internal"] @keyword.modifier

; ─── Keywords — Other ──────────────────────────────────────────────

"with" @keyword

(downcast_expression "downcast" @keyword.operator)
(trusted_downcast_expression "trusted_downcast" @keyword.operator)
(is_expression "is" @keyword.operator)

; ─── Operators ─────────────────────────────────────────────────────

["|>" "!>"] @operator
[".." "..="] @operator
"->" @operator
["??" "?." "?[" "?"] @operator
["&&" "||" "!"] @operator
["==" "!=" "<" ">" "<=" ">="] @operator
["=" "+=" "-=" "*=" "/=" "//=" "%=" "++" "--"] @operator
["+" "-" "*" "/" "//" "%"] @operator
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
  (type_identifier) @module)

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
