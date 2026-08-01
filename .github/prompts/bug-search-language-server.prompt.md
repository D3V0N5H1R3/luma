---
description: "Analyse the Luma language server (LSP) read-only and produce a prioritized, actionable list of suspected bugs — without changing any code"
agent: "agent"
tools: ["search", "read"]
argument-hint: "Optional scope, e.g. 'lsp_completion_provider.cpp' or 'the whole language server'"
version: 1
lastUpdated: "2026-08-01"
---

# Bug Search — Language Server

Survey the Luma language server (`luma_lsp`) and produce a **prioritized list of suspected bugs**. This prompt is the discovery counterpart to [bug-fix-language-server.prompt.md](bug-fix-language-server.prompt.md): this one *finds and ranks* candidate defects; that one *reproduces, root-causes, and fixes* a single chosen item with a regression test and the suite green.

The server reuses the interpreter front-end (lexer, parser, type checker) but never runs the VM, so most defects are wrong diagnostics, hover, completion, navigation, or coordinate conversion. This is a **read-only hunt**: make no code changes, and no build is required — confirming each candidate with a live repro is the first step of [bug-fix-language-server.prompt.md](bug-fix-language-server.prompt.md), which is why every finding carries a **confidence** rating. The deliverable is a ranked report, not a diff.

> **Scope vs sibling prompts:** This hunt covers the language server. If the fault is in another subsystem, use the matching hunt instead: [bug-search.prompt.md](bug-search.prompt.md) for the interpreter core and standard library, [bug-search-debugger.prompt.md](bug-search-debugger.prompt.md) for the debugger (`luma_dap`), or [bug-search-editor-extension.prompt.md](bug-search-editor-extension.prompt.md) for the editor extensions. Because the server reuses the interpreter front-end, a defect that surfaces in an LSP feature may still root-cause to the shared lexer, parser, or type checker — that front-end is [bug-search.prompt.md](bug-search.prompt.md)'s territory. Stay on defects: [code-review.prompt.md](code-review.prompt.md) is a *deep, file-scoped* review that also weighs style and maintainability, while [refactor-audit.prompt.md](refactor-audit.prompt.md) and [consistency-check.prompt.md](consistency-check.prompt.md) own structure and drift respectively — note and cross-reference such candidates rather than restating them here.

## 1 — Understand the Intended Behaviour

Before judging what looks wrong, learn the behaviour the server is meant to have:

- [Luma_Language_Server.md](../../documents/Luma_Language_Server.md) — the server's architecture, its feature surface, and the coordinate/encoding conventions any fix must preserve.
- [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) — how the server reuses the analysis pipeline it never runs to completion.
- [learnings.instructions.md](../../instructions/learnings.instructions.md) — the **Language Server (LSP)** section (component responsibilities and the codepoint-column rule) plus **C++ Pitfalls Discovered**, which make excellent bug signatures, and the deliberate decisions in §6.
- [cpp.instructions.md](../../instructions/cpp.instructions.md) — the C++ idioms whose violation is frequently the defect.

## 2 — Scope and Ground Rules

- **Default scope** is the whole server: `language-server/source/` and the shared libraries it consumes (`shared/protocol/`, `shared/stdlib/`, `shared/symbols/`) where the server's *use* of them is the suspect. If the invocation names a file, restrict the hunt to it and its immediate collaborators.
- **In scope to flag, not to fix here:** when a symptom (a wrong token, a wrong type string) roots in the shared front-end (lexer, parser, type checker), record it and point the handoff at [bug-search.prompt.md](bug-search.prompt.md) / [bug-fix.prompt.md](bug-fix.prompt.md) — the server's own code is this prompt's fix territory.
- **Out of scope:** the debugger (`debugger/`), the editor extensions (`extensions/`), vendored code (`external/`), generated code, and build outputs. Point findings there at the matching hunt.
- **Verify every location.** Read each file you cite — never report a defect or line range you have not confirmed in the source.
- **Make no changes.** Do not edit, format, build, or run tests. Producing the ranked list is the whole job.

## 3 — What to Look For

Hunt for these defect classes. The layer list mirrors [bug-fix-language-server.prompt.md](bug-fix-language-server.prompt.md)'s isolation steps, so a finding drops straight into the fix workflow.

