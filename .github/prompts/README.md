# Luma Prompt Files

Reusable, task-focused prompts for common Luma development workflows. Each `*.prompt.md` file packages a single, parameterised task — fix a bug, add a feature, run the tests, review code — that an agent can run on demand.

Run a prompt in any of three ways:

- **Chat:** type `/`, pick the prompt by name, and supply its argument (the **Argument** column below).
- **Command Palette:** run **Chat: Run Prompt** and choose the file.
- **Editor:** open the `*.prompt.md` file and press the play button.

Every prompt runs in agent mode. A `—` in the **Argument** column means the prompt takes no input.

---

## 1 — Build, Test, and Release

| Prompt                                                 | Argument | Purpose                                                                                   |
| ------------------------------------------------------ | -------- | ----------------------------------------------------------------------------------------- |
| [build-and-test](build-and-test.prompt.md)             | —        | Build (Release) and run the C++ unit tests and Luma feature tests — the quick inner loop. |
| [full-test-sweep](full-test-sweep.prompt.md)           | —        | Run every test category: unit, fuzz smoke, benchmarks, and example validation.            |
| [release-verification](release-verification.prompt.md) | —        | Clean-room rebuild of all binaries and editor extensions, then the full suite.            |

---

## 2 — Bug Hunting and Fixing

| Prompt                                                               | Argument        | Purpose                                                                           |
| -------------------------------------------------------------------- | --------------- | --------------------------------------------------------------------------------- |
| [bug-search](bug-search.prompt.md)                                   | optional scope  | Read-only: find and rank suspected bugs in the interpreter core and stdlib.       |
| [bug-fix](bug-fix.prompt.md)                                         | bug description | Interpreter core (lexer to VM) and the standard library.                          |
| [bug-search-language-server](bug-search-language-server.prompt.md)   | optional scope  | Read-only: find and rank suspected bugs in the language server.                   |
| [bug-fix-language-server](bug-fix-language-server.prompt.md)         | bug description | Language server (`luma_lsp`) — diagnostics, hover, completion, navigation.        |
| [bug-search-debugger](bug-search-debugger.prompt.md)                 | optional scope  | Read-only: find and rank suspected bugs in the debugger.                          |
| [bug-fix-debugger](bug-fix-debugger.prompt.md)                       | bug description | Debugger (`luma_dap`) — breakpoints, stepping, inspection, evaluation.            |
| [bug-search-editor-extension](bug-search-editor-extension.prompt.md) | optional scope  | Read-only: find and rank suspected bugs across the extensions and shared grammar. |
| [bug-fix-editor-extension](bug-fix-editor-extension.prompt.md)       | bug description | VS Code and Zed extensions and their shared grammar.                              |

---

## 3 — Adding Functionality

| Prompt                                                 | Argument          | Purpose                                                                      |
| ------------------------------------------------------ | ----------------- | ---------------------------------------------------------------------------- |
| [new-requirements](new-requirements.prompt.md)         | optional focus    | Read-only: research and rank candidate additions to the language and stdlib. |
| [new-language-feature](new-language-feature.prompt.md) | feature           | Implement a language feature across every pipeline phase.                    |
| [new-stdlib-function](new-stdlib-function.prompt.md)   | module + function | Add a built-in function to an existing standard library module.              |
| [new-stdlib-module](new-stdlib-module.prompt.md)       | module + purpose  | Add an entirely new standard library module.                                 |
| [new-stdlib-type](new-stdlib-type.prompt.md)           | module + type     | Add a record or choice type to a standard library module.                    |

---

## 4 — Refactoring

| Prompt                                     | Argument       | Purpose                                                               |
| ------------------------------------------ | -------------- | --------------------------------------------------------------------- |
| [refactor-audit](refactor-audit.prompt.md) | optional scope | Read-only: find and rank refactoring opportunities — no code changes. |
| [refactor](refactor.prompt.md)             | goal           | Execute one refactoring while keeping the test suite green.           |

---

## 5 — Performance

| Prompt                                           | Argument       | Purpose                                                                |
| ------------------------------------------------ | -------------- | ---------------------------------------------------------------------- |
| [performance-audit](performance-audit.prompt.md) | optional scope | Read-only: find and rank optimization opportunities — no code changes. |
| [optimize](optimize.prompt.md)                   | goal           | Implement one optimization and prove the speedup with a benchmark.     |

