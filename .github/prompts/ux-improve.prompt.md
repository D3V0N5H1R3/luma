---
description: "Apply UX-audit findings — improve the user experience, usability, and visual design of a Luma app or example while keeping all tests green"
agent: "agent"
argument-hint: "A target app or example to improve, e.g. 'examples/applications/gui_todo.luma' or 'examples/applications/' (defaults to the GraphicalUi examples; the pipeline supplies the ranked ux-audit report)"
version: 1
lastUpdated: "2026-08-01"
---

# UX Improve

Apply user-experience, usability, and visual-design improvements to the interface a Luma program presents — most often a `GraphicalUi` app under `examples/applications/` (the `gui_*.luma` and `solaris_*.luma` files), its Solaris MVU surface, or the shared front-end assets in `external/gui-framework/`. This is the executor half of the [ux-audit.prompt.md](ux-audit.prompt.md) pairing: it consumes that audit's ranked findings and resolves them. Change only what improves the experience — preserve each program's logic and observable behaviour otherwise.

1. **Read the rubric and take the work list.** The design principles are the standard every change is measured against — read them first:
    - [ux-design.instructions.md](../../instructions/ux-design.instructions.md) — the full rubric (visual hierarchy, Gestalt grouping, colour and contrast, typography, interaction and feedback, motion, error prevention, information architecture, accessibility, simplicity).
    - [Luma_GraphicalUi_Guide.md](../../documents/Luma_GraphicalUi_Guide.md) for the `GraphicalUi` module — widgets, layout containers, styling helpers, theme tokens, §13 (Motion), §14 (Accessibility), §17 (Responsiveness), and §21 (Testing Without a Window) — and [Luma_Solaris_Guide.md](../../documents/Luma_Solaris_Guide.md) for the Solaris MVU surface.

    Then take your work list from the audit: if you were given a ranked UX audit report (the pipeline supplies one for this phase, `07-ux-audit.md`), work through its findings in priority order; otherwise run the [ux-audit.prompt.md](ux-audit.prompt.md) lens over the target yourself to produce the findings before changing anything.
2. **Read the target.** Read the `.luma` app(s) or example(s) named in the findings, and — when a fix reaches styling or the framework itself — the front-end assets in `external/gui-framework/` (its CSS and JavaScript). Understand the view/update (Elm-architecture) structure so a presentation change does not disturb the model or the messages that drive it.
3. **Establish a green baseline.** Build and test **before** changing anything, and record any pre-existing failures so they stay distinguishable from regressions:
    - Build the interpreter: `cmake --build --preset default`.
    - Run the suite: `ctest --preset default`.
    - Run the examples headlessly: `python scripts/run_luma_examples.py` — the `gui_*` / `solaris_*` examples are driven with `LUMA_GUI_HEADLESS=1` and are **not** part of `ctest`, so this is the only guardrail that exercises the very surfaces you are about to change.
4. **Apply the findings in priority order.** Take Critical and accessibility issues first, then Major, then Minor and Suggestions. For each finding make the smallest change that resolves it, using the mechanism the principle calls for rather than a one-off patch:
    - **Theming over hardcoding** — reference theme tokens (`GraphicalUi.VAR_PRIMARY`, `VAR_FG`, `VAR_BORDER`, `VAR_PRIMARY_HOVER`, …) instead of literal colours, so light/dark themes and cross-app consistency hold (ux-design §8, §16).
    - **Contrast** — bring text and UI boundaries to WCAG AA (4.5:1 body text, 3:1 large text and components); never signal meaning by colour alone (§8).
    - **Hierarchy, spacing, and layout** — express hierarchy through size and weight and a consistent spacing scale via layout containers (rows, columns, grids, scrollable regions), not manual pixel-nudging (§5, §6, §7).
    - **Interaction and focus** — interactive widgets define their hover, active, focus, disabled, and selected states and keep focus always visible; hit targets stay comfortably sized (§11, §18).
    - **Feedback and status** — asynchronous work (HTTP, timers, file I/O) surfaces loading, progress, success, empty, and error states and never blocks silently (§12).
    - **Motion** — animation is purposeful and fast, animates cheap properties, and honours reduced-motion (§13).
    - **Accessibility** — controls carry accessible labels and roles, dynamic changes are announced with `GraphicalUi.announce(...)`, and focus is managed with `GraphicalUi.focus(...)` after actions (§18; GraphicalUi Guide §14).

    Follow the conventions of the language you are editing: [luma.instructions.md](../../instructions/luma.instructions.md) for `.luma`, and [css.instructions.md](../../instructions/css.instructions.md) and [javascript.instructions.md](../../instructions/javascript.instructions.md) for the `external/gui-framework/` front-end. Keep each change small enough that the checks in the next step stay meaningful checkpoints, and commit or stash each green checkpoint so any regression is easy to roll back.
5. **Verify each change against the running interface.** After each change, exercise the surface you touched — a clean rebuild is the first checkpoint, a clean headless run the second:
    - Run the affected example headless: single-config `LUMA_GUI_HEADLESS=1 build/luma <file>`, multi-config `LUMA_GUI_HEADLESS=1 build/Release/luma <file>`; drive interaction with `LUMA_GUI_MESSAGES` or the `GraphicalUi.test_*` API (GraphicalUi Guide §21) to confirm the improved behaviour, its feedback, and its accessibility.
    - Re-run `python scripts/run_luma_examples.py` so every example — including the one you changed — still completes cleanly.
    - Rebuild and re-run `ctest --preset default` whenever a change reaches any C++ (for example a `GraphicalUi` or `Terminal` stdlib tweak).
6. **Update references if you moved anything.** If you added or renamed theme tokens, style rules, or assets, update whatever lists them — the embedded-asset concatenation order documented in [external/gui-framework/DIRECTORY.md](../../external/gui-framework/DIRECTORY.md), and any `#include` or import paths — so the embedded build matches the dev assets.
7. **Lint and format every language you touched.** Follow [lint-and-format.prompt.md](lint-and-format.prompt.md) for the exact tooling, pinned versions, and commands (lint first, then format): **Stylelint** (`npx stylelint --fix …`) for the gui-framework CSS, **ESLint** then **Prettier** for its JavaScript, and `luma --check --strict <file>` for `.luma` (which has no formatter — enforce layout by hand against [luma.instructions.md](../../instructions/luma.instructions.md)). For any C++ you touched, run the `tidy` (clang-tidy) target and clang-format, since the CI **Formatting** job fails on any diff.
8. **Do a final green sweep.** Build once more, run `ctest --preset default`, and run `python scripts/run_luma_examples.py` a final time — confirm the tree is green and every linter and formatter for the languages you touched is clean before finishing. This is a behaviour-preserving polish pass: the program does the same thing, but its interface is measurably better against the rubric.
