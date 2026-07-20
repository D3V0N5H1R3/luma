---
description: "Analyse a Luma editor extension (VS Code or Zed) read-only and produce a prioritized, actionable list of suspected bugs — without changing any code"
agent: "agent"
tools: ["search", "read"]
argument-hint: "Optional scope, e.g. 'the VS Code extension' or 'the shared grammar'"
---

# Bug Search — Editor Extension

Survey the Luma editor extensions and produce a **prioritized list of suspected bugs**. This prompt is the discovery counterpart to [bug-fix-editor-extension.prompt.md](bug-fix-editor-extension.prompt.md): this one *finds and ranks* candidate defects; that one *reproduces, root-causes, and fixes* a single chosen item with a regression test and the affected extension's checks green.

Two extensions live under `extensions/`: **VS Code** (`extensions/vscode/`, TypeScript) and **Zed** (`extensions/zed/`, Rust → WebAssembly + tree-sitter grammar). They share canonical data and code generators in `extensions/shared/`, so a defect may live in one editor, in the shared grammar, or in generated code that drifted from its source. This is a **read-only hunt**: make no code changes, and no build or parser regeneration is required — confirming each candidate with a live repro is the first step of [bug-fix-editor-extension.prompt.md](bug-fix-editor-extension.prompt.md), which is why every finding carries a **confidence** rating. The deliverable is a ranked report, not a diff.

> **Scope vs sibling prompts:** This hunt covers the editor extensions and their shared grammar and data. If the fault is in another subsystem, use the matching hunt instead: [bug-search.prompt.md](bug-search.prompt.md) for the interpreter core and standard library, [bug-search-language-server.prompt.md](bug-search-language-server.prompt.md) for the language server (`luma_lsp`), or [bug-search-debugger.prompt.md](bug-search-debugger.prompt.md) for the debugger (`luma_dap`). For an LSP- or DAP-backed feature, decide whether the fault is in the extension's **client-side wiring** (in scope here) or in the C++ **server/adapter** (use the LSP or DAP hunt). Stay on defects: [code-review.prompt.md](code-review.prompt.md) is a *deep, file-scoped* review that also weighs style and maintainability, while [refactor-audit.prompt.md](refactor-audit.prompt.md) and [consistency-check.prompt.md](consistency-check.prompt.md) own structure and drift — note and cross-reference such candidates rather than restating them here.

## 1 — Understand the Intended Behaviour

Before judging what looks wrong, learn the behaviour the extensions are meant to have:

- [Luma_Syntax_Highlighting.md](../../documents/Luma_Syntax_Highlighting.md) — token categories, TextMate scopes, and the tree-sitter query strategy any highlighting fix must preserve.
- [extensions/shared/README.md](../../extensions/shared/README.md) — the canonical-grammar and code-generation model: what is a single source of truth and what is generated from it.
- [extensions/FEATURE_PARITY.md](../../extensions/FEATURE_PARITY.md) — the cross-editor feature contract, including the **intentional** per-editor differences that are not bugs.
- [extensions/BINARY_ASSETS.md](../../extensions/BINARY_ASSETS.md) — the `{binary}-{os}-{arch}.{ext}` release-asset convention every download path must match.
- [learnings.instructions.md](../../instructions/learnings.instructions.md) — the **Editor Extensions** section (the highlight-query model and the child-order query pitfall) and the deliberate decisions in §6.
- The per-language style guide for the file you inspect: [typescript.instructions.md](../../instructions/typescript.instructions.md) / [javascript.instructions.md](../../instructions/javascript.instructions.md) (VS Code), [rust.instructions.md](../../instructions/rust.instructions.md) (Zed), and [css.instructions.md](../../instructions/css.instructions.md) for any webview styling.

## 2 — Scope and Ground Rules

