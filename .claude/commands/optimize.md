---
description: "Optimize Luma code for speed or memory — interpreter, language server, debugger, or stdlib — proving the win with a benchmark while keeping all tests green"
argument-hint: "Optimization goal, e.g. 'cut the per-opcode Value copy in the VM arithmetic handlers'"
---

Follow the workflow defined in the Luma prompt file below, which is the single
source of truth for this command. Read it in full and carry out its steps in the
order given.

@.github/prompts/optimize.prompt.md

Use the text below as this command's argument. If it is empty, follow the
prompt's default, no-argument behaviour:

$ARGUMENTS
