---
description: "Diagnose and fix a bug in the Luma debugger (DAP)"
argument-hint: "Bug description, e.g. 'step out skips the caller frame inside task_scope'"
---

Follow the workflow defined in the Luma prompt file below, which is the single
source of truth for this command. Read it in full and carry out its steps in the
order given.

@.github/prompts/bug-fix-debugger.prompt.md

Use the text below as this command's argument. If it is empty, follow the
prompt's default, no-argument behaviour:

$ARGUMENTS
