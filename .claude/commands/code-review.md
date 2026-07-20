---
description: "Review code for bugs, security issues, performance pitfalls, and style violations"
argument-hint: "File or directory to review, e.g. 'core/runtime/vm/' or 'core/analysis/types/type_checker.cpp'"
---

Follow the workflow defined in the Luma prompt file below, which is the single
source of truth for this command. Read it in full and carry out its steps in the
order given.

@.github/prompts/code-review.prompt.md

Use the text below as this command's argument. If it is empty, follow the
prompt's default, no-argument behaviour:

$ARGUMENTS
