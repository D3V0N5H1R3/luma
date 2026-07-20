; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Redaction Queries
;
; Redacts sensitive values during screen-sharing collaboration.
; String literals and number literals in variable declarations and
; record fields are replaced with placeholder blocks.
; ═══════════════════════════════════════════════════════════════════

; Redact string and number values in variable declarations
(variable_declaration
  (string) @redact)

(variable_declaration
  (number_literal) @redact)

; Redact string and number values in record literals
(record_literal
  (string) @redact)

(record_literal
  (number_literal) @redact)

; Redact string and number values in array literals
(array_literal
  (string) @redact)

(array_literal
  (number_literal) @redact)

; Redact string and number values in dictionary literals
(dictionary_literal
  (string) @redact)

(dictionary_literal
  (number_literal) @redact)

; Redact string arguments in function calls
(argument
  (string) @redact)

(argument
  (number_literal) @redact)
