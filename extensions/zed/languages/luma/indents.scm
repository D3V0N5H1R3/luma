; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Indentation Queries
;
; Controls automatic indentation in the Zed editor.
;
; Zed indents the lines inside a node captured as @indent, and the @end
; capture marks the token that closes it. The capture must be the whole
; node: an @indent on the opening `{` alone covers a single line and so
; never indents the body.
; ═══════════════════════════════════════════════════════════════════

; Blocks, declaration bodies, match arms, dictionaries, task scopes.
(_
  "{"
  "}" @end) @indent

; Array literals and generic/index brackets spanning multiple lines.
(_
  "["
  "]" @end) @indent

; Parameter and argument lists spanning multiple lines.
(_
  "("
  ")" @end) @indent
