# Luma — Error Handling

> How Luma detects, represents, propagates, and recovers from errors — and the conventions that library authors must follow.

> **Relationship to the User Manual.** This document is the authoritative reference for error-handling *conventions and policy*. For the language syntax of `result<T>` and `optional<T>`, see [User Manual — §14 Result and Optional](Luma_User_Manual.md#14--result-and-optional); for the catalogue of runtime errors, see [User Manual — §29 Error Reference](Luma_User_Manual.md#29--error-reference).

---

## Table of Contents

1. [Error Categories](#1--error-categories)
2. [Domain Failures — `result<T>`](#2--domain-failures--resultt)
3. [Absent Values — `optional<T>`](#3--absent-values--optionalt)
4. [Programmer Errors — Runtime Errors](#4--programmer-errors--runtime-errors)
5. [Try / Catch / Finally](#5--try--catch--finally)
6. [Standard Library Conventions](#6--standard-library-conventions)
7. [Third-Party Library Conventions](#7--third-party-library-conventions)
8. [Anti-Patterns](#8--anti-patterns)
9. [Interpreter Implementation Policy](#9--interpreter-implementation-policy)

- [See Also](#see-also)

---

## 1 — Error Categories

Luma distinguishes two fundamentally different kinds of failure:

| Category              | Description                                                | Mechanism                                   | Examples                               |
| --------------------- | ---------------------------------------------------------- | ------------------------------------------- | -------------------------------------- |
| **Domain failures**   | Expected conditions the caller is responsible for handling | `result<T>` + `match` / `??` / `?` / `!>`   | File not found, invalid input, timeout |
| **Programmer errors** | Conditions that should never occur in a correct program    | Runtime error + `try` / `catch` / `finally` | Division by zero, out-of-bounds access |

Mixing these two categories makes code harder to reason about. The type system reinforces the separation: domain failures are encoded in return types and the type checker ensures they are handled; programmer errors bypass that machinery entirely and surface as runtime faults.

Syntax errors and type errors are detected before execution begins and cannot be caught by any `try` block — see [§29 — Error Reference](Luma_User_Manual.md#29--error-reference) in the User Manual for a complete catalogue.

---

## 2 — Domain Failures — `result<T>`

`result<T>` is the primary mechanism for domain failures. A value is either `success(value)` or `failure(message)`.

### Creating Results

```luma
function result<number> safe_divide(number a, number b) {
    if b == 0 {
        return failure("division by zero")
    }

    return success(a / b)
}
```

The inner type of `failure` defaults to `string`. Use a second type parameter when you need a structured error value:

```luma
choice MathError { DivisionByZero Overflow }

function result<number, MathError> safe_divide(number a, number b) {
    if b == 0 {
        return failure(MathError.DivisionByZero)
    }

    return success(a / b)
}
```

`failure(...)` records its source location (line and column) at the point it is written. When `Result.unwrap` is later called on such a value and throws, the error message includes a `[line:col]` prefix that identifies where the failure originated, even after the value has been passed through multiple call frames.

### Handling Results

Use `match` for exhaustive handling. Both arms are required — the type checker rejects non-exhaustive matches.

```luma
match safe_divide(10.0, 0.0) {
    success(value) { print("result: ${value}") }
    failure(msg)   { print("error: ${msg}") }
}
```

Use `??` when a sensible default exists and the failure does not need to be inspected:

```luma
number value = safe_divide(10.0, 0.0) ?? 0.0
```

Use `Result.unwrap_or` when you prefer the function form:

```luma
number value = Result.unwrap_or(safe_divide(10.0, 0.0), 0.0)
```

Avoid naked `Result.unwrap` unless you have already verified success with `Result.is_success`. A failed unwrap raises a **RuntimeError** — the wrong category for an expected domain failure.

### Propagating Results with `?`

Inside a function that returns `result<T>`, the postfix `?` operator unwraps `success(v)` to `v` or immediately returns the `failure` to the caller:

```luma
function result<number> parse_and_double(string s) {
    number n = Converter.to_number(s)?  # propagates failure if conversion fails

    return success(n * 2)
}
```

`?` can also be used inside `@main`. If it propagates a failure there, the program terminates with a RuntimeError whose message includes the failure value.

`?` is a type error if the enclosing function does not return `result<T>` or `optional<T>`.

### Chaining Results with `!>`

The `!>` error-pipe operator chains a sequence of fallible steps. The first `failure` in the chain short-circuits the rest and becomes the overall result:

```luma
result<integer> r = success(raw_input) !> parse() !> validate() !> transform()
```

If any step returns `failure`, subsequent steps are skipped and the failure propagates through. The overall expression always produces `result<T>`.

A plain (non-`result`) value on the left is treated as `success(value)`, so you can start a chain from any expression.

### Never Silently Discard a Result

The type checker emits a warning when a `result<T>` return value is discarded. Assign to `_` to explicitly acknowledge and suppress it:

```luma
_ = parse(input)  # explicitly discarded — warning suppressed
```

---

## 3 — Absent Values — `optional<T>`

`optional<T>` represents a value that may or may not be present: either `some(value)` or `none`. It is the safe replacement for nullable or sentinel values.

Any `T` value is implicitly assignable to `optional<T>`, so a function that returns `optional<T>` can `return value` directly without wrapping in `some()`.

### Handling Optionals

Use `match` for exhaustive handling:

```luma
match Dictionary.get(config, "host") {
    case some(host) { connect(host) }
    case none       { print("no host configured") }
}
```

Use `??` when a default is sufficient:

```luma
string host = Dictionary.get(config, "host") ?? "localhost"
```

Use the postfix `?` operator inside a function that returns `optional<T>` to propagate `none` to the caller:

```luma
function optional<string> display_name(optional<User> user) {
    User u     = user?                         # returns none if user is none
    string name = find_name(u.id)?             # returns none if name not found

    return name
}
```

### Optional Chaining

Use `?.` to safely access a field on an optional record and `?[` to safely index an optional array. The result is `optional<T>`. When the accessed member is itself optional, the chain auto-flattens:

```luma
record Address { string city }
record User    { string name  optional<Address> address }

optional<User> user = find_user(id)

string city = user?.address?.city ?? "unknown"
```

### Converting Between `result<T>` and `optional<T>`

```luma
result<integer>  r = Optional.to_result(x, "value was absent")
optional<integer> o = Result.to_optional(r)
```

---

## 4 — Programmer Errors — Runtime Errors

Runtime errors represent conditions that should never occur in a correct program. They are not encoded in return types and do not need to be handled on every call site — they indicate a bug.

Common sources:

| Source                           | Example                                         |
| -------------------------------- | ----------------------------------------------- |
| Division by zero                 | `integer x = 0; integer r = 10 / x`             |
| Out-of-bounds array access       | `names[99]` when the array has fewer elements   |
| `assert` failure                 | `assert value >= 0, "must be non-negative"`     |
| `Result.unwrap` on `failure`     | Calling `Result.unwrap` without checking first  |
| `trusted_downcast` type mismatch | `trusted_downcast<Dog>` on a `Cat` value        |
| Maximum recursion depth          | Call stack exceeds the maximum depth            |
| Integer overflow                 | `integer i = 9223372036854775807; i++`          |
| Channel closed                   | `Channel.send(ch, v)` after `Channel.close(ch)` |
| Channel full (non-blocking)      | `Channel.try_send(ch, v)` when buffer is full   |
| Channel empty (non-blocking)     | `Channel.try_receive(ch)` when buffer is empty  |

The channel module uses three typed exception classes for error signalling:

| Exception            | Description                                           |
| -------------------- | ----------------------------------------------------- |
| `ChannelClosedError` | Operating on a channel that has been closed           |
| `ChannelFullError`   | Non-blocking send when the channel buffer is full     |
| `ChannelEmptyError`  | Non-blocking receive when the channel buffer is empty |

Use `try`/`catch` to recover from these where recovery is meaningful — see [§5](#5--try--catch--finally).

Note: `downcast<T>` is an exception to this rule — it is a safe operation that returns `result<T>` rather than throwing. `trusted_downcast<T>` is the unsafe variant that throws a RuntimeError on mismatch.

---

## 5 — Try / Catch / Finally

`try`/`catch`/`finally` handles **runtime errors only**. It cannot intercept syntax errors or type errors.

### Basic Usage

```luma
try {
    integer result = 10 / 0
} catch(err) {
    print("caught: ${err}")  # caught: division by zero
} finally {
    print("always runs")     # runs whether or not an error occurred
}
```

All three clauses are optional, but at least one of `catch` or `finally` must be present.

### `finally` Without `catch`

`finally` guarantees that cleanup runs regardless of outcome. When there is no `catch` block, the error is still propagated to the nearest enclosing `try`/`catch` after `finally` finishes:

```luma
try {
    try {
        integer _ = 1 / 0
    } finally {
        print("inner cleanup")   # runs before the error propagates
    }
} catch(err) {
    print("caught: ${err}")      # caught: division by zero
}
```

### What `catch` Handles

`catch` intercepts runtime errors: division by zero, out-of-bounds access, `assert` failures, failed `Result.unwrap`, and unrecoverable stdlib OS errors. It does **not** intercept syntax or type errors — those abort the interpreter before execution begins.

The `err` binding in `catch(err)` is an immutable `string` containing the error message.

### Design Rule

> Use `result<T>` for **recoverable domain failures** — conditions the caller is expected to handle, such as a missing file, a network timeout, or invalid user input. Use `try`/`catch` for **programmer errors** — conditions that should never occur in a correct program, such as an out-of-bounds access or a failed assertion.

Avoid using `try`/`catch` to swallow `Result.unwrap` failures. If a result may legitimately be `failure`, use `match`, `??`, or `Result.unwrap_or` instead.

---

## 6 — Standard Library Conventions

The standard library follows four consistent rules.

### Rule 1 — Infallible Operations Return Plain Values

Functions that cannot fail return a plain type directly. No wrapping is needed at the call site:

```luma
string  repr = Converter.to_string(42)           # always succeeds
string  ext  = FileSystem.extension("photo.png") # always a string
boolean abs  = FileSystem.is_absolute("/etc")    # always a boolean
```

### Rule 2 — Fallible Operations Return `result<T>`

Any function that can legitimately fail due to external conditions returns `result<T>`. The type system forces the caller to handle both outcomes:

```luma
result<string>  content = FileSystem.read_file("data.txt")   # file may not exist
result<integer> n       = Converter.to_integer("abc")        # parsing may fail
result<Http.Response> _ = Http.post(url, body)               # network may fail
result<string>  hash    = Hash.sha256_file("data.bin")       # file may not exist
```

This covers all I/O operations (`FileSystem`, `Http`, `Socket`), parsing (`Converter`, `Json`, `Csv`, `Xml`), and any function that touches the OS or external resources.

### Rule 3 — Higher-Order Callbacks Wrap Runtime Errors as `failure`

Functions that accept callbacks — `Array.map`, `Array.filter`, `Array.sort`, `Array.reduce`, `Array.flat_map`, and others — automatically convert any runtime exception thrown inside the callback into a `failure`. The program does not crash; the enclosing function returns `failure("message")` instead:

```luma
# A divide-by-zero inside the callback becomes failure("division by zero")
result<array<number>> r = Array.map(values, (number v) -> 10.0 / v)

match r {
    success(mapped) { print(Converter.to_string(mapped)) }
    failure(msg)    { print("callback error: ${msg}") }
}
```

This means that even callback-heavy code does not crash on unexpected runtime faults — the failure surfaces as a `result<T>` that the caller can inspect.

### Rule 4 — Truly Unrecoverable Errors Are Runtime Errors

A small number of stdlib conditions are unrecoverable OS faults (for example, a sudden mid-write filesystem corruption). These raise a runtime error catchable with `try`/`catch`. They are distinct from expected failures (file not found) and are intentionally not returned as `result<T>`.

---

## 7 — Third-Party Library Conventions

Libraries distributed as `.luma` source or as namespaces must follow the same conventions as the standard library. Violating these conventions surprises callers and breaks the composability that `result<T>` is designed to provide.

### Mandatory Rules

1. **Return `result<T>` for every operation that can legitimately fail.** Do not raise runtime errors for conditions the caller is expected to handle. Do not return sentinel values (`-1`, `""`, `none`) to signal failure.

2. **Never return `result<T>` for operations that cannot fail.** Wrapping infallible operations in `result<T>` forces unnecessary error handling on callers and erodes trust in the type signature.

3. **Use `string` error messages for simple cases.** Keep messages short, lowercase, and describing the condition — not the action. For example: `"key not found"`, `"value out of range"`, `"connection refused"`.

4. **Use a typed error choice for APIs with multiple distinct failure modes.** When callers need to branch on the kind of failure, a `choice` type as the second type parameter of `result<T, E>` is clearer than parsing strings:

    ```luma
    choice DatabaseError { ConnectionFailed QueryFailed(string reason) Timeout }

    function result<array<Row>, DatabaseError> query(string sql) { ... }
    ```

5. **Do not catch and swallow runtime errors internally.** A library function that silently ignores a division by zero or an out-of-bounds access hides bugs from the caller. Let runtime errors propagate.

6. **Wrap callback runtime errors as `failure` when accepting user-provided callbacks.** If your function calls a user-provided lambda, wrap its execution in a `try` and return `failure(err)` on error. This matches the behaviour of the standard library's higher-order functions and prevents the caller's code from crashing your function.

    ```luma
    function result<array<T>> transform_each(array<T> items, (T) -> T fn) {
        mutable array<T> out = []

        for item in items {
            try {
                out = Array.push(out, fn(item))
            } catch(err) {
                return failure(err)
            }
        }

        return success(out)
    }
    ```

7. **Avoid `Result.unwrap` in library code.** If the success case cannot be guaranteed, use `match`, `??`, or `?` to propagate failures cleanly to the caller.

### Recommended Practices

- Name error values as conditions, not actions: `"timeout"` not `"timed out while connecting"`.
- Keep `failure` messages stable across versions — callers may compare them programmatically.
- Document in comments which functions return `result<T>` and what the failure cases are.
- Prefer the `?` propagation operator over manual `match` + `return failure(...)` in internal helpers — it reduces boilerplate and preserves the original failure message.

---

## 8 — Anti-Patterns

### Using `try`/`catch` to Handle Expected Failures

```luma
# Bad — catching an expected condition with try/catch
try {
    string content = Result.unwrap(FileSystem.read_file(path))
    process(content)
} catch(err) {
    print("file not found")
}
```

`FileSystem.read_file` returns `result<string>`. The file not being found is an expected condition — use `match` or `??` instead:

```luma
# Good
match FileSystem.read_file(path) {
    success(content) { process(content) }
    failure(msg)     { print("could not read file: ${msg}") }
}
```

### Naked `Result.unwrap`

```luma
# Bad — crashes on failure with a RuntimeError
string content = Result.unwrap(FileSystem.read_file(path))
```

```luma
# Good — handle both cases
string content = FileSystem.read_file(path) ?? ""

# Or — propagate to the caller
string content = FileSystem.read_file(path)?
```

### Returning Sentinel Values Instead of `result<T>`

```luma
# Bad — caller cannot distinguish "not found" from a legitimate empty string
function string find_user(integer id) {
    if id < 0 {
        return ""   # sentinel for "not found"
    }
    ...
}
```

```luma
# Good — the type communicates the possibility of absence
function result<string> find_user(integer id) {
    if id < 0 {
        return failure("user not found")
    }
    ...
}
```

### Silently Discarding Results

```luma
# Bad — failure is silently ignored; the type checker will warn
FileSystem.write_file("output.txt", data)
```

```luma
# Good — failure is explicitly handled
match FileSystem.write_file("output.txt", data) {
    success(_) { }
    failure(msg) { print("write failed: ${msg}") }
}

# Also acceptable — explicitly suppress when the outcome is genuinely unimportant
_ = FileSystem.write_file("output.txt", data)
```

### Wrapping Infallible Functions in `result<T>`

```luma
# Bad — forces callers to handle a failure that can never occur
function result<string> to_string(integer n) {
    return success(Converter.to_string(n))
}
```

```luma
# Good — if the operation cannot fail, return the plain value
function string to_string(integer n) {
    return Converter.to_string(n)
}
```

---

## 9 — Interpreter Implementation Policy

The C++ interpreter itself uses three error handling tiers, matching the pipeline phases.

### Analysis Phase (Lexer, Parser, TypeChecker, Linter)

- **Strategy:** Emit `Diagnostic` objects to a collector vector
- **Do NOT throw** exceptions for user-facing syntax/type errors
- **Do throw** for internal bugs (assertion-like failures)

### Runtime Phase (VM, Stdlib)

- **Invariant violations (bugs):** Throw `RuntimeError`
- **Expected failures (user logic):** Return `result<T>` via `make_success_value()` / `make_failure_value()`
- **Index/bounds errors:** Throw `RuntimeError` (these are programming errors in Luma code)

### Protocol Layer (LSP, DAP)

- **Protocol errors:** Return typed error responses (HandlerResult::error, LSP error codes)
- **I/O/transport errors:** Throw `std::runtime_error`
- **Malformed messages:** Log to stderr and skip (graceful degradation)
- **Internal bugs:** Throw (will be caught by top-level handler)

### Shared Modules (`shared/`)

The `shared/` directory contains C++ code consumed by both the language server and the debugger. Each module uses the error model that best fits its domain:

| Module                     | Error Model                  | Rationale                                                                                                                                              |
| -------------------------- | ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `json/` (JSON parser)      | `std::runtime_error`         | Malformed JSON is a bug in the caller (protocol violation). Exceptions propagate to the caller's handler.                                              |
| `protocol/` (transport)    | `std::optional` + exceptions | `std::optional` for recoverable I/O (e.g. end-of-stream). Exceptions for protocol violations and framing errors that indicate a broken message stream. |
| `stdlib/` (stdlib catalog) | Plain values                 | Purely descriptive metadata with no failure modes.                                                                                                     |
| `symbols/` (symbol info)   | `std::optional` fields       | Optional fields model genuinely absent data (e.g. a symbol with no return type).                                                                       |

**Rules for shared modules:**

1. **Do not use `Result<T, E>` in shared code.** `Result<T, E>` is a Luma runtime concept. Shared C++ code should use `std::optional` for expected absence and exceptions for invariant violations.
2. **Throw `std::runtime_error` for malformed input that the caller should have validated.** JSON parse errors and protocol framing errors fall into this category.
3. **Return `std::optional` when absence is a normal outcome**, such as reaching end-of-stream or looking up a key that may not exist.
4. **Do not silently swallow errors.** If a function encounters an unexpected state, throw rather than returning a default. Silent failures hide bugs.

### Editor Extensions (`extensions/`)

Each extension follows the error conventions of its host language and ecosystem:

| Extension | Language   | Error Model                                                                                                                                                  |
| --------- | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| VS Code   | TypeScript | Exceptions (`throw new Error`) + `Promise` rejection. Show user-facing errors via `vscode.window.showErrorMessage`. Use `try`/`catch` at command boundaries. |
| Zed       | Rust       | `zed::Result<T>` and the `?` operator for fallible operations. Follow Rust idioms.                                                                           |

**Rules for extensions:**

1. **Follow the host ecosystem's conventions.** Do not impose C++ error patterns on TypeScript or Rust code.
2. **Show user-facing errors through the editor's notification API**, not through stderr or silent failures.
3. **Catch errors at command and activation boundaries** to prevent a single failure from crashing the entire extension.
4. **Log diagnostic details to an output channel or log file** for debugging, separate from user-visible messages.

---

## See Also

- [Tutorial — §18 Handling Absence and Failure](Luma_Tutorial.md#18--handling-absence-and-failure) — a beginner's first look at `result` and `optional`
- [User Manual — §14 Result and Optional](Luma_User_Manual.md#14--result-and-optional) — the `result` and `optional` types and their syntax
- [User Manual — §5 Control Flow (Try / Catch / Finally)](Luma_User_Manual.md#5--control-flow) — `try` / `catch` / `finally` semantics
- [User Manual — §29 Error Reference](Luma_User_Manual.md#29--error-reference) — the complete catalogue of runtime errors
- [Coding Guidelines — §9 Error Handling](Luma_Coding_Guidelines.md#9--error-handling) — idiomatic error-handling patterns
- [Standard Library Reference](Luma_Standard_Library_Reference.md) — the `result` / `optional`-returning functions across the library
- [Software Architecture](Luma_Software_Architecture.md) — how the interpreter categorises and reports errors
- [REPL Guide](Luma_REPL_Guide.md) — experiment with error handling interactively
- [Contributing — Error Handling](../CONTRIBUTING.md#error-handling) — error-handling policy for interpreter contributions