- **Default scope** is both extensions and their shared layer: `extensions/vscode/`, `extensions/zed/`, `extensions/shared/`, and the tree-sitter grammar under `extensions/zed/grammars/tree-sitter-luma/`. If the invocation names one editor or the shared grammar, restrict the hunt to it.
- **Generated files are inspection targets, not fix targets.** Files under `src/generated/` (VS Code, Zed) are produced from canonical JSON by `extensions/shared/generate-*.py`. *Finding* that one has drifted from its source (or was hand-edited) is in scope; the fix always lands in the canonical source or generator, which the handoff should say.
- **In scope to flag, not to fix here:** when an LSP/DAP-backed feature misbehaves because of the C++ server or adapter rather than the client wiring, record it and point the handoff at [bug-search-language-server.prompt.md](bug-search-language-server.prompt.md) or [bug-search-debugger.prompt.md](bug-search-debugger.prompt.md).
- **Out of scope:** the interpreter core, language server, and debugger sources (use their hunts), vendored code (`external/`), compiled parsers/WebAssembly, `node_modules/`, and build outputs.
- **Verify every location.** Read each file you cite — never report a defect or line range you have not confirmed in the source.
- **Make no changes.** Do not edit, format, regenerate the parser, build, or run tests. Producing the ranked list is the whole job.

## 3 — What to Look For

Hunt for these defect classes. The triage-first structure and layer list mirror [bug-fix-editor-extension.prompt.md](bug-fix-editor-extension.prompt.md)'s isolation steps.

1. **Shared-vs-editor triage.** Decide first whether a candidate is shared or editor-specific: a fault that would appear in **both** editors points at the shared grammar, shared data, or a generator; a fault in one points at that editor's source. Check [FEATURE_PARITY.md](../../extensions/FEATURE_PARITY.md) — some differences are **intentional** (code lens, the playground, and the debug visualiser are VS Code only) and must not be reported as bugs.
2. **Shared grammar and queries.**
    - **Tree-sitter grammar** (`extensions/zed/grammars/tree-sitter-luma/grammar.js`) — the single source of truth for syntax; wrong node structure breaks Zed highlighting, folding, and indentation.
    - **Query child-order pitfall** — a highlight/fold/indent pattern whose child order does not match the grammar's `seq(...)` order **silently never matches** (and a stricter tree-sitter runtime rejects the *entire* query, killing all highlighting). For `generic_params: seq("<", commaSep1($.type_identifier), ">")` the capture must sit *between* the brackets — `("<" (type_identifier) @type.parameter ">")`, never `("<" ">" (type_identifier))`. A capture in the wrong position is a dead pattern.
    - **Highlight-copy structural drift** — the two `highlights.scm` copies (canonical `extensions/shared/queries/` and `extensions/zed/languages/luma/`) intentionally diverge on capture-*group* names but must match the same grammar *nodes*; a structural change (an added, removed, or reordered node) applied to only one copy is a bug that changes nothing a user sees, or breaks the editor copy.
3. **Shared canonical data and generated drift.** Wrong values in `extensions/shared/defaults.json`, `download-constants.json`, `platform-map.json`, `resolution-order.json`, `keybindings.json`, or `snippets/luma.json`; and any **generated** file that was hand-edited or has drifted from its canonical source (what `ci-check-generated.py` guards).
4. **Per-editor layers.**
    - **VS Code** (`extensions/vscode/`): activation and feature wiring (`src/extension.ts`, `src/utils/feature-registry.ts`); LSP client lifecycle (`src/lsp/`); DAP wiring and the debug visualiser webview (`src/debugger/`); the test runner and coverage (`src/testing/`, plus **generated** `src/generated/test-discovery.ts`); tasks and the playground (`src/tasks.ts`, `src/playground/`); binary download, checksum, and platform detection (`src/utils/binary-download.ts`, `src/utils/binary/`, plus **generated** `src/generated/platform.ts` and `download-constants.ts`); config (`src/utils/config.ts`, plus **generated** `src/generated/config.ts`); the **hand-maintained** TextMate grammar (`syntaxes/luma.tmLanguage.json`, guarded by `src/test/suite/grammar.test.ts`); editor behaviour (`language-configuration.json`, `themes/`); and the manifest `contributes` entries (`package.json`).
    - **Zed** (`extensions/zed/`): the extension entry, LSP/DAP launcher, and binary resolution (`src/lib.rs`); LSP label rendering (`src/labels.rs`); binary download, checksum, and the config `merge_json` (`src/download.rs`, `src/util.rs`, reading **generated** `src/generated/platform.rs`); the rest of the **generated** `src/generated/` config/constants; the manifest and language config (`extension.toml`, `languages/luma/config.toml`, `grammars/luma.toml`); and the tree-sitter queries (`languages/luma/*.scm`).
