# Changelog

All notable changes to Luma are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Luma is in its **alpha** series (the current version is recorded in [`VERSION`](VERSION)) and will adopt [Semantic Versioning](https://semver.org/spec/v2.0.0.html) at `1.0`. Until then, minor releases may include breaking language and standard-library changes; each is called out with **Breaking** below, and the migration steps are collected at the end of every release that has them.

## [Unreleased]

### Added

- **Language — octal integer literals** with a `0o` / `0O` prefix (`0o755`).
- **Language — Unicode identifiers.** A name may contain any well-formed non-ASCII UTF-8 character, so `café` and `π` are valid identifiers. Invisible or non-breaking Unicode spaces and control characters are rejected with `unexpected character U+XXXX`, and malformed UTF-8 with `invalid UTF-8 byte sequence`.
- **Linter — W0017 `boolean-literal-comparison`** reports `x == true` and `x != false`. W0008 now reports only a condition that is always true or always false.
- **Language — `include` works in the REPL and with `--eval`.** Paths resolve against the current working directory, then `LUMA_PATH`; a failed include under `--eval` exits with code 4.

### Changed

- **Breaking — integer overflow raises an error.** `+`, `-`, `*`, unary `-`, `++`, and `--` on `integer` values whose result does not fit in 64 bits now raise `integer overflow in addition` (or `subtraction`, `multiplication`, `negation`, `increment`, `decrement`) instead of silently producing a `number`, so an `integer` variable can no longer hold a `number` at run time. `/` on finite operands that overflows now raises `number overflow in division`, and `/` or `%` on non-numeric operands reports `cannot divide 'T' and 'U'` (`cannot take the remainder of 'T' and 'U'`).
- **Breaking — stricter type checking.** Programs that relied on the type-checker soundness holes listed under Fixed no longer type-check. In particular, `result<T>` now means `result<T, string>`, so `failure(...)` in a `result<T>` must carry a `string`, and a block-bodied lambda with no `return <value>` has type `function(...) -> void`.
- **Breaking — Luma flags are honoured after the file.** `luma app.luma --test` now runs the tests instead of passing `--test` to the program. Other arguments after the file are still forwarded to the program, and everything after a literal `--` is forwarded unchanged. A missing or invalid optimization level (`-O3`, `-O` with no level), `--test` or `--check` without a file, program arguments without a file, a file without the `.luma` extension, and a nonexistent file are now usage errors (exit code 5) instead of warnings or runtime errors. After the file, only `-O0`, `-O1`, `-O2`, or `-O`/`--optimize` followed by `0`, `1`, or `2` are Luma's flag; any other `-O…` argument (such as `-Output`) is passed to the program.
- **Breaking — triple-quoted strings process escape sequences** (`\n`, `\t`, `\"`, `\\`, `\$`) like ordinary strings, resolved after dedenting. A literal backslash must now be escaped: `"""C:\Users"""` is a syntax error; write `"""C:\\Users"""`.
- **Breaking — the lexer is stricter about numbers.** `1.` is no longer a number literal (write `1.0`), and a numeric literal directly followed by a letter or an invalid digit (`123abc`, `0b102`) is a syntax error.
- **Breaking — `Process` refuses Windows batch scripts.** Every Windows launch path refuses `.bat` and `.cmd` files — also when the name only becomes one after Windows normalises it (`x.bat\.`, `x.bat/.`, trailing dots or spaces) — and arguments containing a NUL character; `Process.run_command_typed` returns `InvalidCommand`. Run a batch script through `cmd.exe /c` explicitly.
- **Breaking — `Terminal` is sandbox-aware.** Under `--box`, the input functions `Terminal.enable_raw_mode`, `enable_mouse`, `read_key`, `read_key_timeout`, `get_input`, `read_line`, and `get_cursor_position` are withheld. Output functions, state queries, and the `disable_*` and `test_*` functions remain available.
- **`LUMA_LIMIT_*` overrides are validated.** Each override must lie between 1 and the limit's ceiling: `0` or a non-numeric value is ignored with a warning, and a value above the ceiling is clamped with a warning. See the [resource-limit table](documents/Luma_Performance_Guide.md#6--resource-limits).
- **REPL — command lines are parsed exactly.** A line starting with `:` is never passed to the Luma parser: an unknown command prints `Unknown command: …`, a missing or unexpected argument prints the command's usage, and a repeated `:file` re-reads the file from disk.
- **`Statistics.mean`, `median`, `mode`, `variance`, and `standard_deviation` fail when the result is not finite** instead of returning `success` with an infinity or `NaN`. `mean` returns the exact `sum / n` whenever the sum is finite and switches to an overflow-safe scaled form only when it overflows, and `variance` uses a two-pass sum of squared deviations, so `Statistics.mean([1e308, 1e308])` is `success(1e308)` and `Statistics.mean([0, 1, 6, 1])` is exactly `2.0`.
- **Http — truncated, oversized, and malformed responses are failures** (`Http.Error.Malformed`) instead of partial successes. Chunked transfer encoding takes precedence over `Content-Length`, and bytes past `Content-Length` are dropped. Responses to `HEAD` requests and `1xx`, `204`, and `304` responses have no body whatever their headers say, and interim `1xx` responses (such as `100 Continue`) are skipped. Timeouts are clamped to 1–2,147,483,647 ms.
- The bytecode cache format moves to version 9; existing `.lumc` files are rebuilt automatically.
- The VS Code extension now requires **VS Code 1.140 or newer** (previously 1.137), matching its `@types/vscode` API typings. Packaging the extension with `@vscode/vsce` 4 requires Node.js 22 or newer.
- **Build — no C++ headers are installed.** `cmake --install` and the CPack packages ship only the executables, the `find_package(Luma)` config, examples, and documentation. The previously installed headers were not self-contained and no library was exported to link them against.
- **Build — DEB package dependencies are derived from the built binaries** via `dpkg-shlibdeps` when available, replacing the stale `libc6 (>= 2.31)` floor.
- **Build — configuring inside the source directory is now rejected**, and editing `VERSION` triggers a reconfigure.
- **Tooling — C++ formatting now requires clang-format 20 or newer** (previously 18). `.clang-format` uses the clang-format 20 option spellings, which older versions reject; the CI gate, Dev Container, Git and agent hooks, `lint.py` / `format.py`, and the `format` target all require 20, and skip or fail with a clear message on older versions. clang-tidy stays pinned to 18, and the CI clang-tidy job no longer floats with the `ubuntu-latest` image.
- **Benchmarks — short cases are no longer dominated by timer resolution.** `DateTime.milliseconds_since_start()` ticks in whole milliseconds, so many cases finished in 0–2 ms and reported noise. `time_it` now treats the requested iteration count as a minimum and scales up any run shorter than 30 ms; the `iterations` column reports the count actually timed. A new "luma hello world (bytecode cache hit)" startup case separates compile cost from cache loading, and "Math statistical (10 elem)" is renamed "Statistics mean/median/stddev (10 elem)". Refresh the CI benchmark baseline once after upgrading.

### Removed

- **Internal — the unused name resolver.** `core/analysis/resolver/`, its unit test, and the `fuzz_resolver` fuzz target and corpus are removed. Nothing called the resolver; names are resolved by the type checker. No user-visible change.
- **Internal — `RuntimeConstraints` and `sandboxed()`** are removed from `resource_limits.hpp`; they were unused.

### Fixed

- **Compiler — an `if`/`else` that returns from only one branch no longer truncates the function.** In a function with no locals, falling off the end after such an `if` raised `bytecode truncated: not enough bytes for operand`.
- **Compiler — `return` and `?` inside `try`/`finally` keep locals in the `finally` body intact.** The value being returned occupied the slot of the first local declared in the `finally` body, so in `try { return 42 } finally { string m = "done"; print(m) }` the `print` showed `42` instead of `done`.
- **Compiler — `break` and `continue` inside `try`/`finally` run the `finally` body before leaving the loop's scope**, so the body sees the loop's locals instead of unrelated stack values.
- **Compiler — the `-O2` inliner only inlines straight-line functions.** It copied an early `return` into the caller, so the caller returned the callee's value and skipped the rest of its own body.
- **Compiler — `?` in `@main` raises a catchable RuntimeError.** It silently exited with code 0, skipping both an enclosing `catch` and the rest of `main`. The error carries the failure text (or `unwrap failed: value is none`); uncaught, it exits with code 1.
- **Compiler — `"${""}"` compiles.** An interpolation of an empty string literal pushed no value onto the stack.
- **Compiler — `-O1` and `-O2` no longer share in-memory compilation-cache entries.** The cache key recorded only whether optimization was on, so switching levels in one process reused the other level's bytecode.
- **Compiler — the bytecode verifier rejects a jump to the end of the code and execution falling off the end**, and a full constant pool or name table is a compile error instead of an uncaught exception.
- **Type checker — soundness holes closed.** The checker now rejects: unknown type names in parameters, return types, record, interface, and choice fields, type aliases, lambda parameters, and tuple bindings (a misspelt type silently became "unknown" and accepted anything); a function signature referencing a record declared later in the file; a value returned from a `void` function and a non-`void` function with an empty body; unknown, duplicate, or missing named arguments, and calls to zero-parameter functions with arguments; `??` on a value that is neither `optional` nor `result`; plain `.field` and `[i]` on an `optional<T>`; destructuring a non-tuple, in a declaration or a `for` loop; `x += 0.5` on an `integer` variable; `?` propagating an error type that does not fit the enclosing function's; a `result` or `optional` piped into a parameter that is not one (`nums |> Array.sort(cmp) |> Array.reverse()` failed at run time); and `assert` with no arguments or more than two (a piped value is now its condition). A `unique` value consumed in an `if` without `else` now stays consumed — unless that branch returns, `break`s, or `continue`s, so guard clauses keep working — and shadowed `unique` variables are tracked separately. A call through a parameter or local that shadows a top-level function is checked against the value's own type, and types declared in a nested namespace resolve inside it.
- **Type checker — better generic inference and container types.** Inference of `T` now looks inside records, interfaces, choices, tuples, function types, built-in containers, and the error type of `result<T, E>`, and uses a value piped into a generic call; named arguments on generic calls are type-checked. `queue<T>`, `stack<T>`, `set<T>`, and `key_value_store<T>` keep their element type, so `Set.to_array`, `Queue.to_array`, and `Stack.to_array` return `array<T>`. Match expressions check their arm patterns like match statements, and a `spawn` inside a `task_scope` is checked once.
- **Concurrency — nested `task_scope` blocks and channel waits inside tasks no longer deadlock the thread pool.** When every worker is blocked waiting and work is queued, the pool starts an extra worker (up to 256).
- **Concurrency — `Task.cancel(t)` cancels only `t`.** It cancelled `t`'s siblings and the whole `task_scope`. `Task.is_cancelled` still reports a cancelled enclosing scope.
- **Concurrency — cancellation is checked at loop iterations, function calls, tail calls, and `await`,** and blocking `Channel.send`, `receive`, `send_all`, `consume`, and `select` give up when their task is cancelled, so a cancelled task stops promptly instead of running to completion.
- **Concurrency — `task_scope` is fail-fast in completion order.** The first child to fail cancels its siblings immediately, and that error propagates once they stop. Awaiting a cancelled task raises `task cancelled`, and a child cancelled on its own contributes `none` instead of failing the scope. A task's own cancellation is not caught by its `catch` blocks, but its `finally` blocks run to completion, so a cancelled task inside `try`/`finally` no longer fails its scope.
- **Concurrency — a spawned task sees a read-only snapshot of the globals.** Tasks shared the parent's globals unsynchronised; writing a global from a task now raises `cannot assign to global variable 'x' from a spawned task`. Type-checked programs are unaffected, because functions cannot reach top-level variables.
- **Concurrency — scopes opened by a failing callback are closed.** An error escaping a callback (for example an `Array.map` lambda that opened a `task_scope`) left the scope open, and later spawns attached to it. Destroying a VM no longer overwrites another task's current scope.
- **Interpreter — stale stack values and `try` handlers are released** on return, exception unwinding, tail calls, and string interpolation, so they no longer keep objects alive or catch errors after their frame has returned.
- **Interpreter — functions, sockets, XML nodes, key-value stores, channels, and tasks compare by identity** (a copy of a function, such as one stored in a `mutable` variable, still equals the original), and `integer`/`number` equality is exact beyond 2^53 (a `number` equals an `integer` only if it is the same whole value).
- **Interpreter — tail calls check the required argument count**, and the call-depth limit is enforced as the smaller of `LUMA_LIMIT_MAX_CALL_DEPTH` and the VM's 256-frame limit, with the message `call stack overflow: maximum recursion depth (N) exceeded`.
- **Interpreter — spawning tasks reuses stack memory.** Each thread keeps spare 65,536-slot stacks, so spawning thousands of short tasks no longer allocates a fresh stack per task.
- **Interpreter — `assert` without a message fails with `assertion failed`** (lower case), as documented.
- **Lexer — triple-quoted strings dedent the whole literal.** The common indentation was computed for the text before the first interpolation only, and whitespace between interpolations was dropped (`"""${a} ${b}"""` gave `xy`).
- **Lexer and parser — nested tuple access `t.0.1` works**; it raised `invalid tuple index '0.1'` at run time.
- **Parser — a `(` or `[` at the start of a line begins a new statement.** It was parsed as a call or index on the previous line's expression. Binary operators and `|>` still continue an expression across lines.
- **Parser — error recovery stays local** to the function body, namespace, match arm, or choice variant, so one mistake no longer produces a cascade of follow-on errors. Lexer and parser carets point at the start of the offending token, and control characters in diagnostics are shown as `\xNN`.
- **Diagnostics — type-checker, linter, and runtime carets point at the offending token.** They sat one column past it (for example, after the `x` in `x += 0.5`), the reported column was off by the token's width, and editor squiggles began after the token and ran over the next word. The caret now starts on the token and underlines all of it, in the terminal, in the editor, and for runtime errors raised from cached bytecode.
- **Linter — a variable's initializer is linted before the new name is declared**, so in `integer x = x + 1` inside an inner scope, the `x` on the right counts as a use of the outer variable, not of the new one.
- **CLI — `--verify` bypasses both compilation caches**, as `--strict` already did, so it always verifies freshly compiled bytecode.
- **CLI — `--eval` reports type errors with exit code 2 and compile errors with exit code 4** instead of a runtime error.
- **REPL — an input that fails no longer leaves its includes behind.** A file included by an input that was then rejected (type, compile, or include error) stayed registered, so every later `include` or `:file` of it was silently skipped and edits to it were never picked up. A `:file` reload now also re-reads the files it includes, and runtime errors in typed input show the source line and caret (`<repl:N>`).
- **REPL — line editing is UTF-8 aware.** Backspace, Delete, Ctrl+D, and the arrow keys move by whole characters, the cursor position counts characters, and raw mode writes `\r\n` line endings. On Windows the console uses virtual-terminal input, and a bare Esc no longer swallows following keys.
- **Common — the UTF-8 decoder follows RFC 3629.** Overlong forms, surrogates, bytes `F5`–`FF`, and truncated sequences decode as U+FFFD, and string iteration always advances by the decoded length.
- **Unicode paths work on Windows.** File paths were converted with the ANSI code page, so non-ASCII names were garbled or failed. Paths are now UTF-8 end to end on every platform:
    - The interpreter reads the Unicode command line, so `luma café.luma` runs and program arguments arrive intact.
    - Include paths and `LUMA_PATH` entries are decoded as UTF-8, so `include "café.luma"` works. A path the system cannot represent is an error instead of a crash.
    - Every standard-library file-path argument (`FileSystem`, `Hash.*_file`, `Compression.*_file`, `Log.set_output`, `KeyValueStore`, the `Csv` and `Xml` file functions, and `Http.download`) is read as UTF-8, and paths returned to Luma (`FileSystem.list_files`, `absolute_path`, `Process.current_directory`, …) are UTF-8. On Windows, a path that is not valid UTF-8 is rejected with `invalid path '…': path is not valid UTF-8`.
    - `Process` launches programs and reads and writes environment variables through the wide-character Windows APIs, so non-ASCII program names, arguments, `PATH` entries, and environment values are passed intact.
- **Standard library — `String.pad_left` and `String.pad_right` with a negative width** fail with `width must not be negative`, like `String.center`.
- **Standard library — `String.dedent`** treats tabs as indentation, compares the common prefix exactly, ignores blank lines, and keeps line endings including a trailing newline.
- **Standard library — `String.levenshtein_distance`** decodes each string once and keeps distinct invalid bytes distinct; `String.insert`, `delete`, `replace_range`, and `substring` clamp offsets on truncated UTF-8.
- **Standard library — `Log.set_output` keeps the current file when the new one cannot be opened.**
- **Standard library — `KeyValueStore` saves atomically without a copy fallback** and removes its temporary file on every failure.
- **Standard library — `Process.run_command_timeout` honours its deadline** even after the child closes its output, and on POSIX kills the child's whole process group. Windows no longer leaks pipe handles when `Process.run` fails.
- **Standard library — `Socket` and `Http` connections try every resolved address**, not just the first.
- **Standard library — the terminal is restored at exit** (raw mode off, mouse reporting off) after `Terminal.enable_raw_mode`.
- **Standard library — the console line-length limit also applies to `Console.prompt_integer`, `prompt_number`, and `confirm`**, and an over-long line is discarded so the next read starts fresh.
- **Documentation — the User Manual, Standard Library Reference, and guides match the runtime** for flag handling, include resolution and messages, integer overflow, the truncating `%` operator (the result takes the sign of the dividend), awaiting a task more than once (it returns the same result), `FileSystem.kind` (`SymbolicLink`), `FileSystem.relative` (purely lexical), `Process.run` (no shell), and the default `Socket` timeouts.
- **Interpreter — `luma --verify` accepts valid programs.** The bytecode verifier simulated the stack in a straight line, so it counted a pop on each side of a branch, gave `MakeClosure`, `MakeRecord`, `RecordWith`, `SetField`, `And`/`Or`, `IndexGetOpt`, the pipe operators, and `TaskScopeEnd` no stack effect, and ignored the callee slot each frame starts with. Every program that declared a function failed with "stack underflow … after Pop". It now walks the control flow graph with an exact stack effect per instruction. Cached `.lumc` bytecode, which is re-verified on load, now also passes and runs without the redundant runtime operand checks.
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
- **`String.template` substitutes in a single pass.** A substituted value containing `{name}` was expanded again, so untrusted values could pull in other entries (`String.template("{a}", {"a": "{b}", "b": "SECRET"})` gave `SECRET`). Values are now inserted literally, and unknown placeholders are kept as written.
- **Process — Windows batch scripts are refused, and `Process.signal` validates its pid.** Commands are tokenized and never run through a shell, but on Windows a `.bat` or `.cmd` file is always run by `cmd.exe`, which re-parses the command line with its own quoting rules and could allow argument injection; see Changed. `Process.signal` rejects a pid that is not positive or does not fit in 32 bits, which could otherwise be truncated to a different process id.
- **Process and Socket — child processes inherit only their own pipe ends.** POSIX pipes and sockets are close-on-exec, and on Windows the child inherits only its stdout and stderr pipes and sockets are non-inheritable.
- **Socket — POSIX waits use `poll()` instead of `select()`**, which overflowed its descriptor set beyond 1,024 open descriptors.
- **Http — the chunked-body decoder bounds chunk sizes.** A chunk size that overflowed or exceeded the response limit could stall or exhaust memory; the decoder now checks for overflow, enforces the limit, and resumes where it stopped.
- **Regular expressions — the ReDoS guard can no longer be bypassed** with named groups, `\x`/`\u`/`\c` escapes, back-references, or case folding (`(a|A)+` under `CaseInsensitive`).
- **Size limits are enforced for every growing operation.** `RegularExpression.replace`, `replace_all`, `replace_with`, and `replace_all_with`, `String.indent`, `insert`, `replace_range`, and `replace`, `Json.to_string` (nesting depth, element count, and output size), and `LinearAlgebra.outer` raise an error before exceeding their limit instead of allocating without bound, and `Array.windows` returns `failure`. File reads (`FileSystem.read_*`, `Hash.*_file`, `KeyValueStore`) enforce the size limit while reading, not only from the reported file size.
- **`FileSystem.delete_directory` refuses the current working directory and its ancestors.**
- **Sandbox — `Terminal` input functions are withheld under `--box`**, so a sandboxed program cannot read keystrokes or put the terminal into raw mode; see Changed.
- **Include — containment checks fail closed.** If the including file's directory cannot be resolved, the include is rejected instead of skipping the check, and a file-anchored include never falls back to the working directory.

### Migration

- Code that relied on integer arithmetic overflowing into a `number` must widen an operand first (`number big = x` and then `big * y`), or keep the values in `number` variables.
- Programs that now fail to type-check: give `failure(...)` in a `result<T>` a `string` (or declare `result<T, E>`), remove values returned from `void` functions, fix misspelt type names and named arguments, replace `.field` and `[i]` on optionals with `?.`, `?[`, `??`, or `match`, and drop `??` defaults on values that are never missing.
- Pass program arguments that look like Luma flags after `--` (`luma app.luma -- --test`).
- Escape backslashes in triple-quoted strings (`\\`), write `1.0` rather than `1.`, and separate numbers from following names.
- On Windows, run batch scripts with `cmd.exe /c script.bat` instead of launching them directly.
- Write includes without a leading `./` (`include "file.luma"`); a `.` path component is rejected as directory traversal.

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
