---
description: "Audit a Luma application or example for user-experience, usability, and visual-design quality — especially GraphicalUi apps"
agent: "agent"
tools: ["search", "read"]
argument-hint: "File or directory to review, e.g. 'examples/applications/gui_todo.luma' or 'examples/applications/' (defaults to the GraphicalUi examples)"
---

# UX Audit

> **Scope vs sibling prompts:** This prompt evaluates the **user experience, usability, and visual design** of the interface a Luma program presents — its hierarchy, layout, colour, typography, interaction, feedback, and accessibility. Code-level defects (bugs, security, performance) and code style belong in [code-review.prompt.md](code-review.prompt.md); drift between artefacts (docs vs implementation, examples vs guide) belongs in [consistency-check.prompt.md](consistency-check.prompt.md). When you spot such issues, note them and cross-reference the right prompt rather than expanding this review.

Review the specified Luma application(s) or example(s) from a UX standpoint. If no argument is given, review the GraphicalUi examples under `examples/applications/` (the `gui_*.luma` files).

Read the design principles before starting — they are the rubric for every finding:

- [ux-design.instructions.md](../../instructions/ux-design.instructions.md) — the authoritative UX, usability, and graphic-design principles. Every finding should map to a section here.
- [Luma_GraphicalUi_Guide.md](../../documents/Luma_GraphicalUi_Guide.md) — the low-level GUI API: widgets, layout containers, sizing/alignment/spacing, styling and theming, animation, accessibility, responsive design, and best practices.
- [Luma_Solaris_Guide.md](../../documents/Luma_Solaris_Guide.md) — the beginner-first `Solaris` MVU surface (the primary way apps are written).
- [css.instructions.md](../../instructions/css.instructions.md) — for the style dictionaries GraphicalUi widgets accept (colour, contrast, spacing, focus, reduced-motion) and any `.css` the example ships.
- [luma.instructions.md](../../instructions/luma.instructions.md) — Luma conventions, so recommendations fit the language.

## What to Review

- **GraphicalUi applications** (the primary target). These follow the Elm Architecture and render through an embedded webview, so the UI is a pure function of state expressed in the `view`. Review the UI **as expressed in source**: the widgets chosen, the layout containers, the style dictionaries, the theming tokens, the widget states, and the side effects (commands/subscriptions) that drive feedback.
- **Terminal, REPL, and CLI applications** (secondary). Here the "interface" is textual output and prompts via `Console`, `Terminal`, and `Process`. Apply the same principles to output clarity, hierarchy, prompts, progress, colour use, and error messages.
- **Running the example (optional).** Where a display is available, run it to see the rendered result — single-config: `build/luma <file>`, multi-config: `build/Release/luma <file>`. Otherwise reason from the source. For headless inspection techniques, see [Luma_GraphicalUi_Guide.md](../../documents/Luma_GraphicalUi_Guide.md) §21 (Testing Without a Window).

## Review Checklist

Evaluate each item against the linked `ux-design.instructions.md` section.

### Hierarchy, Layout, and Grouping

- **Visual hierarchy & focal point (§5).** Each screen has one clear focal point; the most important action or content draws the eye first. Importance is encoded with size, weight, colour, and spacing — not everything competing at once.
- **Grouping & Gestalt (§4).** Related controls are grouped by proximity and a shared container (common region); unrelated groups are separated by space. Spacing — not just borders — carries the structure.
- **Layout, alignment & composition (§6).** Elements align to a small number of shared edges; layout containers (rows, columns, grids) are used appropriately; the composition is balanced and follows a sensible reading order.
- **Whitespace & spacing (§7).** Spacing comes from a consistent scale; space *within* a group is smaller than space *between* groups; content has room to breathe.

### Visual Style

- **Colour & contrast (§8).** Colours come from semantic theme tokens, not scattered hardcoded hues; text and UI boundaries meet WCAG AA (4.5:1 text, 3:1 large text / components); meaning is never conveyed by colour alone.
- **Typography (§9).** A limited type scale and ≤2 families; hierarchy via size/weight; readable line length and line-height; left-aligned long text.
- **Iconography (§10).** Icons are conventional and consistent in style, paired with labels or accessible names — no mystery-meat icons.