5. **Binary-asset contract.** Any download path whose constructed asset name diverges from the `{binary}-{os}-{arch}.{ext}` convention in [BINARY_ASSETS.md](../../extensions/BINARY_ASSETS.md) — a platform/arch mapping, extension, or separator that will 404 at download time.
6. **Cross-editor inconsistency.** Shared defaults, download constants, or platform mappings that no longer agree across editors (what the `extensions/tests/validate-*.test.mjs` contract tests guard) — often the tail of a one-editor edit to shared data.

## 4 — How to Gather Evidence

Use the workspace search and file-reading tools; parallelize independent read-only exploration. Do **not** build, regenerate the parser, or run tests.

- **Check query child order against the grammar.** For a suspect highlight/fold/indent capture, read the matching `seq(...)` rule in `grammar.js` and confirm the capture sits in the right child position. Reason about the pattern statically — do not run `tree-sitter highlight` (it degrades and can false-pass); the reliable check (`tree-sitter query`, via `extensions/tests/validate_queries.js`) is the fix step's job.
- **Diff generated against canonical.** For a suspect generated file, read its canonical JSON source and the relevant `generate-*.py` and confirm the output matches — a mismatch is drift that `ci-check-generated.py` would catch.
- **Trace a structural highlight change across both copies.** If a grammar node was added, removed, or reordered, confirm both `highlights.scm` copies reflect it.
- **Compare asset-name construction to the contract.** Read each editor's download code and confirm the `{binary}-{os}-{arch}.{ext}` assembly and platform/arch maps match [BINARY_ASSETS.md](../../extensions/BINARY_ASSETS.md).
- **Check shared-data edits across editors.** For a change to `extensions/shared/*.json`, confirm it stays consistent with what `validate-*.test.mjs` asserts.
- **Grep intent markers** — `TODO`, `FIXME`, `HACK`, `BUG`, `XXX` — and read the per-editor tests (`extensions/vscode/src/test/suite/`, `extensions/zed/src/tests.rs`) plus the shared `extensions/tests/`; a feature with thin coverage is where latent bugs survive — record the gap.

## 5 — Prioritize

Rank every candidate so the fixer picks the highest-value item first. Rate each on:

- **Severity** — impact if the defect is real. **Critical**: a dead highlights query that kills *all* highlighting, a download path that never resolves a binary, or an activation failure that breaks the extension. **High**: a broadly wrong highlight/fold/indent, a broken LSP/DAP feature, or a wrong default that misconfigures every user. **Medium**: a wrong scope on one construct, a wrong snippet, or a wrong keybinding. **Low**: cosmetic, or a narrow case with an easy workaround.
- **Confidence** — how sure it is a genuine bug given you did not run it. **High**: mechanism confirmed against the grammar/canonical source and a triggering document or platform is describable. **Medium**: clearly suspect, but the trigger needs confirmation. **Low**: a plausible smell that needs the fix step's live repro — say so.
- **Reach** — how many editors and users it affects: a shared-grammar or shared-data fault (both editors) outranks a single-editor edge case.
- **Effort** — rough fix size (Small / Medium / Large), independent of severity.

