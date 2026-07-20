---
description: "Analyse the Luma language server (LSP) read-only and produce a prioritized, actionable list of suspected bugs — without changing any code"
argument-hint: "Optional scope, e.g. 'lsp_completion_provider.cpp' or 'the whole language server'"
---

Follow the workflow defined in the Luma prompt file below, which is the single
source of truth for this command. Read it in full and carry out its steps in the
order given.

@.github/prompts/bug-search-language-server.prompt.md

Use the text below as this command's argument. If it is empty, follow the
prompt's default, no-argument behaviour:

$ARGUMENTS
