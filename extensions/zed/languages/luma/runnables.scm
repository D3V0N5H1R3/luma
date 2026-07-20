; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Runnable Queries
;
; Provides inline "Run" buttons for @main and @test functions.
; Implements the shared annotation contract documented in
; extensions/shared/test-discovery-pattern.json (tree-sitter form; the
; regex form there drives the VS Code extension).
; ═══════════════════════════════════════════════════════════════════

; Run @main functions
(annotation_declaration
    (annotation) @_ann
    (function_declaration
        name: (function_name) @run)
    (#eq? @_ann "@main")
    (#set! tag main))

; Run @test functions with --test flag
(annotation_declaration
    (annotation) @_ann
    (function_declaration
        name: (function_name) @run)
    (#eq? @_ann "@test")
    (#set! tag test)
    (#set! tag.args "--test"))
