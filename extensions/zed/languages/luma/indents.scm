; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Indentation Queries
;
; Controls automatic indentation in the Zed editor.
; ═══════════════════════════════════════════════════════════════════

; Increase indent inside blocks opened by {
(block) @indent

; Decrease indent for the closing }
(block
  "}" @outdent)

; Record declarations
(record_declaration
  "{" @indent
  "}" @outdent)

; Choice declarations
(choice_declaration
  "{" @indent
  "}" @outdent)

; Interface declarations
(interface_declaration
  "{" @indent
  "}" @outdent)

; Namespace declarations
(namespace_declaration
  "{" @indent
  "}" @outdent)

; Match expressions
(match_expression
  "{" @indent
  "}" @outdent)

; Dictionary literals
(dictionary_literal
  "{" @indent
  "}" @outdent)

; NOTE: for_statement, while_statement, if_statement, and try_statement
; all use (block) nodes, which are already handled by the generic (block)
; @indent rule above.  Listing them again would cause double indentation.

; Task scope
(task_scope_expression
  "{" @indent
  "}" @outdent)

; Lambda expressions with blocks
(lambda_expression
  (block) @indent)

; Array literals spanning multiple lines
(array_literal
  "[" @indent
  "]" @outdent)
