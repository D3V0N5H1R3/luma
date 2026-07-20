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

; Interpolation delimiters — use editor-appropriate capture:
;   Zed: @string.special
(interpolation
  "${" @punctuation.special
  "}" @punctuation.special)

; ─── Numbers ───────────────────────────────────────────────────────

(integer_literal) @number
(float_literal) @number.float
(hex_literal) @number
(binary_literal) @number

; ─── Booleans ──────────────────────────────────────────────────────

(boolean_literal) @boolean

; ─── None ──────────────────────────────────────────────────────────

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

; Zed uses @keyword.control for all of these.

["if" "else" "match" "case"] @keyword.conditional
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

; ─── Operators ─────────────────────────────────────────────────────

["|>" "!>"] @operator
[".." "..="] @operator
"->" @operator
["??" "?." "?[" "?"] @operator
["&&" "||" "!"] @operator
["==" "!=" "<" ">" "<=" ">="] @operator
["=" "+=" "-=" "*=" "/=" "//=" "%=" "&=" "|=" "^=" "<<=" ">>=" "++" "--"] @operator
["+" "-" "*" "/" "//" "%"] @operator
["&" "|" "^" "~" "<<" ">>"] @operator

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

; ─── Punctuation ───────────────────────────────────────────────────

["(" ")" "[" "]" "{" "}"] @punctuation.bracket
["," ":" "."] @punctuation.delimiter

; ─── Fallback ──────────────────────────────────────────────────────

(identifier) @variable
