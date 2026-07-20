---
description: "Analyse the Luma debugger (DAP) read-only and produce a prioritized, actionable list of suspected bugs — without changing any code"
argument-hint: "Optional scope, e.g. 'breakpoint_manager.cpp' or 'the whole debugger'"
---

Follow the workflow defined in the Luma prompt file below, which is the single
source of truth for this command. Read it in full and carry out its steps in the
order given.

@.github/prompts/bug-search-debugger.prompt.md

Use the text below as this command's argument. If it is empty, follow the
prompt's default, no-argument behaviour:

$ARGUMENTS
