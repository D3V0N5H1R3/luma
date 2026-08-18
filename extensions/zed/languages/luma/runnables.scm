; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Runnable Queries
;
; Provides inline "Run" buttons for @main and @test functions.
; Implements the shared annotation contract documented in
; extensions/shared/test-discovery-pattern.json (tree-sitter form; the
; regex form there drives the VS Code extension).
;
; Each `(#set! tag …)` names a tag that Zed matches against the task
; templates in languages/luma/tasks.json (the "main" and "test" tags).
; Without a matching task template no run button is shown, so the two
; files must stay in sync.
; ═══════════════════════════════════════════════════════════════════

; Run @main functions
(annotation_declaration
    (annotation) @_ann
    (function_declaration
        name: (function_name) @run)
    (#eq? @_ann "@main")
    (#set! tag main))

; Run @test functions with the --test flag.
; The tag maps to the "test" task template in languages/luma/tasks.json,
; which supplies the actual `--test` argument.
(annotation_declaration
    (annotation) @_ann
    (function_declaration
        name: (function_name) @run)
    (#eq? @_ann "@test")
    (#set! tag test))
