---
description: "Analyse the project read-only and produce a prioritized, actionable list of performance optimization opportunities — without changing any code"
argument-hint: "Optional scope, e.g. 'core/runtime/vm/' or 'the whole interpreter'"
---

Follow the workflow defined in the Luma prompt file below, which is the single
source of truth for this command. Read it in full and carry out its steps in the
order given.

@.github/prompts/performance-audit.prompt.md

Use the text below as this command's argument. If it is empty, follow the
prompt's default, no-argument behaviour:

$ARGUMENTS
