# Changelog

All notable changes to Luma are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Luma is in its **alpha** series (the current version is recorded in [`VERSION`](VERSION)) and will adopt [Semantic Versioning](https://semver.org/spec/v2.0.0.html) at `1.0`. Until then, minor releases may include breaking language and standard-library changes; each is called out with **Breaking** below, and the migration steps are collected at the end of every release that has them.

## [Unreleased]

### Changed

- The VS Code extension now requires **VS Code 1.140 or newer** (previously 1.137), matching its `@types/vscode` API typings. Packaging the extension with `@vscode/vsce` 4 requires Node.js 22 or newer.
- **Build — no C++ headers are installed.** `cmake --install` and the CPack packages ship only the executables, the `find_package(Luma)` config, examples, and documentation. The previously installed headers were not self-contained and no library was exported to link them against.
- **Build — DEB package dependencies are derived from the built binaries** via `dpkg-shlibdeps` when available, replacing the stale `libc6 (>= 2.31)` floor.
- **Build — configuring inside the source directory is now rejected**, and editing `VERSION` triggers a reconfigure.
- **Tooling — C++ formatting now requires clang-format 20 or newer** (previously 18). `.clang-format` uses the clang-format 20 option spellings, which older versions reject; the CI gate, Dev Container, Git and agent hooks, `lint.py` / `format.py`, and the `format` target all require 20, and skip or fail with a clear message on older versions. clang-tidy stays pinned to 18, and the CI clang-tidy job no longer floats with the `ubuntu-latest` image.
- **Benchmarks — short cases are no longer dominated by timer resolution.** `DateTime.milliseconds_since_start()` ticks in whole milliseconds, so many cases finished in 0–2 ms and reported noise. `time_it` now treats the requested iteration count as a minimum and scales up any run shorter than 30 ms; the `iterations` column reports the count actually timed. A new "luma hello world (bytecode cache hit)" startup case separates compile cost from cache loading, and "Math statistical (10 elem)" is renamed "Statistics mean/median/stddev (10 elem)". Refresh the CI benchmark baseline once after upgrading.

### Fixed

- **Language — a `number` always holds a number at runtime.** An integer flowing into a `number` slot was converted only for plain variable declarations and assignments, so record fields, choice payloads, function arguments and return values, default values, and collection, tuple, optional, and result elements kept the integer and printed as `5` instead of `5.0` (and `type_of` reported `integer`). The type checker now marks every such flow, including `if`/`match` branches and `??` fallbacks, element-taking stdlib calls (`Array.push`, `Array.insert_at`, `Array.set`, `Array.concat`, `Dictionary.set`), and generic records and choices instantiated with `number`, and the compiler converts the value. Output that relied on the old behaviour now shows `.0`; format whole values with `String.format_number(value, 0)`. The `.lumc` format moves to version 5 for the new `WidenNumber` opcode; existing cache files are rebuilt automatically.
- **Language — array and dictionary literals mixing `integer` and `number` are accepted.** `[3, 4.5]` was rejected as a mixed-type array; it is now an `array<number>` (`[3.0, 4.5]`), and `{"a": 1, "b": 2.5}` a `dictionary<number>`. Likewise `optional<integer>` is now assignable to `optional<number>`, as the documented covariance of optionals intends.
- **Type checker — a `match` whose later arm is wider takes the wider type.** `match x { case 1 { 2 } else { 2.5 } }` was typed by its first arm as `integer`, so it could be assigned to an `integer` variable and hold `2.5`; it is now a `number`, as an `if` with the same branches already was. **Breaking:** such a match can no longer initialise an `integer`.
- **Type checker — `Array.map`, `Array.flat_map`, and `Array.scan` infer their result element type** from the callback (respectively its returned array, and the initial value) instead of the input element type, so `array<integer> lengths = Array.map(words, (string w) -> String.length(w)) ?? []` type-checks. **Breaking:** code that annotated such a result with the input element type, e.g. `result<array<integer>> r = Array.map(ints, (integer n) -> 10 / n)`, must use the callback's actual type (`result<array<number>>`).
- **Language — `_ = expr` discards a value.** It previously failed with `undefined variable '_'` (or `cannot assign to immutable variable '_'` inside a `for _` loop or a `case some(_)` arm), even though the "unused result" and "discarded value" diagnostics recommend it. The expression is still evaluated and type-checked; a declared `mutable` `_` keeps ordinary assignment semantics.
- **REPL — the banner shows the actual interpreter version** instead of a hard-coded "Luma 1.0".
- **Interpreter — editing an included file now takes effect on the next run.** Compiled bytecode was cached (in memory and in `<program>.lumc`) under a hash of the entry-point file alone, so after editing only an `include`d file `luma` kept running the old code until the entry point changed or the `.lumc` file was deleted. Both caches now record every included file with a content hash and recompile when any of them is edited or removed. The `.lumc` format moves to version 4; existing cache files are rejected and rebuilt automatically.
- **Standard library — `Task.race` and `Task.any` return as soon as a task finishes.** They polled their tasks with a sleep between rounds, and on Windows even the shortest sleep lasts a full scheduler tick, so each call took about 15.6 ms even when every task finished in microseconds. They now wake on task completion and cost about the same as `Task.all`.
- **Documentation — the Channel reference matches the runtime.** `Channel.receive` and `Channel.try_receive` return `result<T>` and `Channel.select` returns `result<(integer, T)>`; closed, empty, and timed-out channels are reported as `false` or `failure` values, not as catchable `ChannelClosedError`/`ChannelEmptyError`/`ChannelFullError` exceptions, as the Standard Library Reference and the Error Handling guide claimed.
- **Build — `cmake --build <dir> --target clean` no longer deletes the tracked `extensions/*/src/generated/platform.{ts,rs}` files** generated by `generate_platform_code`.
- **Build — the `format` target works on Windows**; it previously exceeded the 32,767-character command-line limit. The `tidy` target reports a clear error when `compile_commands.json` is missing.
- **Build — the `windows-arm64` preset links again.** `/CETCOMPAT` is an x64-only linker flag, and the ARM64 linker rejected it (`LNK1246`) for every executable; it is now applied to x64 builds only.
- **Build — the `tidy` target skips the other platform's `*_win32.cpp` / `*_posix.cpp` sources.** They are absent from `compile_commands.json`, so clang-tidy failed to parse them on every platform; CI and the pre-commit hook already excluded them.
- **Build — `dap_integration_test` depends on `luma_dap`**, so building only the test target no longer runs a missing or stale debug adapter.
- **Build — `package_source` now contains exactly the tracked sources.** It previously archived in-tree build directories, caches, and the repository clone Zed fetches into `extensions/zed/grammars/luma/`, and CPack's unescaped config turned the ignore patterns' `\.` into a wildcard.
- **Tooling — clang-tidy reports diagnostics in first-party headers on Windows.** The `HeaderFilterRegex` only matched `/` separators, but clang builds header paths with `\` on Windows, so every header finding was silently dropped.
- **Build — `find_package(Luma)` sets `LUMA_EXECUTABLE`** (previously always `NOTFOUND`) and derives `LUMA_EXAMPLES_DIR` from the actual install layout.
- **Build — the clang-cl fuzz build links the libFuzzer runtime for the target architecture** instead of a hard-coded x86_64 library.
- **Agent hooks — non-ASCII paths are read correctly on Windows.** The hook payload was decoded with the ANSI code page, so the C++ formatter silently skipped files such as `café.cpp`. The vendored-code guard no longer denies a read-only `view` of `external/`, and the formatter now touches only first-party files (inside the repository, outside `external/`), never a file that was merely viewed.
- **Prompt pipeline — the PowerShell fix runner's gates work again.** Build, test, and lint output leaked into the gate helpers' boolean results, so a failing `cmake`/`ctest`/`lint.py` run read as green, and the agent transcript leaked into each phase's outcome, so every fix phase was recorded as `error`. Tool and agent output now streams to the console instead, Pester regression tests cover the helpers in CI, and the PowerShell runners require PowerShell 7.2 or newer. Both fix runners also resolve a relative report directory against the caller's location and stop with a clear error when `git status` fails, instead of treating the tree as clean.
- **Prompt pipeline — the PowerShell model probe is bounded to 90 seconds**, matching the bash runner, so a slow or hung Copilot CLI can no longer stall `Invoke-LumaAudit.ps1` / `Invoke-LumaFix.ps1` before the first phase.
- **Scripts — `run_luma_examples.py` no longer crashes on Linux/macOS when an example times out after printing output.** The partial output arrives as bytes there even in text mode; it is now decoded and shown in the failure report.
- **Scripts — `bump_version.py` is all-or-nothing.** Every file is validated before any is written, so a missing version field no longer leaves a partial bump, and documentation references only match whole version numbers (bumping `0.15` no longer rewrites `10.15` or `0.150`).
- **Scripts — `generate_coverage.py` bounds its build to the CPU count** instead of an unlimited `make -j`, which could exhaust memory.
- **Benchmarks — several cases measured the wrong work.** The KeyValueStore read cases ran against an empty store because the copy-on-write result of every `set`/`remove` was discarded; the startup cases timed bytecode-cache hits instead of compilation and left `.lumc` files in the working directory; "Encoder base64 roundtrip" only encoded; and "Hash.verify" also timed computing the reference hash.

### Security

- **Windows executables now actually enable Control Flow Guard.** Code was compiled with `/guard:cf`, but the linker never received `/GUARD:CF`, so no CFG table was emitted. The vendored Mbed TLS and miniz libraries are now also compiled with the stack-protector and Control Flow Guard hardening used by first-party code.
- `_FORTIFY_SOURCE=2` now also applies to `RelWithDebInfo` and `MinSizeRel` builds.

## [0.15.0] - 2026-09-18

### Added

- **Reference-cycle leak diagnostics.** `reference<T>` is the only value type that can form a cycle that reference counting cannot reclaim. Set `LUMA_DIAGNOSE_REFERENCE_CYCLES=1` to make the interpreter print a one-line summary at exit when reference cells are still alive, which indicates a leaked cycle. Break a cycle by reassigning the cell before dropping it. See [Reference Cycles and Long-Running Programs](documents/Luma_Standard_Library_Reference.md#reference-cycles-and-long-running-programs).
- **Sandbox security model.** [Luma_Sandbox.md](documents/Luma_Sandbox.md) documents the trust boundary, capability model, guarantees, and limitations of sandbox mode (`--box`), and new conformance tests check that the registry and the stdlib catalog block the same modules.

### Changed

- **Fuzzing — campaigns run nightly** (previously weekly) with a persisted, seeded corpus, so coverage accumulates between runs.

## [0.14.0] - 2026-09-14

### Changed

- The VS Code extension now requires **VS Code 1.137 or newer** (previously 1.136), matching its `@types/vscode` API typings.
- **Tooling — the VS Code workspace uses CMake Tools as the C/C++ IntelliSense provider**, so IntelliSense works with the Visual Studio generator, which writes no `compile_commands.json`.

### Fixed

- **VS Code extension — snippets load in installed copies.** The snippets contribution pointed outside the packaged extension, so the `.vsix` shipped without them; the extension now bundles its own copy.

## [0.13.0] - 2026-09-11

### Added

- `KeyValueStore.transaction(store, apply)` — closure-based, all-or-nothing transactions. `apply` threads copy-on-write mutations with `?` and returns the final store; the transaction **commits** (atomically persisting a file-backed store) on `success`, or **rolls back** — leaving the store and its backing file untouched — on `failure`. See [KeyValueStore transactions](documents/Luma_Standard_Library_Reference.md#transactions) in the Standard Library Reference.

### Changed

- **Breaking — division semantics.** `/` now always produces a `number` (`7 / 2 == 3.5`); assigning its result to an `integer` is a type error. Use `//` for integer **floor division**, which rounds toward negative infinity (`-7 // 2 == -4`) and requires both operands to be `integer`.
- **Breaking — discarded results are errors.** A call that returns `result<T>` whose value is ignored is now a hard error instead of a silent discard. Intentionally drop a result with `_ = expr`.
- **Breaking — `Array.first`, `Array.last`, and `Array.get` return `optional<T>`.** They yield `none` when the array is empty or the index is out of bounds, standardising the "empty ⇒ `optional`" convention.
- **Breaking — `spawn` requires a `task_scope`.** Using `spawn` outside a `task_scope` is now a compile-time error (previously a warning), guaranteeing structured task lifetimes.
- **Breaking — stricter `unique` enforcement.** Violating a `unique` value's single-use contract is now a compile-time error for local variables (misuse through a parameter remains a warning).
- `Result.map` and `Result.flat_map` now infer `result<U>` from the mapping lambda's return type, so the plain `map`/`flat_map` handle payload-type changes without the typed `Result.map_boolean` / `_integer` / `_number` / `_string` variants (which remain for block-body lambdas whose return type cannot be inferred).
- The `?` operator is now permitted inside block-body lambdas (those inferred as returning `StdlibAny`).

### Removed

- **Breaking — `Math.log(base, value)`.** Removed in favour of `Math.log_base(value, base)` — note the **reversed argument order** (`Math.log(2, 8)` becomes `Math.log_base(8, 2)`). `Math.log_e`, `Math.log_10`, `Math.log_2`, and `Log.log` are unaffected.
- **Breaking — the `widget` built-in type.** A vestigial type with no operations; removed from the language, the standard-library catalog, the language server, and the editor grammars.

### Migration

- Replace integer uses of `/` that relied on truncation with `//` (e.g. `total / count` → `total // count` when both are integers and you want an integer quotient); otherwise expect a `number` result.
- Consume or bind every `result<T>`; add `_ = expr` where a discard is deliberate.
- Update `Array.first` / `Array.last` / `Array.get` call sites to handle `optional<T>` (e.g. `match`, `Optional.unwrap_or`, or `?`).
- Move bare `spawn` calls inside a `task_scope { … }` block.
- Replace `Math.log(base, value)` with `Math.log_base(value, base)`.
- Remove any `widget`-typed declarations (they were dictionary-backed; use `dictionary<…>` directly).

[Unreleased]: https://github.com/d3v0n5h1r3/luma/compare/v0.15.0...HEAD
[0.15.0]: https://github.com/d3v0n5h1r3/luma/releases/tag/v0.15.0
[0.14.0]: https://github.com/d3v0n5h1r3/luma/releases/tag/v0.14.0
[0.13.0]: https://github.com/d3v0n5h1r3/luma/releases/tag/v0.13.0
