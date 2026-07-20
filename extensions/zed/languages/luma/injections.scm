; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Injection Queries
;
; Enables syntax highlighting for embedded languages inside strings.
; Currently handles regex patterns in RegularExpression.* calls.
;
; Only the *pattern* operand is injected — never the subject text or a
; replacement string. In the RegularExpression API the pattern is the
; second argument for find / find_all / matches / replace / replace_all /
; split, and the sole argument for is_valid, so each case is anchored
; positionally with a dedicated pattern below.
; ═══════════════════════════════════════════════════════════════════

; Pattern operand = second argument.
; Example: RegularExpression.matches(text, "[a-z]+\\d{2,}")
;          RegularExpression.replace(text, "[a-z]+", "X")   ← "X" is NOT a regex
(call_expression
  function: (qualified_identifier
    (type_identifier) @_module
    (identifier) @_method)
  "("
  .
  (argument)
  .
  ","
  .
  (argument
    (string
      (string_content) @injection.content))
  (#eq? @_module "RegularExpression")
  (#match? @_method "^(find|find_all|matches|replace|replace_all|split)$")
  (#set! injection.language "regex"))

; Pattern operand = sole argument.
; Example: RegularExpression.is_valid("[a-z]+")
(call_expression
  function: (qualified_identifier
    (type_identifier) @_module
    (identifier) @_method)
  "("
  .
  (argument
    (string
      (string_content) @injection.content))
  (#eq? @_module "RegularExpression")
  (#eq? @_method "is_valid")
  (#set! injection.language "regex"))
