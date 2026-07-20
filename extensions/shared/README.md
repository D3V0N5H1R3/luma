# Shared Editor Resources

This directory contains resources shared across both Luma editor extensions.

## Contents

### Documentation

| Path                               | Purpose                                                     |
| ---------------------------------- | ----------------------------------------------------------- |
| `binary-download/SPECIFICATION.md` | Canonical binary download protocol specification (detailed) |
| `download-spec.md`                 | Concise download flow reference                             |
| `BINARY_RESOLUTION.md`             | Per-editor binary resolution order                          |
| `config-schema.md`                 | Configuration settings reference                            |
| `error-handling.md`                | Error severity contract for both extensions                 |
| `TEXTMATE_GENERATION.md`           | Why the VS Code TextMate grammar is hand-maintained         |

### Shared data

| Path                          | Purpose                                                 |
| ----------------------------- | ------------------------------------------------------- |
| `defaults.json`               | Canonical configuration schema and defaults             |
| `download-constants.json`     | Machine-readable download protocol constants            |
| `platform-map.json`           | Canonical platform→archive suffix mapping               |
| `keybindings.json`            | Canonical keybinding definitions                        |
| `resolution-order.json`       | Binary resolution order specification                   |
| `test-discovery-pattern.json` | Test discovery patterns for Luma test suites            |
| `sha256sums-sample.txt`       | Golden SHA256SUMS fixture for cross-editor parser tests |

### Code generators

| Path                        | Purpose                                                                                    |
| --------------------------- | ----------------------------------------------------------------------------------------- |
| `generate-config.py`        | Generates shared runtime constants (`config.ts`/`.rs`)                              |
| `generate-config-code.py`   | Generates editor-native config (package.json properties, typed accessors, default tables) |
| `generate-download-code.py` | Generates download protocol constants                                                      |
| `generate-platform-code.py` | Generates platform detection code                                                          |
| `generate-keybindings.py`   | Generates editor keybinding configs                                                        |
| `generate-test-discovery.py`| Generates VS Code test-discovery patterns                                                  |
| `generate-all.py`           | Convenience wrapper that runs all generators                                               |
| `codegen_common.py`         | Shared helpers used by all code generators                                                 |

### Tooling and tests

| Path                            | Purpose                                                  |
| ------------------------------- | -------------------------------------------------------- |
| `ci-check-generated.py`         | CI check: runs all generators and fails on any diff      |
| `test_generated_consistency.py` | Checks the runtime config constants match `defaults.json` |
| `sync-queries.py`               | Structural gate: editor tree-sitter queries vs canonical |
| `binary-download/download.py`   | Deprecated standalone downloader (reference / CI use)    |

### Assets

| Path                     | Purpose                                      |
| ------------------------ | -------------------------------------------- |
| `snippets/luma.json`     | Common code snippets consumed by all editors |
| `queries/highlights.scm` | Shared tree-sitter highlight queries         |

## Tree-sitter as the Canonical Grammar

The tree-sitter grammar in `extensions/zed/grammars/tree-sitter-luma/grammar.js` is the **single source of truth** for Luma syntax. All editor extensions should derive their highlighting from this grammar rather than maintaining independent syntax definitions.

### Current State

Each editor extension currently uses a different syntax highlighting mechanism:

| Editor | Current mechanism | Tree-sitter ready? |
| ------ | ----------------- | ------------------- |
| **VS Code** | TextMate grammar (`syntaxes/luma.tmLanguage.json`) | ⚠️ Hand-maintained by design (VS Code lacks native tree-sitter); guarded by `grammar.test.ts` |
| **Zed** | Native tree-sitter (`extension.toml` `[grammars.luma]` pointing to this grammar) | ✅ Already consuming the shared grammar |

### How Each Editor Consumes Tree-sitter

#### Zed

Zed uses tree-sitter as its only grammar format. The `[grammars.luma]` table in `extension.toml` already references this shared grammar.

**Integration path:**

1. Point the `[grammars.luma]` `repository` in `extension.toml` at this grammar (already done).
2. Provide `highlights.scm` in the Zed extension's `languages/` directory.

#### VS Code (TextMate)

VS Code does not support tree-sitter natively (as of 2025), so the extension ships a **hand-maintained** TextMate grammar (`syntaxes/luma.tmLanguage.json`). Auto-generating it from the tree-sitter grammar was investigated and rejected as disproportionately complex; see [`TEXTMATE_GENERATION.md`](./TEXTMATE_GENERATION.md) for the rationale.

**Keeping it in sync:**

1. When Luma syntax changes, update the tree-sitter grammar and the TextMate grammar together.
2. `extensions/vscode/src/test/suite/grammar.test.ts` guards the TextMate grammar's structure (scope name and required pattern includes).

> **Note:** If VS Code ships a stable native tree-sitter API, the TextMate grammar can be retired in favour of consuming the canonical grammar directly.

### Migration Plan

The migration to a fully unified grammar should proceed in phases:

1. **Phase 0 — Current state (done)**
   - Tree-sitter grammar exists and is complete.
   - Zed already consumes it directly.

2. **Phase 1 — VS Code TextMate grammar (done)**
   - The TextMate grammar is hand-maintained by design (see `TEXTMATE_GENERATION.md`).
   - `grammar.test.ts` guards its structure against accidental breakage.

3. **Phase 2 — VS Code tree-sitter native (future)**
   - When VS Code ships native tree-sitter support, switch from TextMate generation to direct tree-sitter integration.

### Contributing

When modifying Luma syntax:

1. Update `extensions/zed/grammars/tree-sitter-luma/grammar.js` first.
2. Run `tree-sitter generate` and `tree-sitter test` to validate.
3. Update Zed query files (`highlights.scm`, etc.) as needed.
4. Update the hand-maintained VS Code TextMate grammar to match, and run the extension test suites before committing.

All syntax PRs should include changes to the shared grammar. Editor-specific syntax files that diverge from the shared grammar are considered bugs.

> **CI check:** Run `python sync-queries.py --check` to verify that the editor query
> copy (Zed) has not *structurally* drifted from the canonical queries in
> `shared/queries/`. It compares the unordered multiset of node types, terminals and
> predicates, so intentional per-editor capture-group renames, reordering and
> reformatting pass, while a genuinely added or removed node/terminal fails the check.
> `python sync-queries.py --force` only *creates* a missing editor copy from the
> canonical source; it never overwrites an existing hand-adapted copy.

## Color Themes

The Luma VS Code extension ships with two color themes:

| Theme | File |
| ----- | ---- |
| Luma Dark | `extensions/vscode/themes/luma-dark-color-theme.json` |
| Luma Light | `extensions/vscode/themes/luma-light-color-theme.json` |

These themes define a curated color palette that gives Luma code a consistent look in VS Code. The Zed extension could derive its own theme from the same palette to provide a unified experience across editors.

### Future Work

Create a portable, editor-agnostic theme definition in `shared/themes/` that captures the canonical Luma color palette and semantic token mappings. Each editor extension would then generate its native theme format from this shared source:

- **VS Code** → JSON color theme files
- **Zed** → Zed theme JSON

This mirrors the approach used for the shared tree-sitter grammar: one source of truth with per-editor derivation. No shared theme files exist yet — this section documents the intended direction.
