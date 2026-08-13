; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Text Object Queries
;
; Defines text objects for Zed's vim-mode and structural selection.
; Supported captures: function.around/inside, class.around/inside,
; comment.around/inside.
; ═══════════════════════════════════════════════════════════════════

; ─── Functions ─────────────────────────────────────────────────────

(function_declaration) @function.around

(function_declaration
  (block) @function.inside)

(lambda_expression) @function.around

; ─── Classes / Records ─────────────────────────────────────────────

(record_declaration) @class.around

(choice_declaration) @class.around

(interface_declaration) @class.around

(namespace_declaration) @class.around

; ─── Comments ──────────────────────────────────────────────────────

(comment) @comment.around
(comment) @comment.inside
