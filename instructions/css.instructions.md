---
description: "Use when writing, reviewing, or modifying CSS source code (.css files). Covers naming, specificity, layout, responsive design, custom properties, and maintainable CSS patterns."
applyTo: "**/*.css"
---

# Working with CSS

These instructions govern how you write CSS source code. Every selector, property, and file you produce must follow these principles. They are aligned with the [MDN CSS documentation](https://developer.mozilla.org/en-US/docs/Web/CSS), [CSS specifications](https://www.w3.org/Style/CSS/), and common community conventions.

---

## Table of Contents

1. [Simplicity First](#1--simplicity-first)
2. [Naming Conventions](#2--naming-conventions)
3. [Consistent Style](#3--consistent-style)
4. [Selectors and Specificity](#4--selectors-and-specificity)
5. [Custom Properties](#5--custom-properties)
6. [Layout](#6--layout)
7. [Responsive Design](#7--responsive-design)
8. [Typography](#8--typography)
9. [Colours and Theming](#9--colours-and-theming)
10. [Spacing and Sizing](#10--spacing-and-sizing)
11. [Animations and Transitions](#11--animations-and-transitions)
12. [Accessibility](#12--accessibility)
13. [Self-Documenting Code](#13--self-documenting-code)
14. [Whitespace as Structure](#14--whitespace-as-structure)
15. [Performance](#15--performance)
16. [File Organisation](#16--file-organisation)
17. [Anti-Patterns](#17--anti-patterns)
18. [Checklist](#18--checklist)

---

## 1 — Simplicity First

Write the simplest CSS that achieves the desired visual result.

- Prefer straightforward selectors and standard properties over clever hacks.
- If a single property does what you need, do not use a complex workaround.
- Avoid over-abstracting — not every visual pattern needs a utility class.
- A developer unfamiliar with the codebase should understand your styles within minutes.

**Test:** Before committing to an approach, ask — _is there a simpler way?_

---

## 2 — Naming Conventions

Use a consistent naming methodology. Prefer BEM (Block-Element-Modifier) for component-scoped styles:

| Entity                | Convention         | Examples                                          |
| --------------------- | ------------------ | ------------------------------------------------- |
| Block                 | `kebab-case`       | `card`, `nav-bar`, `search-form`                  |
| Element               | `__` separator     | `card__title`, `nav-bar__link`                    |
| Modifier              | `--` separator     | `card--highlighted`, `button--disabled`           |
| Custom properties     | `--kebab-case`     | `--color-primary`, `--spacing-md`                 |
| Utility classes       | descriptive        | `visually-hidden`, `text-center`                  |
| JavaScript hooks      | `js-` prefix       | `js-toggle`, `js-modal-trigger`                   |
| State classes         | `is-`/`has-` prefix| `is-active`, `is-open`, `has-error`               |

- Name classes by purpose, not appearance. `error-message` — good. `red-text` — bad.
- Never use IDs for styling. Reserve IDs for JavaScript hooks and anchor targets.
- Avoid generic class names like `container`, `wrapper`, or `content` without a namespace.

---

## 3 — Consistent Style

- **Indentation:** 4 spaces. No tabs.
- **Line length:** One property per line. No multiple properties on a single line.
- **Semicolons:** Always include the trailing semicolon on the last property.
- **Braces:** Opening brace on the same line as the selector. Closing brace on its own line.
- **Quotes:** Double quotes for string values (`url("...")`, `content: ""`).
- **Shorthand:** Use shorthand properties when setting all related values. Use longhand when setting only one.
- **Zero values:** Omit units for zero values (`margin: 0`, not `margin: 0px`).
- **Colours:** Use lowercase hex (`#1a2b3c`) or `hsl()`/`oklch()`. Avoid named colours except `transparent` and `currentColor`.

```css
/* Good — consistent style. */
.card {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    padding: 1.5rem;
    border: 1px solid var(--color-border);
    border-radius: 0.5rem;
    background-color: var(--color-surface);
}

.card__title {
    margin: 0;
    font-size: 1.25rem;
    font-weight: 600;
}
```

---

## 4 — Selectors and Specificity

Keep specificity low and predictable. Specificity wars are a maintenance nightmare.

- Prefer class selectors over element selectors, ID selectors, or complex combinators.
- Avoid nesting selectors more than 2–3 levels deep.
- Never use `!important` unless overriding third-party styles with no other option. Document why.
- Avoid qualifying class selectors with elements (`div.card` → `.card`).
- Use `:where()` to zero out specificity when creating resets or defaults.
- Prefer `:is()` for grouping selectors without increasing specificity beyond the most specific argument.

```css
/* Good — low, predictable specificity. */
.nav-bar__link {
    color: var(--color-text);
    text-decoration: none;
}

.nav-bar__link:hover {
    color: var(--color-primary);
}

/* Bad — overly specific. */
header nav ul li a.nav-bar__link {
    color: var(--color-text);
}
```

---

## 5 — Custom Properties

Use CSS custom properties (variables) for theming, shared values, and configuration.

- Define global tokens on `:root`. Define component-scoped properties on the component block.
- Use a consistent naming prefix for design tokens (`--color-`, `--spacing-`, `--font-`, `--radius-`).
- Provide fallback values where appropriate: `var(--color-primary, #0066cc)`.
- Prefer custom properties over preprocessor variables for runtime theming.
- Keep token hierarchies shallow — avoid deeply nested references.

```css
:root {
    --color-primary: #0066cc;
    --color-surface: #ffffff;
    --color-text: #1a1a1a;
    --color-border: #e0e0e0;

    --spacing-xs: 0.25rem;
    --spacing-sm: 0.5rem;
    --spacing-md: 1rem;
    --spacing-lg: 1.5rem;
    --spacing-xl: 2rem;

    --radius-sm: 0.25rem;
    --radius-md: 0.5rem;
    --radius-lg: 1rem;

    --font-family-body: system-ui, -apple-system, sans-serif;
    --font-family-mono: "Cascadia Code", "Fira Code", monospace;
}

.button {
    --button-padding: var(--spacing-sm) var(--spacing-md);
    --button-radius: var(--radius-sm);

    padding: var(--button-padding);
    border-radius: var(--button-radius);
}
```

---

## 6 — Layout

- Use Flexbox for one-dimensional layouts (rows or columns).
- Use CSS Grid for two-dimensional layouts (rows and columns together).
- Avoid floats for layout. They are for wrapping text around images only.
- Use `gap` instead of margins on children for spacing within flex/grid containers.
- Prefer intrinsic sizing (`min-content`, `max-content`, `fit-content`) over fixed widths where appropriate.
- Use logical properties (`inline-size`, `block-size`, `margin-inline`) for internationalisation support.

```css
/* Good — grid for page layout. */
.page-layout {
    display: grid;
    grid-template-columns: 250px 1fr;
    grid-template-rows: auto 1fr auto;
    min-block-size: 100dvh;
}

/* Good — flexbox for component layout. */
.toolbar {
    display: flex;
    align-items: center;
    gap: var(--spacing-sm);
}
```

---

## 7 — Responsive Design

- Use a mobile-first approach. Write base styles for small screens, then add complexity with `min-width` media queries.
- Prefer relative units (`rem`, `em`, `%`, `vw`, `vh`, `dvh`) over fixed pixels for responsive sizing.
- Use `clamp()` for fluid typography and spacing that scales between breakpoints.
- Use container queries (`@container`) for component-level responsiveness when supported.
- Define breakpoints as custom properties or in a single location — never scatter magic numbers.
- Test layouts at every viewport width, not just at specific breakpoints.

```css
/* Good — mobile-first with clamp. */
.hero__title {
    font-size: clamp(1.5rem, 4vw + 0.5rem, 3rem);
}

/* Good — min-width breakpoint. */
.grid {
    display: grid;
    grid-template-columns: 1fr;
    gap: var(--spacing-md);
}

@media (min-width: 48rem) {
    .grid {
        grid-template-columns: repeat(2, 1fr);
    }
}

@media (min-width: 64rem) {
    .grid {
        grid-template-columns: repeat(3, 1fr);
    }
}
```

---

## 8 — Typography

- Use a type scale for consistent font sizes (e.g., modular scale or predefined steps).
- Set `line-height` unitlessly (e.g., `1.5`) so it scales with font size.
- Limit line length for readability — 60–80 characters (`max-inline-size: 65ch`).
- Use `rem` for font sizes to respect user preferences.
- Prefer `font-display: swap` for web fonts to avoid invisible text.
- Use `text-wrap: balance` or `text-wrap: pretty` for headings and short paragraphs.

```css
/* Good — consistent type scale. */
:root {
    --font-size-sm: 0.875rem;
    --font-size-base: 1rem;
    --font-size-lg: 1.25rem;
    --font-size-xl: 1.5rem;
    --font-size-2xl: 2rem;
}

.prose {
    max-inline-size: 65ch;
    font-size: var(--font-size-base);
    line-height: 1.6;
}

h1 {
    font-size: var(--font-size-2xl);
    line-height: 1.2;
    text-wrap: balance;
}
```

---

## 9 — Colours and Theming

- Define all colours as custom properties. Never hardcode colour values in component styles.
- Use semantic colour names (`--color-primary`, `--color-danger`) rather than descriptive ones (`--blue`, `--red`).
- Support dark mode with `prefers-color-scheme` media query or a class-based toggle.
- Use `oklch()` or `hsl()` for colour values — they are more intuitive to adjust than hex.
- Ensure sufficient contrast ratios (WCAG AA: 4.5:1 for text, 3:1 for large text and UI components).

```css
/* Good — theme with dark mode support. */
:root {
    --color-primary: oklch(55% 0.2 250);
    --color-surface: #ffffff;
    --color-text: #1a1a1a;
}

@media (prefers-color-scheme: dark) {
    :root {
        --color-surface: #1a1a1a;
        --color-text: #e8e8e8;
    }
}
```

---

## 10 — Spacing and Sizing

- Use a consistent spacing scale based on custom properties.
- Prefer `rem` for spacing that should scale with root font size.
- Use `em` for spacing relative to the current element's font size (padding in buttons, for example).
- Avoid magic numbers. If a value appears more than once, extract it into a custom property.
- Use `min()`, `max()`, and `clamp()` for constrained fluid sizing.

```css
/* Good — consistent spacing from scale. */
.stack > * + * {
    margin-block-start: var(--spacing-md);
}

.container {
    inline-size: min(100% - 2rem, 75rem);
    margin-inline: auto;
}
```

---

## 11 — Animations and Transitions

- Use `transition` for state changes (hover, focus, active). Use `@keyframes` for complex multi-step animations.
- Always specify which properties to transition — never use `transition: all`.
- Respect user preferences with `prefers-reduced-motion`.
- Keep animations short (150–300ms for micro-interactions, up to 500ms for larger transitions).
- Use `transform` and `opacity` for performant animations (they avoid layout/paint).
- Prefer CSS animations over JavaScript animation for declarative state changes.

```css
/* Good — targeted transition with motion preference. */
.button {
    transition: background-color 150ms ease-in-out, transform 150ms ease-in-out;
}

.button:hover {
    transform: translateY(-1px);
}

@media (prefers-reduced-motion: reduce) {
    .button {
        transition: none;
    }
}
```

---

## 12 — Accessibility

- Ensure all interactive elements have visible focus styles. Never remove `outline` without providing an alternative.
- Use `prefers-reduced-motion` to disable or reduce animations for users who request it.
- Do not rely solely on colour to convey information — use icons, text, or patterns as well.
- Ensure text contrast meets WCAG AA standards (4.5:1 for normal text, 3:1 for large text).
- Use `visually-hidden` patterns instead of `display: none` for screen-reader-only content.
- Test with keyboard navigation — all interactive elements must be reachable and operable.

```css
/* Good — visible focus with fallback. */
:focus-visible {
    outline: 2px solid var(--color-primary);
    outline-offset: 2px;
}

/* Good — visually hidden but accessible. */
.visually-hidden {
    position: absolute;
    inline-size: 1px;
    block-size: 1px;
    padding: 0;
    margin: -1px;
    overflow: hidden;
    clip-path: inset(50%);
    white-space: nowrap;
    border: 0;
}
```

---

## 13 — Self-Documenting Code

Write CSS that explains itself through clear naming and structure. Reserve comments for _why_, not _what_.

- Use descriptive class names that convey purpose.
- Comment non-obvious decisions (magic numbers, browser workarounds, z-index rationale).
- Delete stale or redundant comments.
- Use section comments (`/* === Section === */`) to delineate major areas in large files.

```css
/* Bad — restates the code. */
/* Set margin to 1rem. */
.card {
    margin: 1rem;
}

/* Good — explains a non-obvious constraint. */
/* Offset to align with the icon baseline in the adjacent column. */
.label {
    margin-block-start: 0.125rem;
}
```

---

## 14 — Whitespace as Structure

Use blank lines to reveal logical structure — like paragraphs in prose.

- **One blank line** between rule sets.
- **One blank line** between logical sections within a file.
- **No** multiple consecutive blank lines.
- **No** trailing whitespace. One trailing newline at end of file.
- Group related properties together within a rule set (layout → box model → typography → visual).

Property ordering within a rule set:

1. Layout (`display`, `position`, `grid-*`, `flex-*`, `float`, `clear`)
2. Box model (`width`, `height`, `margin`, `padding`, `border`)
3. Typography (`font-*`, `line-height`, `text-*`, `color`)
4. Visual (`background`, `box-shadow`, `opacity`, `cursor`)
5. Animation (`transition`, `animation`)

---

## 15 — Performance

- Avoid universal selectors (`*`) in complex selectors — they force the browser to check every element.
- Minimise selector complexity — browsers match selectors right-to-left.
- Use `will-change` sparingly and only on elements that will actually animate.
- Prefer `transform` and `opacity` for animations — they are composited without triggering layout.
- Avoid layout thrashing with frequent changes to properties like `width`, `height`, `top`, `left`.
- Use `contain` for elements whose internals do not affect the rest of the page.
- Prefer `content-visibility: auto` for off-screen content to improve rendering performance.

---

## 16 — File Organisation

- One component per file, named to match the component (`card.css`, `nav-bar.css`).
- Separate concerns: base/reset styles, tokens/variables, layout, components, utilities.
- Use `@import` or a build tool to compose files. Keep individual files small and focused.
- Order imports: reset → tokens → base → layout → components → utilities.
- Place media queries near the rules they modify, not in a separate file.

Typical structure:

```text
styles/
├── reset.css
├── tokens.css
├── base.css
├── layout.css
├── components/
│   ├── button.css
│   ├── card.css
│   └── nav-bar.css
└── utilities.css
```

---

## 17 — Anti-Patterns

- **`!important` for specificity battles.** Fix the specificity instead.
- **IDs for styling.** Use classes — IDs have unnecessarily high specificity.
- **Inline styles from JavaScript.** Use class toggles instead.
- **Magic numbers without explanation.** Extract to custom properties or add a comment.
- **`transition: all`.** Specify the exact properties to transition.
- **Overly generic class names.** Namespace to avoid collisions.
- **Pixel units for font sizes.** Use `rem` to respect user preferences.
- **Deeply nested selectors.** Keep specificity flat and predictable.
- **Duplicated values.** Extract to custom properties.
- **Removing focus outlines without replacement.** Always provide visible focus indication.

---

## 18 — Checklist

- [ ] All colours and spacing use custom properties — no hardcoded values in components.
- [ ] Specificity is low and predictable — no `!important`, no ID selectors for styling.
- [ ] Layout uses Flexbox or Grid — no floats for layout.
- [ ] Responsive design is mobile-first with `min-width` breakpoints.
- [ ] Focus styles are visible on all interactive elements.
- [ ] `prefers-reduced-motion` is respected for animations.
- [ ] Class names follow BEM or a consistent methodology.
- [ ] Properties are ordered consistently within rule sets.
- [ ] No magic numbers without explanation.
- [ ] Files end with a single trailing newline.