### Interaction and Feedback

- **Interaction & states (§11).** Interactive elements look interactive and define their states — default, hover, focus, active, disabled, and selected where relevant. Targets are comfortably sized; focus is always visible.
- **Feedback & system status (§12).** Every action produces timely feedback; loading, progress, success, empty, and error states are handled; long operations show progress and never block silently.
- **Motion & animation (§13).** Animation is purposeful and fast, uses natural easing, animates cheap properties, and respects reduced-motion preferences.

### Safety, Structure, and Inclusivity

- **Error prevention & recovery (§14).** Mistakes are prevented with constraints, defaults, and forgiving input; destructive or irreversible actions confirm before committing; routine actions offer undo; error messages are plain-language and say how to recover.
- **Information architecture & navigation (§15).** Content is organised around the user's tasks; navigation is consistent and shows where the user is and how to get back.
- **Consistency & standards (§16).** The same action, label, icon, and colour mean the same thing throughout; established platform conventions are honoured, not reinvented.
- **Responsive & adaptive design (§17).** The layout adapts to different window sizes; it reflows and reprioritises rather than just shrinking; it works for both pointer and touch.
- **Accessibility & inclusive design (§18).** Full keyboard operation with visible focus; text alternatives and accessible names; semantic structure and labelled controls; plain language; user preferences (reduced-motion, contrast) respected.
- **Simplicity & progressive disclosure (§19).** Only what is needed is shown; advanced options are progressively disclosed; smart defaults; one primary action per view. Watch for the anti-patterns in §20.

### GraphicalUi-Specific Lens

When the target is a `GraphicalUi` app, check these concrete expressions of the principles above:

- **Theming over hardcoding.** Style dictionaries reference theme tokens (`GraphicalUi.VAR_PRIMARY`, `VAR_FG`, `VAR_BORDER`, `VAR_PRIMARY_HOVER`, …) rather than literal colours, so the app honours light/dark themes and stays consistent (§8, §16).
- **Widget states.** Interactive widgets define hover/active/focus styling (`hover_background_color`, `active_transform`, focus treatment); focus is never suppressed without a replacement (§11, §18).
- **Accessibility commands and metadata.** Inputs and controls have accessible labels/roles; dynamic changes are announced with `GraphicalUi.announce(...)`; focus is managed with `GraphicalUi.focus(...)` after actions; see the GraphicalUi Guide §14 (Accessibility) (§18).
- **Layout containers & spacing.** Rows, columns, grids, and scrollable regions are used with consistent gaps and alignment from a spacing scale; nothing relies on manual pixel-nudging (§6, §7).
- **Feedback via commands/subscriptions.** Loading and progress are surfaced for asynchronous commands (HTTP, timers, file I/O); empty and error states have a designed appearance (§12).
- **Responsiveness & motion.** The view adapts to window size (GraphicalUi Guide §17) and animations honour reduced-motion (GraphicalUi Guide §13) (§13, §17).

## Output Format

For each finding, report:

1. **Location** — file path, and the screen, widget, or function involved (line range where useful).
2. **Severity** — one of:
    - **Critical** — blocks a task or makes the UI inaccessible (e.g. no keyboard path, contrast failure on essential text, destructive action with no confirmation or undo).
    - **Major** — significant friction or confusion (e.g. no feedback on a slow action, unclear hierarchy, colour-only status).
    - **Minor** — polish (e.g. inconsistent spacing, missing hover affordance).
    - **Suggestion** — an optional enhancement.
3. **Principle** — the `ux-design.instructions.md` section (and GraphicalUi Guide section, if relevant) the finding maps to.
4. **Description** — what the problem is and why it harms the experience.
5. **Recommendation** — a concrete fix, with a Luma/style snippet where it helps.

Group findings by file or screen. Prioritise Critical and accessibility issues first, then Major. This is an audit: report findings and recommendations, and apply fixes only if the user asks — the paired [ux-improve.prompt.md](ux-improve.prompt.md) executor consumes this report and applies the fixes.
