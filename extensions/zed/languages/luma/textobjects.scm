; ═══════════════════════════════════════════════════════════════════
; Luma — Tree-sitter Text Object Queries
;
; Defines text objects for Zed's vim-mode and structural selection.
; ═══════════════════════════════════════════════════════════════════

; ─── Functions ─────────────────────────────────────────────────────

(function_declaration) @function.around

(function_declaration
  (block
    "{" @function.inside._start
    "}" @function.inside._end))

; ─── Classes / Records ─────────────────────────────────────────────

(record_declaration) @class.around

(record_declaration
  "{" @class.inside._start
  "}" @class.inside._end)

(choice_declaration) @class.around

(choice_declaration
  "{" @class.inside._start
  "}" @class.inside._end)

(interface_declaration) @class.around

(interface_declaration
  "{" @class.inside._start
  "}" @class.inside._end)

(namespace_declaration) @class.around

(namespace_declaration
  "{" @class.inside._start
  "}" @class.inside._end)

; ─── Control Flow Blocks ───────────────────────────────────────────

(if_statement) @block.around

(if_statement
  (block
    "{" @block.inside._start
    "}" @block.inside._end))

(for_statement) @block.around

(for_statement
  (block
    "{" @block.inside._start
    "}" @block.inside._end))

(while_statement) @block.around

(while_statement
  (block
    "{" @block.inside._start
    "}" @block.inside._end))

(match_expression) @block.around

(match_expression
  "{" @block.inside._start
  "}" @block.inside._end)

(try_statement) @block.around

(try_statement
  (block
    "{" @block.inside._start
    "}" @block.inside._end))

(task_scope_expression) @block.around

(task_scope_expression
  "{" @block.inside._start
  "}" @block.inside._end)

(lambda_expression) @function.around

; ─── Array / Dictionary Literals ───────────────────────────────────

(array_literal) @array.around

(array_literal
  "[" @array.inside._start
  "]" @array.inside._end)

(dictionary_literal) @array.around

(dictionary_literal
  "{" @array.inside._start
  "}" @array.inside._end)

; ─── Comments ──────────────────────────────────────────────────────

(comment) @comment.around
(comment) @comment.inside

; ─── Parameters / Arguments ────────────────────────────────────────

(parameter) @parameter.around

(parameter_list
  "(" @parameter.inside._start
  ")" @parameter.inside._end)

(argument) @parameter.around