1. **Coordinate and encoding conversion — the highest-yield LSP bug class.** Any column or range width computed from `lexeme.size()` / `name.size()` (**bytes**) instead of `lexeme_column_width()` (**codepoints**, via `lsp_token_utils.hpp`) is wrong for every multi-byte UTF-8 lexeme — string literals and non-ASCII identifiers — and corrupts hover/definition/rename ranges and applied edits. Also hunt 1-based source vs 0-based LSP `Position` off-by-ones and UTF-16 code-unit conversion in the diagnostic builder. Audit every column computation (~18 call sites across 9 files).
2. **Feature-handler correctness, by layer:**
    - **Protocol transport** (`lsp_transport*`, `shared/protocol/`) — Content-Length framing or JSON-RPC parsing.
    - **Dispatch and capabilities** (`lsp_server*`, `lsp_server_dispatch.cpp`, `lsp_handler_registry.hpp`, `lsp_capabilities.*`) — a request routed to the wrong handler, a capability advertised without a handler, or a handler omitted from the advertised capabilities.
    - **Analysis pipeline and indexing** (`lsp_analysis_*`, `lsp_analysis_result.hpp`, `lsp_token_index.hpp`, `lsp_scope_stack.*`, `lsp_identifier_collector.*`) — the token index, function-body ranges, scoped locals, identifier index, or call graph built wrong.
    - **Symbol resolution** (`lsp_symbol_resolver.*`, `lsp_definition_resolver.hpp`, `lsp_navigation_handler.hpp`, `lsp_rename_handler.hpp`) — the scope-aware lookup (enclosing-function locals → globals, binary search for the enclosing function, cross-file) backing hover, definition, references, rename, document highlight, and linked editing.
    - **Completion and signature help** (`lsp_completion_*`, `lsp_keyword_catalog.*`, `lsp_stdlib_registry.*`, `lsp_server_signature.cpp`) — wrong or missing items, wrong sort/filter text, or wrong signature help.
    - **Hover and type rendering** (`lsp_hover_*`, `lsp_type_formatter.hpp`) — the right symbol resolved but the rendered hover text or type/signature string wrong.
    - **Semantic tokens** (`lsp_semantic_tokens_handler.hpp`, `lsp_token_classifier.hpp`, `lsp_semantic_token_cache.hpp`) — wrong token type/modifier classification, or a token type the lexer can produce left unclassified.
    - **Code actions, quick fixes, refactoring, code lens** (`lsp_code_action_*`, `lsp_quickfix_handler.hpp`, `lsp_refactoring_provider.hpp`) — a wrong or missing edit.
    - **Formatting** (`lsp_formatting_handler.hpp`, `lsp_text_formatter.*`) — wrong edits or ranges from the standalone line/token formatter.
    - **Symbols and hierarchy** (`lsp_symbol_handler.hpp`, `lsp_hierarchy_handler.hpp`) — wrong outline, symbol kind, declaration range, or call/type hierarchy (which relies on the analysis call graph).
    - **Folding, inlay hints, document links, selection ranges** (`lsp_folding_handler.hpp`, `lsp_inlay_hint_handler.hpp`, `lsp_navigation_handler.hpp`, `lsp_brace_matcher.hpp`) — wrong ranges or hint placement.
    - **Include handling and workspace indexing** (`lsp_include_processor.*`, `lsp_workspace_*`, `lsp_persisted_index.*`) — wrong cross-file resolution, or stale/missing cross-file symbols.
3. **Caching and staleness.** A stale analysis cache, a stale semantic-token cache, a stale persisted index (missed content-hash validation), or an incremental **ranged edit** applied in place that drifts from the full-text fallback in `lsp_document_synchronizer.*`.
4. **Cancellation and concurrency.** A mishandled cancellation deadline, a partially cancelled analysis returning inconsistent results, or a data race on the shared analysis cache.
5. **Configuration negotiation.** Client-capability negotiation or a setting read (`lsp_configuration_manager.*`, `lsp_config.hpp`) that produces the wrong default or behaviour.

## 4 — How to Gather Evidence

Use the workspace search and file-reading tools; parallelize independent read-only exploration. Do **not** build or run.

- **Chase the codepoint rule.** Grep for `.size()` and `.length()` near anything that builds a column, `Position`, `Range`, or edit width, and confirm each uses `lexeme_column_width()` rather than a byte count.
- **Trace one request.** Pick a feature (hover, rename, semantic tokens) and walk a document from the request through dispatch → analysis result → the handler → the response; the defect sits where the coordinate space, scope, or cache assumption breaks.
- **Check the pairings.** Confirm every advertised capability has a registered handler and vice versa; confirm each cache has a validation/invalidation path.
- **Grep intent markers** — `TODO`, `FIXME`, `HACK`, `BUG`, `XXX`.
- **Read the tests** (`language-server/tests/lsp_test_*.cpp`). A feature with thin coverage is where latent bugs survive — record the coverage gap in the finding.

