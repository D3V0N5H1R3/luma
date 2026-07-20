# Extension Tests

Shared fixtures and consistency tests that validate Luma's editor extensions (VS Code and Zed) outside the main C++ interpreter test suite.

All scripts are plain Node.js and require **Node.js 18 or newer** (CI runs them on Node.js 22). The Tree-sitter checks additionally need the Tree-sitter CLI, installed via `npm install` in the grammar directory.

## Contents

| File or directory                      | Purpose                                                                                                                         |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| `fixtures/`                            | Representative `.luma` source files covering keywords, literals, generics, interfaces, concurrency, and standard library usage. |
| `parse_fixtures.js`                    | Parses every fixture with the Tree-sitter grammar and fails on `ERROR` or `MISSING` nodes.                                      |
| `validate_queries.js`                  | Validates the Tree-sitter query files (`.scm`) for Zed: syntax and expected highlight captures. |
| `validate-defaults.test.mjs`           | Checks each editor's generated config defaults against [`../shared/defaults.json`](../shared/defaults.json).                    |
| `validate-download.test.mjs`           | Checks each editor's generated platform map against [`../shared/platform-map.json`](../shared/platform-map.json) and parses the [`../shared/sha256sums-sample.txt`](../shared/sha256sums-sample.txt) fixture. |
| `validate-download-constants.test.mjs` | Checks each editor's generated download constants against [`../shared/download-constants.json`](../shared/download-constants.json). |
| `validate-resolution-order.test.mjs`   | Checks each editor's binary resolution order against [`../shared/resolution-order.json`](../shared/resolution-order.json).      |
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
node extensions/tests/parse_fixtures.js
node extensions/tests/validate_queries.js
```

> **Note:** The highlight-capture checks in `validate_queries.js` shell out to the `tree-sitter query` command and need the generated parser; they compile each highlights query against the grammar (catching dead "impossible" patterns) and assert the expected capture on known snippets. They are skipped when that command is unavailable.

### Cross-Editor Consistency Validators

Run the consistency validators directly with Node.js from the repository root:

```bash
node extensions/tests/validate-defaults.test.mjs
node extensions/tests/validate-download.test.mjs
node extensions/tests/validate-download-constants.test.mjs
node extensions/tests/validate-resolution-order.test.mjs
```

Each validator compares the per-editor generated code against its canonical source in [`../shared`](../shared). A mismatch usually means the generated files are stale and need regenerating — see [`../shared/README.md`](../shared/README.md).

## Continuous Integration

These checks also run automatically:

- `parse_fixtures.js` runs in the Zed workflow ([`.github/workflows/ci-zed.yml`](../../.github/workflows/ci-zed.yml)).
- The four `validate-*.test.mjs` validators run in the VS Code workflow ([`.github/workflows/ci-vscode.yml`](../../.github/workflows/ci-vscode.yml)).

These fixtures and validators exist to catch grammar and configuration regressions in editor tooling; interpreter behaviour is still covered by the C++ and Luma tests under [`../../tests`](../../tests).
