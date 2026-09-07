# Editor Extension Feature Parity

This document tracks feature availability across Luma's editor extensions.

## Feature Matrix

| Feature | VS Code | Zed |
|---------|---------|-----|
| Syntax highlighting | ✅ TextMate | ✅ Tree-sitter |
| LSP integration | ✅ | ✅ |
| DAP debugging | ✅ | ✅ |
| Auto-complete | ✅ | ✅ |
| Hover information | ✅ | ✅ |
| Go to definition | ✅ | ✅ |
| Find references | ✅ | ✅ |
| Rename symbol | ✅ | ✅ |
| Code actions | ✅ | ✅ |
| Auto-download LSP | ✅ Automatic | ✅ Automatic |
| Test runner | ✅ Tasks | ✅ Runnables |
| Semantic tokens | ✅ | ✅ |
| Formatting | ✅ | ✅ |
| Signature help | ✅ | ✅ |
| Snippets | ✅ Shared (64) | ✅ Shared (64) |
| Language configuration | ✅ | ✅ |

> **Note:** Most language-intelligence features (auto-complete, hover, go-to-definition,
> find references, rename, code actions, semantic tokens, formatting, and signature help)
> are provided by the shared `luma_lsp` server, so parity across editors is inherent. The
> remaining row — test-runner UX — is an editor-specific integration where capabilities
> genuinely differ.

## Known Gaps

1. **Test running:** Both editors surface tests through lightweight, editor-native
   mechanisms rather than a bespoke UI — VS Code via the `luma` run/test tasks (and the
   editor-title Run buttons), Zed via inline tree-sitter runnables
   (`languages/luma/runnables.scm`).

## Grammar Consistency

| Editor | Grammar Type | Source |
|--------|-------------|--------|
| VS Code | TextMate (hand-maintained) | `extensions/vscode/syntaxes/luma.tmLanguage.json` |
| Zed | Tree-sitter | `extensions/zed/grammars/tree-sitter-luma/grammar.js` |

> **Note:** The VS Code TextMate grammar is hand-maintained by design (VS Code has no
> native tree-sitter support). Its structure is guarded by
> `extensions/vscode/src/test/suite/grammar.test.ts`; see
> `extensions/shared/TEXTMATE_GENERATION.md` for the rationale.
>
> **Note:** Zed consumes the `tree-sitter-luma` grammar. Its highlight queries are kept
> aligned with the canonical `extensions/shared/queries/` by
> `extensions/shared/sync-queries.py` (run with `--check` to detect drift).
