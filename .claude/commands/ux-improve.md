---
description: "Apply UX-audit findings — improve the user experience, usability, and visual design of a Luma app or example while keeping all tests green"
argument-hint: "A target app or example to improve, e.g. 'examples/applications/gui_todo.luma' or 'examples/applications/' (defaults to the GraphicalUi examples; the pipeline supplies the ranked ux-audit report)"
---

Follow the workflow defined in the Luma prompt file below, which is the single
source of truth for this command. Read it in full and carry out its steps in the
order given.

@.github/prompts/ux-improve.prompt.md

Use the text below as this command's argument. If it is empty, follow the
prompt's default, no-argument behaviour:

$ARGUMENTS
