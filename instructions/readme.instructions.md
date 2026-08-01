---
description: "Use when writing, reviewing, or modifying README files. Covers structure, section ordering, writing style, completeness checks, and update safety for project README documents."
applyTo: "**/README.md"
priority: reference
---

# Working with README Files

## Table of Contents

1. [Purpose](#1--purpose)
2. [Core Principles](#2--core-principles)
3. [Creating a New README](#3--creating-a-new-readme)
4. [Reading and Analysing an Existing README](#4--reading-and-analysing-an-existing-readme)
5. [Updating an Existing README](#5--updating-an-existing-readme)
6. [Formatting Reference](#6--formatting-reference)
7. [Anti-Patterns](#7--anti-patterns)
8. [Checklist Before Finalising](#8--checklist-before-finalising)

---

## 1 — Purpose

This document provides instructions for creating, reading, updating, and maintaining README files. Follow these guidelines to produce clear, consistent, and useful README documents for any project.

For general Markdown formatting, whitespace, naming, and style rules that apply to **all** Markdown files (not just READMEs), see [markdown.instructions.md](markdown.instructions.md). The rules below are README-specific.

---

## 2 — Core Principles

1. **Clarity over cleverness.** Write for someone encountering the project for the first time.
2. **Progressive disclosure.** Lead with the essentials, then layer in detail.
3. **Keep it current.** A stale README erodes trust faster than a missing one.
4. **Avoid content duplication.** Link to external docs rather than duplicating content that will drift.

---

## 3 — Creating a New README

### Determine the Project Type

Before writing, identify the context. The structure shifts depending on what the project is.

| Project Type         | Key Sections to Prioritise                           |
| -------------------- | ---------------------------------------------------- |
| API Service          | Authentication, endpoints, request/response examples |
| CLI Tool             | Installation, command reference, flags and options   |
| Data / ML Project    | Data sources, model details, reproduction steps      |
| Internal / Team Tool | Access, configuration, who to contact                |
| Library / Package    | Installation, API reference, usage examples          |
| Web Application      | Setup, environment variables, deployment             |

### Standard Structure

Use the following section order as a baseline. Include only sections that are relevant — omit any that add no value.

```markdown
# Project Name

One-sentence description of what this project does and why it exists.

## Quick Start

Minimal steps to go from zero to running.

## Prerequisites

Software, services, or access required before installation.

## Installation

Step-by-step setup instructions.

## Configuration

Environment variables, config files, and their accepted values.

## Usage

Common workflows and code examples.

## API Reference

Endpoints, methods, parameters, and response shapes (if applicable).

## Development

How to run tests, lint, build, and contribute.

## Deployment

How to ship to staging and production (if applicable).

## Troubleshooting

Known issues and their fixes.

## License

SPDX identifier or a link to the LICENSE file.
```

### Writing Rules

- Start the file with a level-one heading (`#`) matching the project name.
- Write the description as a single plain sentence directly beneath the heading — no badge wall, no logo, no blank lines of padding.
- Use level-two headings (`##`) for top-level sections and level-three (`###`) for subsections. Never skip heading levels.
- Keep paragraphs short — two to four sentences maximum.
- Use fenced code blocks with a language identifier for every snippet (`bash`, `python`, `json`, etc.).
- Prefer concrete examples over abstract explanations.
- Use relative links (`./docs/setup.md`) for in-repo references and absolute URLs for external resources.
- Do not hard-wrap lines. Let the renderer handle line length.

---

## 4 — Reading and Analysing an Existing README

When asked to review or summarise a README, follow this process.

### Step 1 — Read the File

```bash
cat README.md
```

If the file is large, read it in sections:

```bash
head -n 80 README.md # overview and setup
tail -n 40 README.md # footer, license, links
```

### Step 2 — Evaluate Completeness

Check whether the README answers these five questions:

1. **What** does this project do?
2. **Why** would someone use it?
3. **How** do you install and run it?
4. **How** do you contribute or get help?
5. **What** license applies?

Flag any question that is unanswered or unclear.

### Step 3 — Check Quality

Look for these common problems:

- **Outdated instructions.** Compare install commands and version numbers against `package.json`, `pyproject.toml`, `Cargo.toml`, or equivalent.
- **Broken links.** Verify that referenced files and URLs exist.
- **Missing code fences.** Inline commands should be in backticks; multi-line snippets in fenced blocks.
- **Ambiguous prerequisites.** "You need Node" is not helpful. "Requires Node 20 or later" is.
- **Wall of text.** Large paragraphs with no headings or visual breaks.

---

## 5 — Updating an Existing README

### When to Update

- A dependency version requirement changes.
- A setup step is added, removed, or reordered.
- An environment variable or config key is introduced.
- A section is factually wrong or misleading.
- The project scope or name changes.

### How to Update Safely

1. **Read the entire file first.** Understand the existing structure before changing anything.
2. **Make the smallest effective change.** Do not restructure a working README just to match a preferred template.
3. **Preserve the author's voice.** Match the existing tone, heading style, and formatting conventions.
4. **Validate code snippets.** If you change a command or code example, verify it runs correctly.
5. **Update the table of contents** (if one exists) after any heading changes.

### Generating a Table of Contents

If the README is long enough to benefit from navigation, generate a TOC from its headings:

```markdown
## Table of Contents

1. [Quick Start](#quick-start)
2. [Prerequisites](#prerequisites)
3. [Installation](#installation)
4. [Configuration](#configuration)
5. [Usage](#usage)
6. [API Reference](#api-reference)
7. [Development](#development)
8. [Deployment](#deployment)
9. [Troubleshooting](#troubleshooting)
10. [License](#license)
```

Anchor links must match the GitHub/GitLab slug rules: lowercase, spaces replaced with hyphens, special characters stripped.

---

## 6 — Formatting Reference

### Badges

Place badges on a single line directly below the description, separated by spaces. Only include badges that provide genuinely useful status information.

```markdown
![CI](https://github.com/org/repo/actions/workflows/ci.yml/badge.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
```

Do not add badges purely for decoration.

### Tables

Use tables for structured reference data such as environment variables or CLI flags:

```markdown
| Variable       | Required | Default | Description                  |
| -------------- | -------- | ------- | ---------------------------- |
| `DATABASE_URL` | Yes      | —       | PostgreSQL connection string |
| `PORT`         | No       | `3000`  | HTTP listen port             |
```

### Admonitions

Use blockquotes with bold labels for warnings and notes:

```markdown
> **Note:** This requires Docker 24 or later.

> **Warning:** Running the migration will drop all existing data.
```

### Collapsible Sections

Use `<details>` for content that is useful but not essential on first read:

```markdown
<details>
<summary>Advanced configuration options</summary>

Content goes here.

</details>
```

---

## 7 — Anti-Patterns

1. **Starting with a logo and twenty badges.** The first thing a reader needs is a clear sentence explaining what the project does.
2. **Documenting every function.** A README is not API documentation. Link to generated docs instead.
3. **Assuming context.** Do not write "run the usual setup." Spell it out.
4. **Ignoring non-happy paths.** Include a troubleshooting section for errors people actually hit.
5. **Copy-pasting a template without tailoring it.** Empty placeholder sections ("TODO") are worse than no section at all. Remove anything you cannot fill in.
6. **Mixing install instructions for multiple platforms without clear separation.** Use headings or tabs to separate macOS, Linux, and Windows steps.

---

## 8 — Checklist Before Finalising

Use this checklist before delivering any new or updated README:

- [ ] Level-one heading matches the project name.
- [ ] First paragraph explains what the project does in one or two sentences.
- [ ] All code blocks have a language identifier.
- [ ] Commands can be copied and run without modification (no placeholder paths left unexplained).
- [ ] Internal links resolve to files that exist in the repository.
- [ ] No heading levels are skipped.
- [ ] Prerequisites list specific version requirements.
- [ ] License section is present or the file links to a LICENSE file.
- [ ] API Reference section documents endpoints, methods, and response shapes (if applicable).
- [ ] Deployment section describes how to ship to staging and production (if applicable).
- [ ] Troubleshooting section covers common errors users are likely to encounter.
- [ ] No empty or placeholder sections remain.
