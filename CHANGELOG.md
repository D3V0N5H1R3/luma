# Changelog

All notable changes to Luma are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Luma is in its **alpha** series (current version `0.13.0`) and will adopt
[Semantic Versioning](https://semver.org/spec/v2.0.0.html) at `1.0`. Until then,
minor releases may include breaking language and standard-library changes; each
is called out with **Breaking** below, and the migration steps are collected at
the end of every release.

## [Unreleased]

_Nothing yet._

## [0.13.0] - 2026-09-11

### Added

- `KeyValueStore.transaction(store, apply)` — closure-based, all-or-nothing
  transactions. `apply` threads copy-on-write mutations with `?` and returns the
  final store; the transaction **commits** (atomically persisting a file-backed
  store) on `success`, or **rolls back** — leaving the store and its backing file
  untouched — on `failure`. See the Standard Library Reference (KeyValueStore ▸
  Transactions).

### Changed

- **Breaking — division semantics.** `/` now always produces a `number`
  (`7 / 2 == 3.5`); assigning its result to an `integer` is a type error. Use `//`
  for integer **floor division**, which rounds toward negative infinity
  (`-7 // 2 == -4`) and requires both operands to be `integer`.
- **Breaking — discarded results are errors.** A call that returns `result<T>`
  whose value is ignored is now a hard error instead of a silent discard.
  Intentionally drop a result with `_ = expr`.
- **Breaking — `Array.first`, `Array.last`, and `Array.get` return `optional<T>`.**
  They yield `none` when the array is empty or the index is out of bounds,
  standardising the "empty ⇒ `optional`" convention.
- **Breaking — `spawn` requires a `task_scope`.** Using `spawn` outside a
  `task_scope` is now a compile-time error (previously a warning), guaranteeing
  structured task lifetimes.
- **Breaking — stricter `unique` enforcement.** Violating a `unique` value's
  single-use contract is now a compile-time error for local variables (misuse
  through a parameter remains a warning).
- `Result.map` and `Result.flat_map` now infer `result<U>` from the mapping
  lambda's return type, so the plain `map`/`flat_map` handle payload-type changes
  without the typed `Result.map_boolean` / `_integer` / `_number` / `_string`
  variants (which remain for block-body lambdas whose return type cannot be
  inferred).
- The `?` operator is now permitted inside block-body lambdas (those inferred as
  returning `StdlibAny`).

### Removed

- **Breaking — `Math.log(base, value)`.** Removed in favour of
  `Math.log_base(value, base)` — note the **reversed argument order**
  (`Math.log(2, 8)` becomes `Math.log_base(8, 2)`). `Math.log_e`, `Math.log_10`,
  `Math.log_2`, and `Log.log` are unaffected.
- **Breaking — the `widget` built-in type.** A vestigial type with no operations;
  removed from the language, the standard-library catalog, the language server,
  and the editor grammars.

### Migration

- Replace integer uses of `/` that relied on truncation with `//` (e.g.
  `total / count` → `total // count` when both are integers and you want an
  integer quotient); otherwise expect a `number` result.
- Consume or bind every `result<T>`; add `_ = expr` where a discard is deliberate.
- Update `Array.first` / `Array.last` / `Array.get` call sites to handle
  `optional<T>` (e.g. `match`, `Optional.unwrap_or`, or `?`).
- Move bare `spawn` calls inside a `task_scope { … }` block.
- Replace `Math.log(base, value)` with `Math.log_base(value, base)`.
- Remove any `widget`-typed declarations (they were dictionary-backed; use
  `dictionary<…>` directly).

[Unreleased]: https://github.com/d3v0n5h1r3/luma/commits/main
[0.13.0]: https://github.com/d3v0n5h1r3/luma/releases/tag/v0.13.0
