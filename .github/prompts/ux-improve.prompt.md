---
description: "Apply UX-audit findings — improve the user experience, usability, and visual design of a Luma Terminal or Console app while keeping all tests green"
agent: "agent"
argument-hint: "A target app or example to improve, e.g. 'examples/applications/text_adventure.luma' or 'examples/applications/' (defaults to Terminal and Console examples; the pipeline supplies the ranked ux-audit report)"
version: 1
lastUpdated: "2026-09-04"
---

# UX Improve

Apply user-experience, usability, and presentation-design improvements to the Terminal/TUI or Console interface a Luma program presents, especially apps under `examples/applications/`. This is the executor half of the [ux-audit.prompt.md](ux-audit.prompt.md) pairing: it consumes that audit's ranked findings and resolves them. Change only what improves the experience — preserve each program's logic and observable behaviour otherwise.

1. **Read the rubric and take the work list.** The design principles are the standard every change is measured against — read them first:
    - [ux-design.instructions.md](../../instructions/ux-design.instructions.md) — the full rubric (visual hierarchy, Gestalt grouping, colour and contrast, typography, interaction and feedback, motion, error prevention, information architecture, accessibility, simplicity).
    - [luma.instructions.md](../../instructions/luma.instructions.md) for Luma source conventions.
    - [Luma_Standard_Library_Reference.md](../../documents/Luma_Standard_Library_Reference.md) for the `Console` and `Terminal` modules and related APIs.

    Then take your work list from the audit: if you were given a ranked UX audit report (the pipeline supplies one for this phase, `07-ux-audit.md`), work through its findings in priority order; otherwise run the [ux-audit.prompt.md](ux-audit.prompt.md) lens over the target yourself to produce the findings before changing anything.
2. **Read the target.** Read the `.luma` app(s) or example(s) named in the findings. Understand the input loop, state updates, screen/output rendering, and any test hooks so a presentation change does not disturb the program logic.
3. **Establish a green baseline.** Build and test **before** changing anything, and record any pre-existing failures so they stay distinguishable from regressions:
    - Build the interpreter: `cmake --build --preset default`.
    - Run the suite: `ctest --preset default`.
    - Run the examples headlessly: `python scripts/run_luma_examples.py`.
4. **Apply the findings in priority order.** Take Critical and accessibility issues first, then Major, then Minor and Suggestions. For each finding make the smallest change that resolves it, using the mechanism the principle calls for rather than a one-off patch:
    - **Clarity over decoration** — use headings, labels, spacing, and concise wording to make the next action obvious.
    - **Contrast and non-colour cues** — keep terminal colours readable and pair colour with text, symbols, or position.
    - **Hierarchy, spacing, and layout** — express structure through consistent blank lines, indentation, separators, and aligned columns.
    - **Interaction and focus** — make current selections, prompts, shortcuts, and exits visible; preserve keyboard-only operation.
    - **Feedback and status** — asynchronous work (HTTP, timers, file I/O) surfaces loading, progress, success, empty, and error states and never blocks silently.
    - **Accessibility** — use plain language, avoid colour-only meaning, keep output screen-reader-friendly, and provide recoverable error messages.

    Follow [luma.instructions.md](../../instructions/luma.instructions.md) for `.luma` source. Keep each change small enough that the checks in the next step stay meaningful checkpoints, and commit or stash each green checkpoint so any regression is easy to roll back.
5. **Verify each change against the running interface.** After each change, exercise the surface you touched — a clean rebuild is the first checkpoint, a clean run the second:
    - Run the affected example: single-config `build/luma <file>`, multi-config `build/Release/luma <file>`; drive interaction with scripted stdin or the `Terminal.test_*` API when possible to confirm the improved behaviour, its feedback, and its accessibility.
    - Re-run `python scripts/run_luma_examples.py` so every example — including the one you changed — still completes cleanly.
    - Rebuild and re-run `ctest --preset default` whenever a change reaches any C++ (for example a `Terminal` or `Console` stdlib tweak).
6. **Update references if you moved anything.** If you renamed examples, helper functions, commands, or documented shortcuts, update imports, includes, example indexes, and documentation references so they match.
7. **Lint and format every language you touched.** Follow [lint-and-format.prompt.md](lint-and-format.prompt.md) for the exact tooling, pinned versions, and commands (lint first, then format). For `.luma`, run `luma --check --strict <file>` and enforce layout by hand against [luma.instructions.md](../../instructions/luma.instructions.md). For any C++ you touched, run the `tidy` (clang-tidy) target and clang-format, since the CI **Formatting** job fails on any diff.
8. **Do a final green sweep.** Build once more, run `ctest --preset default`, and run `python scripts/run_luma_examples.py` a final time — confirm the tree is green and every linter and formatter for the languages you touched is clean before finishing. This is a behaviour-preserving polish pass: the program does the same thing, but its interface is measurably better against the rubric.
