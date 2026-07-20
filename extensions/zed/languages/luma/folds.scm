; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Fold Queries
;
; Defines foldable regions for the Zed editor.
; ═══════════════════════════════════════════════════════════════════

; ─── Blocks ────────────────────────────────────────────────────────

(block
  "{" @fold.start
  "}" @fold.end)

; ─── Declarations ──────────────────────────────────────────────────

(record_declaration
  "{" @fold.start
  "}" @fold.end)

(choice_declaration
  "{" @fold.start
  "}" @fold.end)

(interface_declaration
  "{" @fold.start
  "}" @fold.end)

(namespace_declaration
  "{" @fold.start
  "}" @fold.end)

; ─── Expressions ───────────────────────────────────────────────────

(match_expression
  "{" @fold.start
  "}" @fold.end)

(task_scope_expression
  "{" @fold.start
  "}" @fold.end)

; ─── Array / Dictionary Literals ───────────────────────────────────

(array_literal
  "[" @fold.start
  "]" @fold.end)

(dictionary_literal
  "{" @fold.start
  "}" @fold.end)

; ─── Comments ──────────────────────────────────────────────────────

; Consecutive line comments can be folded as a group.
(comment) @fold
