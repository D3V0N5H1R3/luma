# Extension Tests

Shared fixtures and consistency tests that validate Luma's editor extensions (VS Code and Zed) outside the main C++ interpreter test suite.

All scripts are plain Node.js and require **Node.js 18 or newer** (CI runs them on Node.js 22). The Tree-sitter checks additionally need the Tree-sitter CLI, installed via `npm install` in the grammar directory.

## Contents

| File or directory                      | Purpose                                                                                                                         |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| `fixtures/`                            | Representative `.luma` source files covering keywords, literals, generics, interfaces, concurrency, and standard library usage. `manual_grammar_constructs.luma` covers the User Manual's formal grammar (§32) forms the grammar once missed: if-expressions, tuple destructuring, comparison patterns and `when` guards, record patterns, qualified type names, and a literal `$` in strings. |
| `parse-fixtures.js`                    | Parses every fixture with the Tree-sitter grammar and fails on `ERROR` or `MISSING` nodes.                                      |
| `validate-queries.js`                  | Validates the Tree-sitter query files (`.scm`) for Zed: syntax, expected highlight captures, which capture wins for a token (Zed applies the last matching pattern), and regex injections. |
| `validate-defaults.test.mjs`           | Checks each editor's generated config defaults against [`../shared/defaults.json`](../shared/defaults.json).                    |
| `validate-download.test.mjs`           | Checks each editor's generated platform map against [`../shared/platform-map.json`](../shared/platform-map.json) and parses the [`../shared/sha256sums-sample.txt`](../shared/sha256sums-sample.txt) fixture. |
| `validate-download-constants.test.mjs` | Checks each editor's generated download constants against [`../shared/download-constants.json`](../shared/download-constants.json). |
| `validate-resolution-order.test.mjs`   | Checks each editor's binary resolution order against [`../shared/resolution-order.json`](../shared/resolution-order.json).      |
| `validate-snippets.test.mjs`           | Checks the shared snippets (keyword-free placeholders, dictionary and result-arm syntax, interfaces as field lists, `spawn` inside `task_scope`), that each editor's copy matches, and — when a built `luma` is found (`$LUMA_BIN` or the CMake build tree) — that every snippet expands to syntactically valid Luma. |
| `package.json`                         | Marks this directory as a private (unpublished) Node package.                                                                   |

## Usage

### Tree-sitter Grammar and Query Tests

The Tree-sitter checks need a generated parser. From the grammar directory used by the Zed extension, install the CLI and generate the parser once:

```bash
cd extensions/zed/grammars/tree-sitter-luma
npm install
npx tree-sitter generate
```

Then run the grammar and query checks from the repository root:

```bash
node extensions/tests/parse-fixtures.js
node extensions/tests/validate-queries.js
```

> **Note:** The highlight-capture checks in `validate-queries.js` shell out to the `tree-sitter query` command and need the generated parser; they compile each highlights query against the grammar (catching dead "impossible" patterns) and assert the expected capture on known snippets. They are skipped when that command is unavailable.

### Cross-Editor Consistency Validators

Run the consistency validators directly with Node.js from the repository root:

```bash
node extensions/tests/validate-defaults.test.mjs
node extensions/tests/validate-download.test.mjs
node extensions/tests/validate-download-constants.test.mjs
node extensions/tests/validate-resolution-order.test.mjs
```

Each validator compares the per-editor generated code against its canonical source in [`../shared`](../shared). A mismatch usually means the generated files are stale and need regenerating — see [`../shared/DIRECTORY.md`](../shared/DIRECTORY.md).

## Continuous Integration

These checks also run automatically:

- `parse-fixtures.js` and `validate-snippets.test.mjs` run in the Zed workflow ([`.github/workflows/ci-zed.yml`](../../.github/workflows/ci-zed.yml)).
- The `validate-*.test.mjs` validators run in the VS Code workflow ([`.github/workflows/ci-vscode.yml`](../../.github/workflows/ci-vscode.yml)).

These fixtures and validators exist to catch grammar and configuration regressions in editor tooling; interpreter behaviour is still covered by the C++ and Luma tests under [`../../tests`](../../tests).
