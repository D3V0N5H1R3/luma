---
description: "Analyse a Luma editor extension (VS Code or Zed) read-only and produce a prioritized, actionable list of suspected bugs — without changing any code"
argument-hint: "Optional scope, e.g. 'the VS Code extension' or 'the shared grammar'"
---

# Bug Search — Editor Extension

Follow the workflow defined in the Luma prompt file below, which is the single source of truth for this command. Read it in full and carry out its steps in the order given.

@.github/prompts/bug-search-editor-extension.prompt.md

Use the text below as this command's argument. If it is empty and the argument is optional, use the prompt's full default scope; if the argument is required, ask for it before starting.

$ARGUMENTS
