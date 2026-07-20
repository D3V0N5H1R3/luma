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
| Inlay hints | ✅ Configurable | ✅ Configurable |
| Code lens | ✅ | ❌ |
| Playground | ✅ | ❌ |
| Debug visualiser | ✅ | ❌ |
| Auto-download LSP | ✅ Automatic | ✅ Automatic |
| Test runner | ✅ Native | ✅ Runnables |
| Semantic tokens | ✅ | ✅ |
| Formatting | ✅ | ✅ |
| Signature help | ✅ | ✅ |
| Snippets | ✅ Shared (64) | ✅ Shared (64) |
| Language configuration | ✅ | ✅ |

> **Note:** Most language-intelligence features (auto-complete, hover, go-to-definition,
> find references, rename, code actions, semantic tokens, formatting, signature help, and
> inlay hints) are provided by the shared `luma_lsp` server, so parity across editors is
> inherent. The remaining rows — code lens, playground, debug visualiser, and test-runner
> UX — are editor-specific integrations where capabilities genuinely differ.

## Known Gaps

1. **Zed:** No code lens (reference counts above functions and types) — VS Code only
2. **Zed:** No playground command — VS Code only
3. **Zed:** No debug visualiser webview — VS Code only
4. **Zed:** Test running is surfaced through inline tree-sitter runnables (`languages/luma/runnables.scm`), not a dedicated Test Explorer (VS Code)

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
