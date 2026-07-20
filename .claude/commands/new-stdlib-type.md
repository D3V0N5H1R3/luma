---
description: "Add a new record or choice type to a Luma standard library module"
argument-hint: "Module name and type description, e.g. 'Http.Response — record with status, reason, body, headers'"
---

Follow the workflow defined in the Luma prompt file below, which is the single
source of truth for this command. Read it in full and carry out its steps in the
order given.

@.github/prompts/new-stdlib-type.prompt.md

Use the text below as this command's argument. If it is empty, follow the
prompt's default, no-argument behaviour:

$ARGUMENTS
