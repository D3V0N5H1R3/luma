---
description: "Audit a Luma application or example for user-experience, usability, and visual-design quality for Terminal and Console interfaces"
agent: "agent"
tools: ["search", "read"]
argument-hint: "File or directory to review, e.g. 'examples/applications/text_adventure.luma' or 'examples/applications/' (defaults to Terminal and Console examples)"
version: 1
lastUpdated: "2026-09-04"
---

# UX Audit

> **Scope vs sibling prompts:** This prompt evaluates the **user experience, usability, and presentation design** of the interface a Luma program presents — textual hierarchy, layout, colour, prompts, interaction, feedback, and accessibility. Code-level defects (bugs, security, performance) and code style belong in [code-review.prompt.md](code-review.prompt.md); drift between artefacts (docs vs implementation, examples vs guide) belongs in [consistency-check.prompt.md](consistency-check.prompt.md). When you spot such issues, note them and cross-reference the right prompt rather than expanding this review.

Review the specified Luma application(s) or example(s) from a UX standpoint. If no argument is given, review Terminal/TUI and Console examples under `examples/applications/`, such as text adventures, calculators, editors, drawing demos, and other command-line interfaces.

Read the design principles before starting — they are the rubric for every finding:

- [ux-design.instructions.md](../../instructions/ux-design.instructions.md) — the authoritative UX, usability, and visual-design principles. Every finding should map to a section here.
- [luma.instructions.md](../../instructions/luma.instructions.md) — Luma conventions, so recommendations fit the language.
- [Luma_Standard_Library_Reference.md](../../documents/Luma_Standard_Library_Reference.md) — the `Console` and `Terminal` modules and any stdlib APIs the interface uses.

## What to Review

- **Terminal/TUI applications.** These use the `Terminal` module for raw-mode input, cursor control, colour, screen clearing, keyboard/mouse events, and richer text interfaces. Review screen structure, keyboard flow, focus cues, status areas, shortcuts, and recovery from invalid input.
- **Console applications.** These use plain input and output via `Console` and related modules. Review prompt wording, command discoverability, output hierarchy, error messages, progress feedback, and how well the app guides a first-time user.
- **Running the example (optional).** Run the target to inspect the actual text UI — single-config: `build/luma <file>`, multi-config: `build/Release/luma <file>`. For non-interactive checks, use scripted stdin or the `Terminal.test_*` APIs documented in the standard library reference and feature tests.

## Review Checklist

Evaluate each item against the linked `ux-design.instructions.md` section.

### Hierarchy, Layout, and Grouping

- **Visual hierarchy & focal point (§5).** Each screen or output block has one clear focal point; the most important action or information appears first and is reinforced with spacing, labels, colour, or text weight where the terminal supports it.
- **Grouping & Gestalt (§4).** Related commands, results, and status lines are grouped by proximity, headings, indentation, or separators; unrelated groups are separated by whitespace.
- **Layout, alignment & composition (§6).** Text columns, menus, forms, and status areas align consistently and follow a sensible reading order. Width assumptions are explicit and degrade gracefully on narrow terminals.
- **Whitespace & spacing (§7).** Blank lines are intentional: enough room to scan, but not so much that critical context scrolls away.

### Text Presentation

- **Colour & contrast (§8).** Colour, when used, is semantic and readable on light and dark terminals; meaning is never conveyed by colour alone.
- **Typography in text (§9).** Headings, labels, prompts, and body text use consistent casing, punctuation, and line length. Dense output is chunked into short, readable lines.
- **Symbols and icons (§10).** ASCII/Unicode symbols are conventional, optional, and paired with text so they remain understandable in limited fonts or screen readers.

### Interaction and Feedback

- **Interaction & states (§11).** Inputs and keyboard shortcuts are discoverable, focus or current selection is visible, disabled/unavailable actions are explained, and controls are reachable without a mouse.
- **Feedback & system status (§12).** Every action produces timely feedback; loading, progress, success, empty, and error states are handled; long operations never block silently.
- **Motion and refresh (§13).** Terminal redraws are stable and purposeful, avoid flicker, and do not rely on rapid animation for comprehension.

### Safety, Structure, and Inclusivity

- **Error prevention & recovery (§14).** Prompts constrain input where possible, destructive actions confirm before committing, routine actions offer a way back, and error messages say how to recover.
- **Information architecture & navigation (§15).** Menus, commands, and help are organised around user tasks; users can tell where they are and how to exit or return.
- **Consistency & standards (§16).** The same key, command, label, and status vocabulary mean the same thing throughout; common CLI/TUI conventions are honoured.
- **Responsive & adaptive design (§17).** Output adapts to terminal width and non-interactive environments, and it remains usable with redirected input/output where applicable.
- **Accessibility & inclusive design (§18).** Full keyboard operation, plain language, readable contrast, non-colour cues, and screen-reader-friendly text are preserved.
- **Simplicity & progressive disclosure (§19).** Only what is needed is shown by default; help and advanced options are easy to discover without overwhelming beginners. Watch for the anti-patterns in §20.

### Terminal/Console Lens

When the target is a text UI, check these concrete expressions of the principles above:

- **Prompt clarity.** Prompts state expected input, default choices, and how to cancel or get help.
- **Command discoverability.** Available commands and shortcuts are shown where needed, with consistent names and examples.
- **Screen stability.** Redraws avoid confusing jumps; persistent status, help, or error regions stay in predictable places.
- **Input tolerance.** Invalid input is handled without data loss, with actionable recovery text.
- **Headless testability.** Interactive behaviour can be driven by scripted stdin or `Terminal.test_*` APIs so future changes do not regress the experience.

## Output Format

For each finding, report:

1. **Location** — file path, and the screen, prompt, output block, or function involved (line range where useful).
2. **Severity** — one of:
    - **Critical** — blocks a task or makes the interface inaccessible (e.g. no keyboard path, unreadable essential text, destructive action with no confirmation or undo).
    - **Major** — significant friction or confusion (e.g. no feedback on a slow action, unclear prompt, colour-only status).
    - **Minor** — polish (e.g. inconsistent spacing, noisy wording).
    - **Suggestion** — an optional enhancement.
3. **Principle** — the `ux-design.instructions.md` section the finding maps to.
4. **Description** — what the problem is and why it harms the experience.
5. **Recommendation** — a concrete fix, with a Luma snippet where it helps.

Group findings by file or screen. Prioritise Critical and accessibility issues first, then Major. This is an audit: report findings and recommendations, and apply fixes only if the user asks — the paired [ux-improve.prompt.md](ux-improve.prompt.md) executor consumes this report and applies the fixes.
