; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Override Queries
;
; Overrides highlighting inside string interpolation expressions
; so that `${expr}` content is highlighted as code, not string.
; ═══════════════════════════════════════════════════════════════════

; Remove string highlighting from interpolation body so inner
; expressions are highlighted normally by highlights.scm.
(interpolation) @none