## 5 — Prioritize

Rank every candidate so the fixer picks the highest-value item first. Rate each on:

- **Severity** — impact if the defect is real. **Critical**: a crash, a hang, or an edit that corrupts the user's source. **High**: a wrong navigation/rename result or a wrong diagnostic that blocks the user on common input. **Medium**: a wrong range/position, hover text, or completion on an edge case. **Low**: cosmetic, or a narrow case with an easy workaround.
- **Confidence** — how sure it is a genuine bug given you did not run it. **High**: mechanism traced end-to-end and a triggering document is describable. **Medium**: clearly suspect, but the trigger path needs confirmation. **Low**: a plausible smell that needs the fix step's live repro — say so.
- **Reachability** — whether it fires on ordinary documents (multi-byte text, large files, cross-file includes) versus a rare shape. Higher reachability raises priority.
- **Effort** — rough fix size (Small / Medium / Large), independent of severity.

Rank by severity weighted by confidence, tie-broken by reachability then effort. A high-severity, low-confidence item still belongs on the list, with "confirm the repro first" recorded as the fixer's prerequisite.

## 6 — What to Exclude

- **No fixes.** Reproducing and fixing is [bug-fix-language-server.prompt.md](bug-fix-language-server.prompt.md)'s job — do not edit code.
- **Not structure, style, or drift.** Behaviour-preserving structure belongs in [refactor-audit.prompt.md](refactor-audit.prompt.md); style in [code-review.prompt.md](code-review.prompt.md) / [lint-and-format.prompt.md](lint-and-format.prompt.md); artefact drift (e.g. the keyword catalog vs the lexer, or advertised capabilities vs handlers) in [consistency-check.prompt.md](consistency-check.prompt.md). Note and cross-reference.
- **Respect deliberate decisions that look like bugs.** Documented in [learnings.instructions.md](../../instructions/learnings.instructions.md):
    - Function-body ranges held **twice** (a map plus a sorted vector) — a deliberate space/time trade-off for O(log n) enclosing-function lookup, not redundant storage.
    - `LspConfig::get()` briefly acquiring `update_mutex_` to copy the `shared_ptr` — the source comment claiming "without holding any lock" is imprecise, but the behaviour (a brief lock, then lock-free reads of the immutable snapshot) is correct and intentional.
    - Re-analysis is **full per change** (with incremental red-green tree updates) even though document sync is incremental — that is by design, not a missed optimization.
- **No hallucinated findings.** Every entry needs a location you have opened and read.

## 7 — Output Format

Produce the report in two parts.

First, a summary table ordered by priority for quick scanning:

```markdown
| ID  | Suspected bug                              | Category         | Severity | Confidence | Effort |
| --- | ------------------------------------------ | ---------------- | -------- | ---------- | ------ |
| B01 | Rename width uses `lexeme.size()` on emoji | Coordinates      | High     | High       | Small  |
| B02 | Semantic-token cache not invalidated on …  | Caching          | Medium   | Medium     | Small  |
| B03 | …                                          | …                | …        | …          | …      |
```

Then, one detailed entry per candidate:

```markdown
### B01 — <Short, symptom-oriented title>

- **Category:** <one of the §3 classes>
- **Severity:** <Critical | High | Medium | Low>
- **Confidence:** <High | Medium | Low>
- **Location:** `path/to/file.cpp` (lines A–B), `path/to/other.hpp` (lines C–D)
- **Symptom / trigger:** <the observable failure, the LSP request, and the document that provokes it>
- **Root-cause hypothesis:** <the mechanism — why the code is wrong, traced through the layer>
- **Suggested fix direction:** <a sketch, not a full patch; name the helper or idiom that applies>
- **Regression test idea:** <the `lsp_test_*.cpp` case that would prove the fix>
- **Effort:** <Small | Medium | Large>
- **Handoff goal:** "<one-line goal string ready to paste into bug-fix-language-server.prompt.md>"
```

Close with a short note on what you would fix first and why (highest severity weighted by confidence, most reachable). Make each **Handoff goal** specific enough that [bug-fix-language-server.prompt.md](bug-fix-language-server.prompt.md) can act on it without re-discovering the problem.
