---
description: "Use when writing, reviewing, or modifying Markdown documentation files. Covers structure, formatting, linking, and content guidelines for project documentation."
applyTo: "**/*.md"
---

# Working with Markdown

Guidelines for writing, reviewing, and maintaining Markdown documentation files. Every recommendation here prioritises **clarity, consistency, and maintainability**. When in doubt, choose the approach that is easiest for a newcomer to read.

For README-specific conventions (structure, badges, project-type templates), see [readme.instructions.md](readme.instructions.md).

---

## Table of Contents

1. [Core Principles](#1--core-principles)
2. [Formatting and Whitespace](#2--formatting-and-whitespace)
3. [Naming Conventions](#3--naming-conventions)
4. [Code Blocks](#4--code-blocks)
5. [Links](#5--links)
6. [Lists](#6--lists)
7. [Tables](#7--tables)
8. [Emphasis and Inline Formatting](#8--emphasis-and-inline-formatting)
9. [Blockquotes and Admonitions](#9--blockquotes-and-admonitions)
10. [Images and Diagrams](#10--images-and-diagrams)
11. [Table of Contents](#11--table-of-contents)
12. [Collapsible Sections](#12--collapsible-sections)
13. [Comments](#13--comments)
14. [Document Structure Patterns](#14--document-structure-patterns)
15. [Anti-Patterns](#15--anti-patterns)
16. [Checklist Before Committing](#16--checklist-before-committing)

---

## 1 — Core Principles

1. **Clarity over cleverness.** Write for someone encountering the document for the first time.
2. **Progressive disclosure.** Lead with the essentials, then layer in detail.
3. **Keep it current.** A stale document erodes trust faster than a missing one.
4. **Avoid content duplication.** Link to the authoritative source rather than copying content that will drift.
5. **Consistency.** Every Markdown file in the project should look like it was written by the same person.

---

## 2 — Formatting and Whitespace

### Headings

- Start every document with a single level-one heading (`#`) that identifies the document's subject.
- Use level-two headings (`##`) for top-level sections and level-three (`###`) for subsections.
- **Never skip heading levels.** Do not jump from `##` to `####`.
- Use Title Case for headings: capitalise the first word and all major words.
- Leave one blank line before and after every heading.

```markdown
# Document Title

## Section Heading

### Subsection Heading
```

### Paragraphs

- Keep paragraphs short — two to four sentences maximum.
- Separate paragraphs with one blank line.
- Do not hard-wrap lines at a fixed column. Let the renderer handle line length.
- Do not insert unnecessary line breaks within a paragraph. Use a single line for each sentence or let sentences flow naturally. A line break in the source creates a `<br>` or a new paragraph depending on the renderer — use them only when a visual break is intentional.

### Blank Lines

- Use one blank line to separate logical sections: between headings and body text, between paragraphs, between list items that contain multiple lines, and before and after code blocks, tables, and blockquotes.
- Do not use multiple consecutive blank lines.
- End every file with a single trailing newline character.

### Horizontal Rules

Use `---` on its own line, surrounded by blank lines, to separate major sections. Use sparingly — headings are usually sufficient.

---

## 3 — Naming Conventions

### File Names

- Use `PascalCase` with underscores for multi-word document names: `Luma_User_Manual.md`.
- Use `UPPER_CASE` for conventional root-level files: `README.md`, `CONTRIBUTING.md`, `LICENSE`, `SECURITY.md`.
- Use `kebab-case` with the `.instructions.md` suffix for instruction files: `cpp.instructions.md`, `cmake.instructions.md`.
- Always use the `.md` extension.

### Heading and Section Names

- Choose descriptive, meaningful headings that tell the reader what the section contains without needing to read the body.
- `Installation` — good. `Step 2` — bad (context-free).
- Keep headings concise — ideally under eight words.

### Anchors and Link Text

- Use descriptive link text that makes sense out of context. `See the [installation guide](./docs/install.md)` — good. `Click [here](./docs/install.md)` — bad.
- Anchor links must match the GitHub slug rules: lowercase, spaces replaced with hyphens, special characters stripped.

---

## 4 — Code Blocks

- Use fenced code blocks (triple backticks) with a language identifier for every snippet.
- Supported identifiers in this project: `cpp`, `cmake`, `bash`, `luma`, `yaml`, `json`, `text`, `markdown`, plus the per-language identifiers used by the style guides (`rust`, `python`, `typescript`, `javascript`, `css`, `powershell`) and the documentation-specific identifiers (`ebnf`, `scheme`, `toml`, `vim`).
- Do not use indented code blocks (four-space indent). Fenced blocks are clearer and support syntax highlighting.
- Keep code snippets focused — show only the relevant lines. If context is needed, add a brief comment above the block explaining what the snippet demonstrates.

````markdown
```cpp
const auto result = parse(input);
```
````

- For command-line examples, use `bash` and include the expected output only when it aids understanding.

````markdown
```bash
cmake --build build --parallel
```
````

---

## 5 — Links

### Relative Links

Use relative paths for references to other files within the repository. This ensures links work regardless of hosting platform.

```markdown
See [CONTRIBUTING.md](../CONTRIBUTING.md) for the contribution workflow.
```

### Absolute URLs

Use absolute URLs only for external resources (websites, specifications, third-party documentation).

### Link Maintenance

- Verify that referenced files and anchors exist before committing.
- When renaming or moving a file, update all links that point to it.
- Prefer linking to a specific section (`[Section](file.md#section-heading)`) over linking to a file and expecting the reader to search.

---

## 6 — Lists

### Unordered Lists

Use `-` (hyphen) as the bullet marker. Indent nested lists by 4 spaces.

```markdown
- First item
- Second item
    - Nested item
    - Another nested item
- Third item
```

### Ordered Lists

Use sequential numbers (`1.`, `2.`, `3.`, …). This makes the source readable as plain text without relying on the renderer.

```markdown
1. First step
2. Second step
3. Third step
```

### List Item Length

- Keep list items to one or two sentences. If an item needs a full paragraph, consider using a subsection instead.
- Separate multi-line list items with blank lines for readability.

---

## 7 — Tables

Use tables for structured reference data such as configuration options, CLI flags, or comparison matrices.

```markdown
| Column A | Column B | Column C |
| -------- | -------- | -------- |
| Value 1  | Value 2  | Value 3  |
```

- **Pad every cell so that all rows share the same column widths.** Pad with spaces so that each pipe character (`|`) aligns vertically across all rows — header, separator, and data rows alike. This makes tables readable as plain text, not only when rendered.
- Match the dash count in the separator row to the column width established by the header.
- Keep cell content short. If a cell needs a paragraph, the data probably belongs in a subsection instead.
- Always include the header separator row.

```markdown
# Good — columns are padded to uniform width.

| Name    | Type    | Default |
| ------- | ------- | ------- |
| timeout | integer | 30      |
| retries | integer | 3       |
| verbose | boolean | false   |

# Bad — ragged columns, hard to scan as text.

| Name | Type | Default |
| --- | --- | --- |
| timeout | integer | 30 |
| retries | integer | 3 |
| verbose | boolean | false |
```

---

## 8 — Emphasis and Inline Formatting

- Use **bold** (`**text**`) for key terms, UI labels, and important warnings.
- Use _italics_ (`*text*`) for emphasis, titles of external works, and introducing new terms.
- Use `backticks` for inline code: file names, function names, command names, variable names, and short code fragments.
- Do not combine bold and backticks — use one or the other.
- Do not use emphasis for entire paragraphs. If everything is bold, nothing is.

---

## 9 — Blockquotes and Admonitions

Use blockquotes with bold labels for warnings and notes:

```markdown
> **Note:** This requires CMake 3.21 or later.

> **Warning:** Running this command will delete all build artifacts.
```

Reserve admonitions for information that the reader must not miss. Overusing them dilutes their impact.

---

## 10 — Images and Diagrams

- Store images in a dedicated directory (e.g., `docs/images/`).
- Use descriptive alt text: `![Pipeline architecture diagram](docs/images/pipeline.png)` — good. `![](img.png)` — bad.
- Prefer text-based diagrams (Mermaid, ASCII art) over images when the content is simple enough. Text diagrams are searchable, diffable, and do not require external tools to update.

---

## 11 — Table of Contents

For documents with more than four or five sections, include a table of contents after the introductory paragraph.

```markdown
## Table of Contents

1. [Section One](#section-one)
2. [Section Two](#section-two)
3. [Section Three](#section-three)
```

- Use numbered lists for TOCs to reflect document order.
- Update the TOC whenever headings are added, removed, or renamed.

---

## 12 — Collapsible Sections

Use `<details>` for content that is useful but not essential on first read:

```markdown
<details>
<summary>Advanced configuration options</summary>

Content goes here.

</details>
```

Use sparingly. If the content is important enough to include, it is usually important enough to show.

---

## 13 — Comments

Markdown does not have a native comment syntax, but HTML comments work:

```markdown
<!-- TODO: Update this section after the v2.0 release. -->
```

Use comments for:

- Notes to future editors about non-obvious decisions.
- Temporary TODOs that should not appear in the rendered output.
- Marking sections that need review.

Do not leave stale comments. Remove them when the issue they reference is resolved.

---

## 14 — Document Structure Patterns

### Instruction / Guideline Documents

Follow the numbered-section pattern used throughout the `instructions/` directory:

```markdown
# Working with <Topic>

Introductory paragraph.

---

## 1 — Section Name

Content.

---

## 2 — Section Name

Content.

---

## N — Checklist

- [ ] Item
```

### Reference Documents

Use a table of contents, descriptive headings, and cross-references between sections. Place the most frequently needed information first.

### Concept / Design Documents

Lead with the problem statement and motivation, then describe the solution. End with trade-offs and alternatives considered.

---

## 15 — Anti-Patterns

1. **Skipping heading levels.** Going from `##` to `####` breaks document outline and accessibility.
2. **Walls of text.** Break long content into short paragraphs, lists, or tables.
3. **Bare URLs.** Always wrap URLs in a descriptive link: `[description](url)`.
4. **Missing language identifiers on code blocks.** Every fenced block needs one.
5. **Stale content.** Outdated instructions are worse than no instructions.
6. **Duplicating content.** Link to the authoritative source instead.
7. **Over-formatting.** If everything is bold, nothing stands out.

---

## 16 — Checklist Before Committing

- [ ] Document starts with a level-one heading matching its subject.
- [ ] No heading levels are skipped.
- [ ] Paragraphs are short (two to four sentences).
- [ ] All code blocks have a language identifier.
- [ ] All internal links resolve to files and anchors that exist.
- [ ] No multiple consecutive blank lines.
- [ ] File ends with a single trailing newline.
- [ ] No stale or placeholder content remains.
- [ ] Table of contents (if present) matches the actual headings.
- [ ] Formatting is consistent with other Markdown files in the project.
