; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Outline Queries
;
; Defines the symbols shown in Zed's outline/breadcrumb navigation.
; ═══════════════════════════════════════════════════════════════════

; ─── Functions ─────────────────────────────────────────────────────

(function_declaration
  name: (function_name) @name) @item

(annotation_declaration
  (annotation) @context
  (function_declaration
    name: (function_name) @name)) @item

; ─── Records ───────────────────────────────────────────────────────

(record_declaration
  (type_identifier) @name) @item

; ─── Choices ───────────────────────────────────────────────────────

(choice_declaration
  (type_identifier) @name) @item

; ─── Interfaces ────────────────────────────────────────────────────

(interface_declaration
  (type_identifier) @name) @item

; ─── Namespaces ────────────────────────────────────────────────────

(namespace_declaration
  (type_identifier) @name) @item

; ─── Type Aliases ──────────────────────────────────────────────────

(type_alias
  (type_identifier) @name) @item

; ─── Top-level Variables ───────────────────────────────────────────

(variable_declaration
  (identifier) @name) @item

; ─── Includes ──────────────────────────────────────────────────────

(include_statement
  (string) @name) @item