Rank by severity weighted by confidence, tie-broken by reach then effort. A shared-layer fault affecting both editors, or a dead-query regression, goes to the top. A high-severity, low-confidence item still belongs on the list, with "confirm the repro first" recorded as the fixer's prerequisite.

## 6 — What to Exclude

- **No fixes.** Reproducing and fixing is [bug-fix-editor-extension.prompt.md](bug-fix-editor-extension.prompt.md)'s job — do not edit code, canonical data, or generated output.
- **Not structure, style, or drift-as-consistency.** Behaviour-preserving structure belongs in [refactor-audit.prompt.md](refactor-audit.prompt.md); style in [code-review.prompt.md](code-review.prompt.md) / [lint-and-format.prompt.md](lint-and-format.prompt.md); broad artefact drift across the project in [consistency-check.prompt.md](consistency-check.prompt.md). (A *specific* generated-file or cross-editor-data drift that produces a user-visible defect is a legitimate bug finding — report it; a whole-tree consistency audit is not.)
- **Respect deliberate decisions that look like bugs.** Documented in [learnings.instructions.md](../../instructions/learnings.instructions.md) and [FEATURE_PARITY.md](../../extensions/FEATURE_PARITY.md):
    - **Intentional parity gaps** — code lens, the playground, and the debug visualiser are VS Code only; their absence in Zed is not a bug.
    - The `highlights.scm` copies intentionally **diverge on capture-group names** (e.g. canonical `@punctuation.special` vs Zed `@string.special`, plus Zed's reordering/reformatting) — only a divergence in the matched grammar **nodes** counts as a defect.
    - `extensions/shared/sync-queries.py --check` flags that intentional group-name divergence and exits non-zero, and is **not** wired into CI — treat its exit code as a coarse diff aid, not evidence of a bug; likewise `extensions/tests/validate_queries.js` is effectively local-only.
    - The **VS Code TextMate grammar is hand-maintained by design** (VS Code lacks native tree-sitter) — its existence alongside the tree-sitter grammar is intentional, not duplication to "fix".
- **No hallucinated findings.** Every entry needs a location you have opened and read.

## 7 — Output Format

Produce the report in two parts.

First, a summary table ordered by priority for quick scanning:

```markdown
| ID  | Suspected bug                              | Category          | Severity | Confidence | Effort |
| --- | ------------------------------------------ | ----------------- | -------- | ---------- | ------ |
| B01 | `generic_params` capture in wrong position | Shared grammar    | Critical | High       | Small  |
| B02 | Zed asset name misses `-arch` segment      | Binary-asset      | High     | Medium     | Small  |
| B03 | …                                          | …                 | …        | …          | …      |
```

Then, one detailed entry per candidate:

```markdown
### B01 — <Short, symptom-oriented title>

- **Category:** <one of the §3 classes>
- **Editor(s):** <VS Code | Zed | Shared>
- **Severity:** <Critical | High | Medium | Low>
- **Confidence:** <High | Medium | Low>
- **Location:** `path/to/file` (lines A–B), `path/to/other` (lines C–D)
- **Symptom / trigger:** <the observable failure, the editor interaction, and the document or platform that provokes it>
- **Root-cause hypothesis:** <the mechanism — traced to grammar node, canonical source, generator, or editor layer>
- **Suggested fix direction:** <a sketch, not a full patch; if it is generated code, name the canonical source to fix>
- **Regression test idea:** <the per-editor or shared fixture/test that would prove the fix>
- **Effort:** <Small | Medium | Large>
- **Handoff goal:** "<one-line goal string ready to paste into bug-fix-editor-extension.prompt.md>"
```

Close with a short note on what you would fix first and why (highest severity weighted by confidence, widest reach). Make each **Handoff goal** specific enough that [bug-fix-editor-extension.prompt.md](bug-fix-editor-extension.prompt.md) can act on it without re-discovering the problem.