---

## 6 — Code Quality and Review

| Prompt                                               | Argument          | Purpose                                                              |
| ---------------------------------------------------- | ----------------- | -------------------------------------------------------------------- |
| [code-review](code-review.prompt.md)                 | file or directory | Review for bugs, security, performance, and style violations.        |
| [consistency-check](consistency-check.prompt.md)     | optional focus    | Find drift between code, build, docs, tests, and tooling.            |
| [lint-and-format](lint-and-format.prompt.md)         | —                 | Run every language's linter and formatter across the repository.     |
| [source-code-cleanup](source-code-cleanup.prompt.md) | —                 | Apply the project's style and convention rules across all languages. |
| [ux-audit](ux-audit.prompt.md)                       | file or directory | Audit an app's UX, usability, and visual design — esp. GraphicalUi.  |
| [ux-improve](ux-improve.prompt.md)                   | file or directory | Apply the UX audit's findings to an app or example.                  |

---

## 7 — Project Maintenance

| Prompt                                                   | Argument       | Purpose                                                                     |
| -------------------------------------------------------- | -------------- | --------------------------------------------------------------------------- |
| [iterative-improvement](iterative-improvement.prompt.md) | —              | Orchestrate the review, fix, build, and test prompts in a loop until clean. |
| [update-learnings](update-learnings.prompt.md)           | optional focus | Capture and prune entries in the learnings instruction file.                |

---

## 8 — Conventions

- **Pick by subsystem, then by depth.** The bug prompts route by subsystem and run in two steps — a read-only [bug-search](bug-search.prompt.md) hunt finds and ranks suspected bugs, then [bug-fix](bug-fix.prompt.md) reproduces and fixes one (mirroring the two-step [refactor-audit](refactor-audit.prompt.md) / [refactor](refactor.prompt.md), [performance-audit](performance-audit.prompt.md) / [optimize](optimize.prompt.md), and [ux-audit](ux-audit.prompt.md) / [ux-improve](ux-improve.prompt.md) pairings), each with dedicated variants for the language server, debugger, and editor extensions — and the test prompts form a depth ladder: [build-and-test](build-and-test.prompt.md) (inner loop), then [full-test-sweep](full-test-sweep.prompt.md) (every category), then [release-verification](release-verification.prompt.md) (clean room).
- **Adding functionality also runs in two steps.** A read-only [new-requirements](new-requirements.prompt.md) study researches peer languages, their standard libraries, and Elm-architecture GUI frameworks and produces a ranked, routed list of candidate additions; each item then feeds the matching builder — [new-language-feature](new-language-feature.prompt.md), [new-stdlib-module](new-stdlib-module.prompt.md), [new-stdlib-type](new-stdlib-type.prompt.md), or [new-stdlib-function](new-stdlib-function.prompt.md) — completing the same discovery-then-execution pairing as the refactor, performance, UX, and bug prompts.
- **Prompts cross-reference instead of repeating.** Shared steps live in one place: the canonical build-and-test workflow in [build-and-test](build-and-test.prompt.md) and the canonical linter and formatter commands in [lint-and-format](lint-and-format.prompt.md). Other prompts link to them rather than duplicating the commands.
- **The discovery-report scaffold is repeated on purpose.** Every discovery prompt ends with the same ranked-report shape — a priority-ordered summary table followed by a per-entry detail template, preceded by a prioritization pass. It recurs near-identically across the seven: [bug-search](bug-search.prompt.md) with its [debugger](bug-search-debugger.prompt.md), [language-server](bug-search-language-server.prompt.md), and [editor-extension](bug-search-editor-extension.prompt.md) variants, [refactor-audit](refactor-audit.prompt.md), [performance-audit](performance-audit.prompt.md), and [new-requirements](new-requirements.prompt.md). Unlike the build and lint commands above, the report format is structural content the invoked agent must emit inline, and prompt files have no include mechanism — so each keeps its own copy by design, accepting the duplication for self-containment. If you change the report shape, change it in all seven.
- **Keep this index in sync.** When you add, rename, or remove a prompt, update the table above and the prompt-index invariant in [consistency-check](consistency-check.prompt.md) §14 that enforces it.
