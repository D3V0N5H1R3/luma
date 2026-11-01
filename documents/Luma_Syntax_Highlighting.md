# Luma — Syntax Highlighting

This document covers the token categories and scope strategy for syntax highlighting Luma source files (`.luma`), and the design, file layout, and implementation strategy for the Visual Studio Code and Zed editor extensions.

---

## Table of Contents

1. [Token Design](#1--token-design)
    - [Goals](#goals)
    - [Token Categories](#token-categories)
    - [Rule Ordering](#rule-ordering)
    - [Non-Goals](#non-goals)
2. [Visual Studio Code Extension](#2--visual-studio-code-extension)
    - [Overview](#overview)
    - [File Layout](#file-layout)
    - [`package.json` — Extension Manifest](#packagejson--extension-manifest)
    - [`language-configuration.json` — Editor Behaviour](#language-configurationjson--editor-behaviour)
    - [`syntaxes/luma.tmLanguage.json` — TextMate Grammar](#syntaxeslumatmlanguagejson--textmate-grammar)
    - [Build Process](#build-process)
    - [Theme Compatibility](#theme-compatibility)
3. [Zed Extension](#3--zed-extension)
    - [Overview](#overview-1)
    - [File Layout](#file-layout-1)
    - [`extension.toml` — Extension Manifest](#extensiontoml--extension-manifest)
    - [`[grammars.luma]` — Grammar Reference](#grammarsluma--grammar-reference)
    - [`languages/luma/config.toml` — Language Configuration](#languageslumaconfigtoml--language-configuration)
    - [Tree-sitter Grammar — `tree-sitter-luma`](#tree-sitter-grammar--tree-sitter-luma)
    - [`languages/luma/highlights.scm` — Highlight Queries](#languageslumahighlightsscm--highlight-queries)
    - [`languages/luma/brackets.scm` — Bracket Matching](#languageslumabracketsscm--bracket-matching)
    - [`languages/luma/indents.scm` — Indentation](#languageslumaindentsscm--indentation)
    - [Build Process](#build-process-1)
    - [Theme Compatibility](#theme-compatibility-1)
4. [Semantic Tokens from the Language Server](#4--semantic-tokens-from-the-language-server)

- [See Also](#see-also)

---

## 1 — Token Design

This section defines the token categories, scope strategy, and design rationale for syntax highlighting Luma source files. It applies to both the TextMate grammar (VS Code) and the tree-sitter highlight queries (Zed).

### Goals

- Make the logical structure of Luma programs immediately visible to the reader.
- Distinguish control flow, declarations, types, literals, and operators with consistent, purposeful colour categories.
- Follow established conventions (TextMate scopes for editors, tree-sitter queries for Zed) so themes apply colour without extension-specific configuration.
- Keep the grammar maintainable: prefer clear, layered rules over monolithic regexes.

### Token Categories

#### 1. Comments

**Syntax:** `#` followed by any text to the end of the line.

```luma
# This is a comment
number x = 42 # trailing comment
```

**Scope:** `comment.line.number-sign.luma`

Comments are visually de-emphasised. No sub-highlighting inside comment text.

---

#### 2. Annotations

**Syntax:** `@main`, `@test`

```luma
@main
function void main() { ... }

@test
function void test_add() { ... }
```

**Scope:** `entity.name.tag.annotation.luma`

Annotations are rare, structurally significant markers. They should stand out visually (often rendered in a warm accent colour by themes).

---

#### 3. Keywords

Keywords are split into sub-categories so that themes can colour control flow differently from declarations and modifiers.

##### 3a. Control Flow

`break`, `case`, `catch`, `continue`, `else`, `finally`, `for`, `if`, `in`, `match`, `return`, `try`, `when`, `while`

`when` is contextual — it only introduces a match-arm guard (`case >= 0 when n == 0 { … }`) and is not reserved elsewhere.

**Scope:** `keyword.control.luma`

##### 3b. Declaration Keywords

`await`, `choice`, `function`, `include`, `interface`, `namespace`, `record`, `spawn`, `task_scope`, `type`, `use`

**Scope:** `keyword.declaration.luma`

##### 3c. Storage Modifiers

`borrow`, `internal`, `mutable`, `unique`

**Scope:** `storage.modifier.luma`

##### 3d. Other Keywords

`downcast`, `is`, `trusted_downcast`, `with`

**Scope:** `keyword.other.luma`

---

#### 4. Built-in Types

`array`, `boolean`, `channel`, `dictionary`, `integer`, `key_value_store`, `number`, `optional`, `queue`, `reference`, `result`, `set`, `socket`, `stack`, `string`, `task`, `xml`

**Scope:** `storage.type.luma`

`none` is excluded from this category — it is a language constant (see §5a) rather than a standalone type, since `none` is the empty case of `optional<T>`. Including it in the type rule would conflict with literal matching.

The `function` keyword appears both as a declaration keyword and as a type keyword (e.g. `function(integer) -> boolean`). When used as a type it carries the `storage.type.luma` scope as part of the surrounding type annotation.

`void` is included here (as a return type) rather than in `keyword.other`.

---

#### 5. Language Constants

##### 5a. Boolean and Null

`false`, `none`, `true`

**Scope:** `constant.language.luma`

##### 5b. Value Constructors and Built-in Functions

`assert(...)`, `failure(...)`, `print(...)`, `some(...)`, `success(...)`, `type_of(...)`

**Scope:** `support.function.builtin.luma` (on the identifier), `constant.language.luma` for bare `none`.

`success`, `failure`, and `some` are syntactically function calls that carry semantic meaning as data constructors. `print`, `assert`, and `type_of` are built-in functions. All six are highlighted as built-in callables to distinguish them from user-defined calls. The lookahead `(?=\s*[(])` restricts the match to call sites so bare identifiers in other positions are not affected.

---

#### 6. String Literals

##### 6a. Double-Quoted Strings

```luma
string s = "hello, world"
```

**Scope:** `string.quoted.double.luma`

##### 6b. Multi-Line Triple-Quoted Strings

```luma
string s = """
  Multi-line
  content here
"""
```

**Scope:** `string.quoted.triple.luma`

Triple-quoted strings are matched before double-quoted strings in the rule list so the longer delimiter wins.

##### 6c. String Escape Sequences

`\"`, `\$`, `\0`, `\\`, `\n`, `\r`, `\t`

**Scope:** `constant.character.escape.luma`

##### 6d. String Interpolation

`${expr}` inside any string literal.

```luma
string msg = "value is ${x + 1}"
```

- The `${` and `}` delimiters: `punctuation.section.interpolation.begin.luma` / `punctuation.section.interpolation.end.luma`
- The embedded expression: recurse into the full grammar (`$self`)

This allows full syntax highlighting inside string interpolations — a key differentiator from simpler approaches.

---

#### 7. Numeric Literals

##### 7a. Hexadecimal

`0xFF`, `0xDEAD_BEEF`

**Scope:** `constant.numeric.hex.luma`

##### 7b. Binary

`0b1010`, `0b1111_0000`

**Scope:** `constant.numeric.binary.luma`

##### 7c. Floating-Point

`3.14`, `1e6`, `2.5e-3`

**Scope:** `constant.numeric.float.luma`

##### 7d. Decimal Integer

`0`, `42`, `-100`

**Scope:** `constant.numeric.integer.luma`

Decimal integers are matched last; hex and binary must be tested first.

---

#### 8. Entity Names

##### 8a. Function Names

The identifier directly following the `function` keyword (after optional generics and return type).

```luma
function number add(number a, number b) { ... }
function<T> T identity(T value) { ... }
```

**Scope:** `entity.name.function.luma`

##### 8b. Type Declaration Names

The identifier directly following `record`, `choice`, `interface`, `namespace`, or `type`.

```luma
record Point { number x, number y }
choice Shape { Circle(number r) Rectangle }
```

**Scope:** `entity.name.type.luma`

---

#### 9. Module Member Access

`Module.member` where the module starts with an uppercase letter.

```luma
Array.map(nums, fn)
Math.pi
String.length(s)
```

- Module name: `support.class.luma`
- Member name: `support.function.luma`

Standard library modules (`Array`, `Math`, `String`, etc.) are all PascalCase and distinct from user-defined lowercase identifiers. This pattern reliably highlights them without a hardcoded list.

---

#### 10. Operators

Operators are grouped by semantic role for theme flexibility.

| Group             | Symbols                                      | Scope                                  |
| ----------------- | -------------------------------------------- | -------------------------------------- |
| Arithmetic        | `+` `-` `*` `/` `//` `%`                     | `keyword.operator.arithmetic.luma`     |
| Assignment        | `=` `+=` `-=` `*=` `/=` `%=` `//=` `++` `--` | `keyword.operator.assignment.luma`     |
| Comparison        | `==` `!=` `<` `>` `<=` `>=`                  | `keyword.operator.comparison.luma`     |
| Lambda arrow      | `->`                                         | `keyword.operator.arrow.luma`          |
| Logical           | `&&` `\|\|` `!`                              | `keyword.operator.logical.luma`        |
| Optional chaining | `?.` `?[`                                    | `keyword.operator.optional-chain.luma` |
| Optional unwrap   | `??`                                         | `keyword.operator.optional.luma`       |
| Pipe              | `\|>` `!>`                                   | `keyword.operator.pipe.luma`           |
| Range             | `..` `..=`                                   | `keyword.operator.range.luma`          |

Luma has **no bitwise operators** — bitwise operations use the `Bits` module (`Bits.and`, `Bits.or`, `Bits.xor`, `Bits.not`, `Bits.shift_left`, `Bits.shift_right`). The lexer does not tokenise `&`, `^`, `~`, or `<<` as operators; `|` is only a match-pattern alternative and `>>` only closes nested generics, so neither is highlighted as an arithmetic/bitwise operator.

### Rule Ordering

Rules must be evaluated in this order to avoid mis-matches:

1. Triple-quoted strings (before double-quoted)
2. Comments
3. Annotations
4. Numbers (hex and binary before decimal)
5. Language constants (`false`, `none`, `true`)
6. Built-in call constructors (`assert`, `failure`, `print`, `some`, `success`)
7. Storage types
8. Control flow keywords
9. Multi-token declarations (function name, type name) — **must come before declaration keywords** so the full `function <return> <name>(` pattern is consumed before the bare `function` keyword rule would fire
10. Declaration keywords
11. Storage modifiers
12. Other keywords
13. Operators (longest-first within each group: `..=` before `..`, `//=` before `//`, etc.)
14. Module member access

### Non-Goals

- **Semantic colouring** (e.g. distinguishing used vs unused variables) — requires a language server and is outside the scope of syntax highlighting.
- **Error markers** — handled by the type checker / language server, not the syntax grammar.
- **Indentation-based folding beyond bracket matching** — the language uses explicit `{}`/`[]` delimiters.

---

## 2 — Visual Studio Code Extension

This section describes the design, file layout, and implementation strategy for the Luma language extension for Visual Studio Code.

### Overview

The VSCode extension provides:

- **Syntax highlighting** via a TextMate grammar (`.tmLanguage.json`).
- **Language configuration** for bracket matching, comment toggling, auto-indentation, and folding.
- **File association** so `.luma` files are automatically recognised.

The extension does **not** require a language server. It is a pure grammar extension installable as a `.vsix` package.

### File Layout

```text
extensions/vscode/
├── .vscodeignore               # Files excluded from the packaged extension
├── language-configuration.json # Editor behaviour (brackets, comments, indentation)
├── package-lock.json           # Locked dependency versions
├── package.json                # Extension manifest (VSCode entry point)
├── DIRECTORY.md                   # Extension documentation
├── tsconfig.json               # TypeScript compiler configuration
│
├── images/                     # Extension icons and images
│   ├── icon.png
│   ├── icon.svg
│   ├── luma-file-dark.svg
│   └── luma-file-light.svg
│
├── snippets/
│   └── luma.json              # Code snippets
│
├── src/
│   ├── debugger/
│   │   └── debug.ts               # Debug adapter integration
│   ├── generated/                 # Auto-generated code (platform maps, config)
│   ├── lsp/
│   │   ├── client-manager.ts      # LSP client lifecycle management
│   │   ├── commands.ts            # LSP-related commands
│   │   └── types.ts               # LSP type definitions and guards
│   ├── utils/
│   │   ├── binary/                # Binary download submodules (split from binary-download.ts)
│   │   │   ├── types.ts           # Release/asset/config types and GitHub release parsing
│   │   │   ├── platform.ts        # Platform asset naming and bundled-binary path resolution
│   │   │   ├── archive.ts         # Archive extraction (.zip / .tar.gz)
│   │   │   ├── resolve.ts         # Download and the 4-step resolution order
│   │   │   └── update.ts          # Update checking and atomic replacement
│   │   ├── binary-download.ts     # Barrel re-exporting the binary/ submodules
│   │   ├── checksum.ts            # SHA-256 checksum verification
│   │   ├── config.ts              # Re-exports the generated typed config accessor (LumaConfig)
│   │   ├── constants.ts           # Shared constants, config keys, and type patterns
│   │   ├── feature-registry.ts    # Feature registration
│   │   ├── html.ts                # HTML escaping utility
│   │   ├── http.ts                # HTTP utilities (fetch, redirect)
│   │   └── util.ts                # General utilities
│   ├── extension.ts               # Extension entry point (activation, LSP client)
│   ├── tasks.ts                   # Task provider
│   └── test/                      # Extension tests
│
├── syntaxes/
│   ├── luma.markdown-injection.json # Markdown fenced code block injection
│   └── luma.tmLanguage.json         # TextMate grammar
│
├── themes/
│   ├── luma-dark-color-theme.json   # Dark colour theme
│   └── luma-light-color-theme.json  # Light colour theme
```

### `package.json` — Extension Manifest

The manifest registers:

- **Extension identity:** `publisher.luma-language`, display name `Luma Language Support`.
- **Language contribution:** associates the `luma` language ID with `.luma` file extensions.
- **Grammar contribution:** binds `source.luma` scope to the TextMate grammar file.
- **Language configuration contribution:** points to `language-configuration.json`.
- **Engine requirement:** `vscode ^1.75.0` (first version with stable grammar API used here).
- **Category:** `Programming Languages`.

No activation events are needed — grammar-only extensions activate lazily on file open.

### `language-configuration.json` — Editor Behaviour

| Feature              | Configuration                                                     |
| -------------------- | ----------------------------------------------------------------- |
| Auto-closing pairs   | `{}`, `[]`, `()`, `""` (triple-quote handled via `notIn` context) |
| Bracket pairs        | `{}`, `[]`, `()`                                                  |
| Folding              | Marker-based: `{` open, `}` close                                 |
| Indentation decrease | Lines starting with `}`                                           |
| Indentation increase | Lines ending with `{`                                             |
| Line comment toggle  | `#` prefix (VS Code comment toggle command)                       |
| On Enter             | One indent level after an opening bracket (no extra `appendText`) |
| Surrounding pairs    | Same as auto-closing                                              |
| Word pattern         | `[a-zA-Z_][a-zA-Z0-9_]*` (standard identifier pattern)            |

### `syntaxes/luma.tmLanguage.json` — TextMate Grammar

The grammar uses the `oniguruma` regex engine (standard for TextMate grammars and VSCode).

#### Top-Level Pattern Inclusion Order

Patterns are included in this order inside the `patterns` array (see [Token Design](#1--token-design) for full rationale):

1. `#string_triple`
2. `#comment`
3. `#annotation`
4. `#number`
5. `#constant`
6. `#for_variable`
7. `#typed_binding`
8. `#type`
9. `#keyword_control`
10. `#function_declaration` — **must precede `#keyword_declaration`** so the full `function <return> <name>(` pattern is consumed before the bare `function` keyword rule fires
11. `#type_declaration` — same rationale as `#function_declaration`
12. `#keyword_declaration`
13. `#storage_modifier`
14. `#with_field`
15. `#record_literal_field`
16. `#keyword_other`
17. `#named_argument`
18. `#function_call`
19. `#constructor_call`
20. `#operator`
21. `#module_access`
22. `#string` (double-quoted, after triple-quoted)

#### Key Repository Rules

`#string_triple` — Uses `begin`/`end` delimiters `"""`. Embeds `#interpolation` and `#string_escape`.

`#string` — Uses `begin`/`end` delimiters `"`. Embeds `#interpolation` and `#string_escape`. Must come after `#string_triple`.

`#interpolation` — Uses `begin`/`end` `${`/`}`. Recursively includes `$self` so the full grammar applies inside interpolations.

`#for_variable` — Matches the loop-variable binding in `for x in …` and `for k, v in …`. Both variable names receive `variable.other.luma`. Must come before `#type` and `#keyword_control` so the identifiers are captured before `for` and `in` are consumed individually.

`#typed_binding` — Matches a built-in type followed immediately by an identifier (e.g. `integer count`, `array<array<integer>> grid` — one level of nested generics). Captures the type as `storage.type.luma` and the name as `variable.other.luma`. Must come before `#type` so the combined pattern wins over a bare type match.

`#lambda` — Begin/end rule for a lambda's parameter list: a `(` whose matching `)` (with no nested parentheses) is followed by `->`. Lambda parameters are always typed, so each `[mutable|unique|borrow] Type name` entry scopes the name as `variable.parameter.luma`.

`#function_declaration` — Matches the pattern:

```text
(\bfunction\b)(?:\s*<[^>]*>)?\s+[a-zA-Z0-9_<>(), \t.-]+?\s+([a-z_][a-zA-Z0-9_]*)\s*(?=\()
```

Capture group 2 → `entity.name.function.luma`. The return-type class includes `.` so a namespace-qualified return type (`DateTime.TimeParts`) still isolates the function name; the pattern is non-greedy to stop at the name before `(`.

`#type_declaration` — Single match covers `record`, `choice`, `interface`, `namespace`, and `type` followed by an uppercase identifier:

```text
\b(record|choice|interface|namespace|type)\s+([A-Z][a-zA-Z0-9_]*(?:<[^>]*>)?)
```

- Group 1 → `keyword.declaration.luma`
- Group 2 → `entity.name.type.luma`

`#with_field` — Begin/end rule that opens on `with {` and highlights field names before `=` inside the record-update block.

`#record_literal_field` — Begin/end rule that opens on `TypeName[<…>] {` and highlights field names before `=` inside a record literal.

`#named_argument` — Matches `identifier:` call-argument labels (lookahead excludes `::`) and applies `variable.parameter.luma`.

`#function_call` — Matches lowercase `identifier(` or `identifier::<` call sites as `variable.function.luma`.

`#constructor_call` — Matches `PascalCaseIdentifier(` as `variable.function.luma` (choice variant constructors and similar).

`#module_access` — Matches `UpperCaseIdentifier.identifier`:

```text
\b([A-Z][a-zA-Z0-9_]*)\.([a-zA-Z_][a-zA-Z0-9_]*)
```

- Group 1 → `support.class.luma`
- Group 2 → `support.function.luma`

### Build Process

The extension uses `@vscode/vsce` to package itself into a `.vsix` file.

```bash
npm install     # install vsce as dev dependency
npm run package # runs: vsce package --no-dependencies
```

The output `luma-language-<version>.vsix` can be installed with:

```bash
code --install-extension luma-language-<version>.vsix
```

No transpilation, no bundler. The grammar JSON is used as-is.

### Theme Compatibility

The grammar uses standard TextMate scopes from the [TextMate naming conventions](https://macromates.com/manual/en/language_grammars#naming_conventions). All major VSCode themes (Dark+, Monokai, One Dark Pro, Dracula, etc.) assign colours to these standard scopes without any extension-specific theme configuration.

Custom theme authors can also target Luma-specific sub-scopes (e.g. `keyword.operator.pipe.luma`) for fine-grained control.

---

## 3 — Zed Extension

This section describes the design, file layout, and implementation strategy for the Luma language extension for the Zed editor.

### Overview

The Zed extension provides:

- **Syntax highlighting** via a tree-sitter grammar and Scheme highlight queries.
- **Language configuration** for file association, comment style, bracket pairs, and indentation.
- **Bracket matching** and **indentation hints** via dedicated `.scm` query files.

Zed's extension system is built around tree-sitter. Syntax highlighting is defined as a set of tree-sitter queries (`.scm` files) that annotate AST nodes with highlight names, which themes then map to colours.

### File Layout

```text
extensions/zed/
├── Cargo.toml             # Rust crate manifest (Zed extension API)
├── extension.toml         # Extension manifest
│
├── grammars/
│   └── tree-sitter-luma/  # Tree-sitter grammar source
│       ├── grammar.js     # Grammar definition (tree-sitter DSL)
│       ├── package.json   # Node.js project manifest
│       └── src/           # Generated parser source
│
├── languages/
│   └── luma/
│       ├── brackets.scm   # Bracket matching queries
│       ├── config.toml    # Language configuration
│       ├── folds.scm      # Code folding queries
│       ├── highlights.scm # Highlight queries
│       ├── indents.scm    # Indentation queries
│       ├── injections.scm # Language injection queries
│       ├── outline.scm    # Symbol outline queries
│       ├── overrides.scm  # Scope override queries
│       ├── redactions.scm # Sensitive data redaction queries
│       ├── runnables.scm  # Runnable detection queries (@main/@test → task tags)
│       ├── tasks.json     # Task templates for the runnable tags (run/test)
│       └── textobjects.scm # Vim text-object queries
│
└── src/
    └── lib.rs             # Extension entry point (Rust)
```

The tree-sitter grammar source lives in a separate repository (`tree-sitter-luma`) referenced by the `[grammars.luma]` table in `extension.toml`. It can also be co-located in the extension repository for development.

### `extension.toml` — Extension Manifest

Declares:

- **`id`:** `luma`
- **`name`:** `Luma`
- **`version`:** semantic version string
- **`schema_version`:** `1` (current Zed extension schema)
- **`description`:** Short one-line description
- **`authors`:** Author list
- **`repository`:** Source repository URL
- **`snippets`:** `["snippets/luma.json"]` — a top-level key, so it must come before the first `[table]` header (written below `[lib]`, TOML files it as `lib.snippets` and Zed never loads the snippets)
- **`[lib]`:** `kind = "Rust"` — Zed only builds and loads the WASM component (and so registers the debug adapter) for a Rust library
- **`[grammars.luma]`:** Declares the `luma` tree-sitter grammar — its `repository`, `path` within it, and `rev` (see below)
- **`[language_servers]`:** Declares the `luma-lsp` language server so Zed can connect to the Luma LSP binary when it is available on the user's `PATH` or at the path configured in the extension settings

### `[grammars.luma]` — Grammar Reference

The `[grammars.luma]` table in `extension.toml` points to the tree-sitter grammar:

```toml
[grammars.luma]
repository = "https://github.com/D3V0N5H1R3/luma"
rev = "main"
path = "extensions/zed/grammars/tree-sitter-luma"
```

The query files under `languages/luma/` are written against one version of the grammar, so the grammar and the queries must come from the same commit — a query naming a node the fetched grammar lacks makes Zed reject the whole file. Development builds track `main`; the release workflow (`.github/workflows/release-zed.yml`) rewrites `rev` in the packaged `extension.toml` to the tagged commit's full SHA. Zed reads this single declaration — there is no separate per-grammar manifest file.

### `languages/luma/config.toml` — Language Configuration

| Key                | Value                                                                       |
| ------------------ | --------------------------------------------------------------------------- |
| `autoclose_before` | `;:.,=}])>` — characters before which an opening bracket is auto-closed     |
| `block_comment`    | none (Luma has no block comments)                                           |
| `brackets`         | `{}`, `[]`, `()` (auto-closed, newline-expanded) and `""` (not in strings or comments) |
| `debuggers`        | `["Luma"]` — the extension's debug adapter                                  |
| `grammar`          | `luma`                                                                      |
| `line_comments`    | `["# "]`                                                                    |
| `name`             | `Luma`                                                                      |
| `path_suffixes`    | `["luma"]`                                                                  |
| `tab_size`         | 4                                                                           |

The `not_in = ["string", "comment"]` scopes of the quote pair are defined by `languages/luma/overrides.scm`.

### Tree-sitter Grammar — `tree-sitter-luma`

The grammar is defined in `grammar.js` using the tree-sitter DSL and follows the formal grammar in the [User Manual](Luma_User_Manual.md) (§32). Key node types:

| Node type               | Description                                                              |
| ----------------------- | ------------------------------------------------------------------------ |
| `annotation`            | `@main`, `@test`                                                         |
| `array_literal`         | `[...]`                                                                  |
| `boolean_literal`       | `true` / `false`                                                         |
| `call_expression`       | `fn(args)`, `Module.fn(args)`, `record.handler(args)`, `make(1)(2)`      |
| `choice_declaration`    | `choice Name { ... }` (variants optionally comma-separated)              |
| `comment`               | `# ...` line                                                             |
| `comparison_pattern`    | `case >= 90 { ... }` match arm pattern                                   |
| `function_declaration`  | Full function including name, params, body                               |
| `function_name`         | The identifier naming the function                                       |
| `if_expression`         | `if cond { ... } else { ... }` — an expression, also used as a statement |
| `interface_declaration` | `interface Name { ... }` (comma-separated fields)                        |
| `interpolation`         | `${expr}` inside strings                                                 |
| `lambda_expression`     | `(params) -> expr`                                                       |
| `match_expression`      | `match expr { case ... }`                                                |
| `match_guard`           | `when cond` refining a match arm                                         |
| `namespace_declaration` | `namespace Name { ... }`                                                 |
| `number_literal`        | Decimal, hex, binary, float                                              |
| `pipe_expression`       | `expr \|> Module.fn()`                                                   |
| `qualified_identifier`  | `Module.member`, `Type.Variant`, `Namespace.Type.Variant`                |
| `qualified_type`        | `Module.Type` in a type position (`DateTime.TimeParts parts = ...`)      |
| `range_expression`      | `a..b`, `a..=b`                                                          |
| `record_declaration`    | `record Name { ... }`                                                    |
| `record_destructure`    | `Line { start, finish } = segment`                                       |
| `record_pattern`        | `case Point { x, y } { ... }`                                            |
| `source_file`           | Root node                                                                |
| `string`                | Double-quoted string (a `$` not followed by `{` is literal text)         |
| `triple_string`         | Triple-quoted string                                                     |
| `tuple_destructure`     | `(number x, number y) = coords()`                                        |
| `type_alias`            | `type Name = ...`                                                        |
| `variable_declaration`  | Type + name + `=` + expr, or a tuple/record destructuring                |

The interpreter starts a new statement at a `(` that begins a line. The grammar cannot see line breaks (it has no external scanner), so calls through a field or of a call result carry no static precedence: the parser keeps both readings and drops the call when the next line is, for example, a tuple destructuring.

### `languages/luma/highlights.scm` — Highlight Queries

The highlight queries map tree-sitter node types (and captured nodes) to highlight names. Zed themes map highlight names to colours.

When several patterns capture the same node, Zed applies the **last** one. The general fallbacks `(identifier) @variable` and `(type_identifier) @type` therefore come first and every specific pattern after them; a fallback placed at the end would override all of them. `extensions/tests/validate-queries.js` checks which capture wins for representative tokens.

Key highlight names used:

| Highlight name          | Tokens                                                                                |
| ----------------------- | ------------------------------------------------------------------------------------- |
| `attribute`             | `annotation` nodes                                                                    |
| `comment`               | `comment` nodes                                                                       |
| `constant.builtin`      | `false`, `none`, `true`                                                               |
| `constructor`           | `failure`, `some`, `success`                                                          |
| `function.call`         | Function names in call expressions                                                    |
| `function`              | Function names in declarations                                                        |
| `keyword.control`       | `else`, `for`, `if`, `match`, `return`, `while`, etc.                                 |
| `keyword.modifier`      | `borrow`, `internal`, `mutable`, `unique`                                             |
| `keyword.operator`      | `downcast`, `trusted_downcast`, `is` (matched in expression context)                  |
| `keyword`               | Declaration and control-flow keywords                                                 |
| `module`                | Module names in member expressions                                                    |
| `number`                | Number literal nodes                                                                  |
| `operator`              | All operator tokens                                                                   |
| `punctuation.bracket`   | `{`, `}`, `[`, `]`, `(`, `)`                                                          |
| `punctuation.delimiter` | `,` `:`                                                                               |
| `string.escape`         | Escape sequences inside strings                                                       |
| `string.special`        | Interpolation delimiters `${` `}`                                                     |
| `string`                | String node content                                                                   |
| `type.builtin`          | `array`, `boolean`, `integer`, `number`, `optional`, `result`, `string`, `void`, etc. |
| `type`                  | Type names in declarations                                                            |

### `languages/luma/brackets.scm` — Bracket Matching

Defines matching pairs for editor bracket highlighting:

```scheme
("{" @open "}" @close)
("[" @open "]" @close)
("(" @open ")" @close)
```

### `languages/luma/indents.scm` — Indentation

Captures every node delimited by a bracket pair as `@indent`, with `@end` on the closing bracket — `(_ "{" "}" @end) @indent`, and likewise for `[]` and `()`. Zed indents the lines inside a captured node, so the capture must be the whole node: an `@indent` on the opening `{` token alone spans a single line and never indents the body.

### `languages/luma/outline.scm` — Outline

Lists functions, records, choices, interfaces, namespaces, type aliases, includes, and file- or namespace-level variables (locals inside function bodies are excluded). Each pattern carries an `@item` capture, which Zed requires; a standalone `(annotation) @annotation` attaches `@main`/`@test` to the function that follows it.

### Build Process

#### Grammar Compilation

The tree-sitter grammar must be compiled before the extension can be used:

```bash
cd grammars/tree-sitter-luma
npm install
npx tree-sitter generate # produces src/parser.c and headers
```

#### Local Installation

Zed loads extensions from `~/.config/zed/extensions/`. For local development:

```bash
# Install extension locally (Linux/macOS)
cp -r extensions/zed ~/.config/zed/extensions/luma

# On Windows
xcopy extensions\zed %APPDATA%\Zed\extensions\luma /E /I
```

After copying, restart Zed and open a `.luma` file.

#### Extension Registry

To publish to the Zed extension registry, submit a pull request to the [zed-industries/extensions](https://github.com/zed-industries/extensions) repository with the extension manifest.

### Theme Compatibility

Zed's built-in themes (One Dark, Solarized Light, Gruvbox, etc.) use the standard highlight names listed above. No Luma-specific theme configuration is required.

---

## 4 — Semantic Tokens from the Language Server

The grammars above colour text by its shape. The language server (`luma_lsp`) adds `textDocument/semanticTokens/full`, which colours identifiers by what they *resolve to* — a function, a parameter, a record type. In VS Code, a semantic token overrides the TextMate scope of the text it covers; text the server leaves untokenised keeps its grammar scope. The token legend (indices are fixed by `SemanticTokenType` in `lsp_types.hpp`):

| Index | Token type  | Assigned to                                                                                   |
| ----- | ----------- | --------------------------------------------------------------------------------------------- |
| 0     | `namespace` | Standard library module names (`Math`, `String`, …), with the `readonly` modifier            |
| 1     | `type`      | Built-in type keywords, `void`, the container/handle type names, user records/choices/aliases |
| 2     | `function`  | User-defined functions (`definition` modifier on the declaration's name)                      |
| 3     | `variable`  | Other identifiers (`readonly` unless declared `mutable`)                                      |
| 4     | `parameter` | Parameters of the enclosing function                                                          |
| 5     | `keyword`   | Word keywords (`if`, `function`, `mutable`, …)                                                |
| 6     | `string`    | String literals and the literal segments of interpolated strings                              |
| 7     | `number`    | Integer and number literals                                                                   |
| 8     | `operator`  | Operators                                                                                     |
| 9     | `decorator` | Annotations (`@main`, `@test`)                                                                |

To stay consistent with the grammar scopes in [§1](#token-categories):

- `void` and the container/handle type names (`set`, `queue`, `stack`, `task`, `channel`, `reference`, `socket`, `xml`, `key_value_store`) lex as identifiers but are tokenised as `type`, matching `storage.type.luma`, where they stand in a type position — before `<` or a name (`set<integer>`, `socket s`, `function void main`), or after `<` or `->`. They are also valid field and variable names, so a field (`integer task`, `job.task`), a named argument, or a variable or parameter the document declares with that name is tokenised as a `variable`.
- `true`, `false`, `none` (§5a) and `success`, `failure`, `some` (§5b) are not tokenised, so they keep `constant.language.luma` and `support.function.builtin.luma`. The LSP legend has no standard constant type, and a `keyword` token would override those scopes.
- The `${` and `}` delimiters of an interpolation are not tokenised and keep their grammar scope; the text between them is tokenised like any other code.
- A triple-quoted string is tokenised once per line it covers, because LSP semantic tokens cannot span lines.

---

## See Also

- [Contributing](../CONTRIBUTING.md) — installing the editor extensions
- [Language Server](Luma_Language_Server.md) — semantic editor features (diagnostics, hover, completion, semantic tokens)
- [Manual Tests](Luma_Manual_Tests.md) — manual test plan covering the VS Code and Zed extensions
