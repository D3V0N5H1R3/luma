; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Outline Queries
;
; Defines the symbols shown in Zed's outline/breadcrumb navigation.
; Every pattern that should produce an entry needs an @item capture; Zed
; skips matches without one. @annotation attaches the `@main`/`@test`
; annotation to the function that follows it, as Zed does for Python
; decorators and Rust attributes.
; ═══════════════════════════════════════════════════════════════════

(annotation) @annotation

; ─── Functions ─────────────────────────────────────────────────────

(function_declaration
  "function" @context
  name: (function_name) @name) @item

; ─── Records ───────────────────────────────────────────────────────

(record_declaration
  "record" @context
  (type_identifier) @name) @item

; ─── Choices ───────────────────────────────────────────────────────

(choice_declaration
  "choice" @context
  (type_identifier) @name) @item

; ─── Interfaces ────────────────────────────────────────────────────

(interface_declaration
  "interface" @context
  (type_identifier) @name) @item

; ─── Namespaces ────────────────────────────────────────────────────

(namespace_declaration
  "namespace" @context
  (type_identifier) @name) @item

; ─── Type Aliases ──────────────────────────────────────────────────

(type_alias
  "type" @context
  (type_identifier) @name) @item

; ─── Top-level Variables ───────────────────────────────────────────

; Only file- and namespace-level declarations: locals inside function
; bodies would flood the outline.

(source_file
  (variable_declaration
    (identifier) @name) @item)

(namespace_declaration
  (variable_declaration
    (identifier) @name) @item)

; ─── Includes ──────────────────────────────────────────────────────

(include_statement
  "include" @context
  (string) @name) @item
