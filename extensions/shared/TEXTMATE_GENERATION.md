# TextMate Grammar: Hand-Maintained by Design

## Context

VS Code does not support tree-sitter natively (as of 2025), so the VS Code
extension ships a TextMate grammar (`extensions/vscode/syntaxes/luma.tmLanguage.json`)
while Zed uses the canonical tree-sitter grammar
(`extensions/zed/grammars/tree-sitter-luma/grammar.js`).

## Decision

The TextMate grammar is **hand-maintained**. Auto-generating it from the
tree-sitter grammar was investigated and **not pursued**: the two formalisms
differ fundamentally (tree-sitter is a GLR parser producing a concrete syntax
tree; TextMate is a line-oriented regex/scope engine), so a faithful,
maintainable converter is disproportionately complex relative to the size of
the grammar.

There is intentionally **no** `generate-textmate-grammar.js` script.

## Keeping it consistent

- `extensions/vscode/src/test/suite/grammar.test.ts` asserts the TextMate
  grammar's structure (scope name and the required top-level pattern includes),
  catching accidental breakage.
- When Luma syntax changes, update the tree-sitter grammar and the
  hand-maintained TextMate grammar together, and run the extension test suites.

## Future option

If VS Code ships a stable native tree-sitter API, the TextMate grammar can be
retired in favour of consuming the canonical tree-sitter grammar directly.

## See also

- [`DIRECTORY.md`](./DIRECTORY.md#tree-sitter-as-the-canonical-grammar) — How each editor consumes the canonical tree-sitter grammar.
