---
description: "Capture new development learnings and prune redundant or obsolete ones from the project's learnings file"
agent: "agent"
argument-hint: "Optional focus, e.g. 'this session's changes' or a subsystem like 'VM' or 'LSP'"
version: 1
lastUpdated: "2026-08-01"
---

# Update Learnings

Maintain [learnings.instructions.md](../../instructions/learnings.instructions.md) — the single, auto-loaded store of patterns, pitfalls, and non-obvious knowledge discovered during development. Because it is applied to **every** file (`applyTo: "**/*"`), it is injected into every session: keep it accurate, lean, and high-signal. Adding noise has a real cost, so capture aggressively but prune just as aggressively.

Perform both steps below. If a focus area is given, scope the work to it; otherwise review the whole file.

## What Counts as a Learning

Record knowledge that is **durable and non-obvious** — something a future contributor (or agent) would otherwise have to rediscover:

- Architectural decisions and the reasoning behind them.
- Non-obvious invariants, gotchas, and pitfalls (especially ones that already caused a bug).
- Cross-cutting patterns, key file locations, and module responsibilities.
- Build, test, tooling, and CI quirks that are not self-evident from the configs.

Do **not** record:

- Transient task notes, TODOs, or progress updates.
- Anything obvious from reading the code or already stated in a canonical guide.
- Verbatim duplication of `instructions/*.instructions.md` or `documents/*` — link to the canonical guide and keep only a quick-reference summary, mirroring the existing "Canonical guide: … The notes below are a quick-reference summary." pattern.

## 1. Capture New Learnings

1. Identify knowledge gained since the file was last updated — from recent changes, debugging, reviews, or the focus area provided.
2. Place each learning under the most appropriate existing `##` section (Architecture & Pipeline, Module Layout & Key Files, Type System, Testing, Language Server (LSP), Debugger (DAP), C++ Pitfalls Discovered, etc.). Add a new section only when nothing fits.
3. Write concise, self-contained bullet points. Prefer one dense bullet over several thin ones. Include concrete file paths, type names, and opcode/preset names where they aid recall.
4. If a learning refines or supersedes an existing bullet, edit that bullet in place rather than appending a near-duplicate.

## 2. Prune Redundant or Obsolete Learnings

Remove or correct entries that no longer earn their place:

- **Obsolete** — describes code, files, or behaviour that no longer exists or has changed. Verify against the current source before deciding; update if still partly true, delete if not.
- **Redundant** — duplicates another bullet or restates a canonical guide. Merge into one authoritative entry.
- **Stale references** — file paths, module names, presets, or links that have moved or been renamed. Fix or remove them.
- **Low-signal** — obvious, trivial, or transient notes that should never have been durable knowledge.

When in doubt, prefer a single sharper bullet over several overlapping ones. Do not discard still-valid knowledge while pruning.

## Conventions

- Follow [markdown.instructions.md](../../instructions/markdown.instructions.md): heading hierarchy, list style, and link formatting.
- Keep links relative and valid (the file lives in `instructions/`, so reference siblings as `[name](name.instructions.md)` and documents as `[name](../documents/name.md)`).
- Preserve the existing section ordering and the frontmatter (`description`, `applyTo: "**/*"`).
- Match the file's existing terse, factual voice — no filler.

## Verification

- Every file path, type name, and link still resolves to something in the current tree.
- No section duplicates another, and no bullet duplicates a canonical guide.
- The file reads as a coherent quick-reference, not a changelog.
- Markdown lints cleanly per the markdown conventions.
