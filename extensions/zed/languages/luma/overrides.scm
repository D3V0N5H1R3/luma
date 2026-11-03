; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Override Queries
;
; Names the syntax scopes that config.toml refers to. The `brackets`
; entries use `not_in = ["string", "comment"]` so typing `"` inside a
; string or a comment does not auto-insert a closing quote.
; ═══════════════════════════════════════════════════════════════════

(comment) @comment.inclusive

[
  (string)
  (triple_string)
] @string
