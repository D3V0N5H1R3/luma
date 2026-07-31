---
description: "Use when writing, reviewing, or modifying Luma source code (.luma files). Covers Luma language syntax, types, conventions, standard library usage, and testing patterns."
applyTo: "**/*.luma"
---

# Working with Luma

Guidelines for writing, reviewing, and maintaining Luma source code (`.luma` files). Every function, variable, and file you produce must follow these principles. They are aligned with the [Luma_User_Manual.md](../documents/Luma_User_Manual.md) and the [Luma_Coding_Guidelines.md](../documents/Luma_Coding_Guidelines.md).

---

## Table of Contents

1. [Simplicity First](#1--simplicity-first)
2. [Syntax Fundamentals](#2--syntax-fundamentals)
3. [Types](#3--types)
4. [Variables and Mutability](#4--variables-and-mutability)
5. [Type Annotations](#5--type-annotations)
6. [Naming Conventions](#6--naming-conventions)
7. [Self-Documenting Code](#7--self-documenting-code)
8. [Functions](#8--functions)
9. [Control Flow](#9--control-flow)
10. [Error Handling](#10--error-handling)
11. [String Interpolation](#11--string-interpolation)
12. [Pipe Operator](#12--pipe-operator)
13. [Standard Library Modules](#13--standard-library-modules)
14. [Concurrency](#14--concurrency)
15. [Lambdas](#15--lambdas)
16. [Testing](#16--testing)
17. [Formatting](#17--formatting)
18. [File Organisation](#18--file-organisation)
19. [Anti-Patterns](#19--anti-patterns)
20. [Checklist](#20--checklist)

---

## 1 — Simplicity First

Write the simplest code that solves the problem correctly.

- Prefer straightforward control flow over clever tricks.
- If a standard library function does what you need, use it instead of reimplementing the logic.
- When two approaches are equally correct, choose the one that is easier to read.
- A beginner should be able to read your code and understand what it does within minutes.

**Test:** Before committing to an approach, ask — _is there a simpler way?_

---

## 2 — Syntax Fundamentals

- No semicolons. Luma does not require or use them.
- Comments start with `#`. See [§7 — Self-Documenting Code](#7--self-documenting-code) for comment guidelines.
- Entry point: exactly one `@main`-annotated function per runnable program.
- Braces are required on all control flow bodies (`if`, `else`, `for`, `match`, functions) — even single-statement bodies.

```luma
@main
function void main() {
    print("hello, world")
}
```

---

## 3 — Types

### Primitives

`boolean`, `integer`, `number`, `string`.

- `integer` and `number` are distinct. Integer division truncates; mixed arithmetic promotes integers.
- **`integer` is for indices and range bounds.** Use `integer` for array subscripts, string offsets, loop counters that index into collections, and range end-points (`0..n`). Use `number` for all other numeric values — counts, sizes, quantities, scores, IDs, measurements, and computations.

### Collections

- `array<T>` — ordered, zero-indexed, homogeneous.
- `dictionary<V>` — string-keyed, unordered, homogeneous values.

### Compound Types

- **Tuples:** `(T1, T2, ...)`. Access elements by index: `t.0`, `t.1`.
- **Records:** named product types with `PascalCase` names. Fields accessed by name. A record models one concept — every field should belong to that concept. If a record accumulates unrelated fields, split it into smaller records or use composition.
- **Choice types (ADTs):** tagged unions where each variant can optionally carry data. Variants without data (unit variants) serve the same role as enumerations. Support generics and recursive self-references (e.g., `choice List<T> { Nil Cons(T head, List<T> tail) }`). Match exhaustively with `case` arms. A choice type represents one decision or one dimension of variation — do not merge unrelated alternatives into a single choice.
- **Interfaces:** structural contracts. Records implement interfaces by providing the required fields.
- **Namespaces:** group related functions, records, choice types, and type aliases together. A namespace covers one domain concept — if it grows to cover unrelated concerns, split it into separate namespaces.
- **Type aliases:** `type Name = existing_type`. Use to give domain meaning to primitives.

### Special Types

- `result<T>` — either `success(value)` or `failure(message)`. Primary error-handling mechanism.
- `optional<T>` — either `some(value)` or `none`. Use to represent values that may be absent.
    - Match exhaustively with `some(x)` and `none` arms, or unwrap with `??`.

---

## 4 — Variables and Mutability

Variables are immutable by default. This is a core language principle.

```luma
# Immutable (default)
string name = "Alice"
integer count = 10

# Mutable (only when mutation is needed)
mutable integer total = 0

for n in nums {
    total += n
}
```

- Prefer immutable values. Use `mutable` only when the variable genuinely needs to change.
- Minimise the scope of mutable variables.
- Use compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`) and increment/decrement (`++`, `--`) when they express intent clearly.

---

## 5 — Type Annotations

Luma requires explicit type annotations on all variable declarations, function parameters, and return types.

```luma
integer count = 10
number price = 19.99
array<number> scores = [95.0, 87.5, 92.0]

function number calculate_area(number radius) {
    return Math.pi * radius * radius
}
```

Always state the return type explicitly. Use `void` for functions that return nothing.

---

## 6 — Naming Conventions

| Entity          | Convention   | Examples                               |
| --------------- | ------------ | -------------------------------------- |
| Booleans        | question     | `is_valid`, `has_children`             |
| Choice types    | `PascalCase` | `Direction`, `Status`, `Shape`         |
| Choice variants | `PascalCase` | `Direction.North`, `Shape.Circle`      |
| File names      | `snake_case` | `math_utils.luma`, `user_model.luma`   |
| Functions       | `snake_case` | `calculate_area`, `is_prime`           |
| Namespaces      | `PascalCase` | `Geometry`, `Validation`               |
| Records         | `PascalCase` | `Point`, `UserProfile`                 |
| Type aliases    | `PascalCase` | `UserId`, `Score`                      |
| Variables       | `snake_case` | `total_count`, `user_name`, `is_valid` |

- **Name what the value represents, not its type.** `connection_timeout` — good. `ct` — bad. `int_val` — bad.
- Avoid abbreviations unless they are universally understood in the domain (`id`, `url`, `max`). Never sacrifice clarity for brevity.
- Functions returning a boolean start with `is_`, `has_`, `can_`, or `should_`.
- Use `_` as the discard variable when a loop variable is intentionally unused.

```luma
# Good — names describe what the values represent.
integer retry_limit = 5
string user_email = "alice@example.com"

# Bad — abbreviated, unclear, or type-encoded.
integer rl = 5
string s1 = "alice@example.com"
```

---

## 7 — Self-Documenting Code

Write code that explains itself. Reserve comments for _why_, not _what_.

- The structure, naming, and flow of the code should make the _what_ obvious.
- Use comments to explain **intent**, **trade-offs**, **constraints**, and **non-obvious decisions** — things the code alone cannot convey.
- Delete stale or redundant comments. A wrong comment is worse than no comment.

```luma
# Bad — restates the code.
# Add one to count.
count += 1

# Good — explains intent.
# Retry once more: transient DNS failures are common on this network.
retry_count += 1
```

```luma
# Bad — obvious from the function name and types.
# This function calculates the area of a circle.
function number calculate_area(number radius) {
    return Math.pi * radius * radius
}

# Good — explains a non-obvious constraint.
# Clamped to 1000 because the upstream API rejects larger page sizes.
integer page_size = Math.min(requested_size, 1000)
```

---

## 8 — Functions

- State the return type explicitly.
- Place optional parameters after required parameters.
- Keep functions small — one logical operation per function.
- Place helper functions before `@main`, so the entry point is at the bottom of the file.

```luma
function string greet(string name, string prefix = "Hello") {
    return "${prefix}, ${name}!"
}
```

---

## 9 — Control Flow

### `if` / `else`

Use `if` as an expression when both branches produce a value to avoid mutable variables.

```luma
string label = if score >= 60 { "pass" } else { "fail" }
```

### `for` Loops

Use `for ... in` with ranges or collections. Ranges are exclusive on the right.

```luma
for i in 0..10 { print(i) }          # 0 through 9
for fruit in fruits { print(fruit) } # collection
for i, fruit in fruits { ... }       # with index
for _ in 0..5 { retry() }            # discard variable
```

### `match` Expressions

Use `match` for exhaustive pattern matching, especially with `result<T>` and choice types.

```luma
match safe_divide(10.0, 0.0) {
    success(value) { print("result: ${value}") }
    failure(msg) { print("error: ${msg}") }
}
```

Use bare integer or string literals in match arms for direct equality checks:

```luma
match day {
    case 1 { "Monday" }
    case 2 { "Tuesday" }
    else   { "other" }
}
```

---

## 10 — Error Handling

`result<T>` is the primary error-handling mechanism — not exceptions, not sentinel values.

- Functions that can fail return `result<T>`.
- Handle results with `match` for exhaustive handling, `??` for safe defaults, `?` for propagation, or `Result.unwrap_or` when you need the function form.
- Avoid naked `Result.unwrap` unless the success case is guaranteed.

```luma
function result<number> safe_divide(number a, number b) {
    if b == 0 {
        return failure("division by zero")
    }

    return success(a / b)
}
```

---

## 11 — String Interpolation

Use `${}` for embedding expressions in strings. Prefer interpolation over concatenation.

```luma
# Good
print("fibonacci(${i}) = ${fibonacci(i)}")

# Avoid
print("fibonacci(" + Converter.to_string(i) + ") = " + Converter.to_string(fibonacci(i)))
```

---

## 12 — Pipe Operator

Use `|>` for chaining operations. It passes the left-hand value as the first argument to the right-hand function.

```luma
string result = "  Hello, World!  "
    |> String.trim()
    |> String.lowercase()
    |> String.replace("world", "luma")
```

Prefer pipes over deeply nested function calls when three or more transformations are chained.

---

## 13 — Standard Library Modules

`Array`, `BinaryTree`, `Calculus`, `Channel`, `Compression`, `Console`, `Converter`, `Csv`, `DateTime`, `Decimal`, `Dictionary`, `Encoder`, `FileSystem`, `GraphicalUi`, `Hash`, `Http`, `Json`, `KeyValueStore`, `LinearAlgebra`, `Log`, `Math`, `Optional`, `Process`, `Queue`, `Random`, `Reference`, `RegularExpression`, `Resource`, `Result`, `Set`, `Socket`, `Stack`, `String`, `Task`, `Terminal`, `Xml`.

All use the pipe-first calling convention: `value |> Module.function()`.

### Sandbox Mode

When running with `--box`, modules that access OS resources are disabled: `Console`, `Csv`, `FileSystem`, `Http`, `KeyValueStore`, `Process`, `Socket`, `Xml`. Individual file-I/O functions in safe modules are also disabled: `Compression.gunzip_file`, `Compression.gzip_file`, `Hash.sha256_file`, `Hash.sha512_file`, `Log.set_output`.

Accessing a sandboxed function produces the error: `'Module.function' is not available in sandbox mode (--box)`.

### Resource Limits

The interpreter enforces limits on pending tasks (100,000), open sockets (1,000), collection sizes (10,000,000), and other resources. Always `await` tasks promptly and close sockets when finished.

---

## 14 — Concurrency

### Spawn and Await

Use `spawn` to run a function call asynchronously and `await` to retrieve the result.

```luma
task<integer> t = spawn compute(42)
integer result = await t
```

### Structured Concurrency with `task_scope`

Prefer `task_scope` over bare `spawn`/`await`. A `task_scope` block waits for all spawned tasks, cancels siblings on first failure, and returns an `array` of results.

```luma
array<integer> results = task_scope {
    spawn compute(1)
    spawn compute(2)
    spawn compute(3)
}
```

- `task_scope` blocks can be nested; inner scopes are independent of outer scopes.
- The type checker emits a warning for `spawn` outside a `task_scope`.
- Use `Task.cancel(t)` and `Task.is_cancelled(t)` for cooperative cancellation.

### Channels

Use channels for inter-task communication.

```luma
channel<integer> ch = Channel.new()
boolean _ = Channel.send(ch, 42)
integer received = Channel.receive(ch)
```

---

## 15 — Lambdas

Use lambdas for short inline operations, especially with higher-order functions like `Array.map` and `Array.filter`.

```luma
result<array<integer>> doubled = Array.map(nums, (integer x) -> x * 2)
result<array<integer>> positive = Array.filter(nums, (integer x) -> x > 0)
```

---

## 16 — Testing

- Annotate test functions with `@test`. Run with `luma --test <file>`.
- Use `assert(condition)` or `assert(condition, "message")`.
- No `@main` in test files.
- Each test function is self-contained — do not rely on execution order.
- See [testing.instructions.md](testing.instructions.md) for the full testing guide.

---

## 17 — Formatting

- **Indentation:** 4 spaces. No tabs.
- **Line length:** 100 characters maximum.
- **File ending:** single trailing newline.

### Horizontal Spacing

- Place a space after keywords: `if (`, `for (`, `match (`.
- Place a space around binary operators: `a + b`, `x == y`, `i < n`.
- No space after unary operators: `!flag`, `++i`.
- No space inside parentheses: `func(a, b)`, not `func( a, b )`.

### Vertical Spacing

Use blank lines to reveal logical structure — like paragraphs in prose.

- **One blank line** between top-level declarations (functions, records, choice types).
- **One blank line** between logical blocks within a function (setup, processing, result).
- **No** multiple consecutive blank lines. A single blank line is the unit of separation.

```luma
function integer sum_positive(array<integer> numbers) {
    result<array<integer>> filtered = Array.filter(numbers, (integer x) -> x > 0)

    match filtered {
        success(values) {
            mutable integer total = 0

            for n in values {
                total += n
            }

            return total
        }
        failure(msg) { return 0 }
    }
}

function boolean is_even(integer n) {
    return n % 2 == 0
}
```

---

## 18 — File Organisation

- One concept per file.
- Place helper functions before the `@main` entry point.
- Use file headers in test suites:

```luma
# ═══════════════════════════════════════════════════════════
# Luma v1.0 — Feature Tests — <Topic>
#
# Feature Tests are based on Luma_Initial_Concept.md and
# Luma_User_Manual.md.
# ═══════════════════════════════════════════════════════════
```

Standard library test suites under `tests/features/stdlib/` reference `Luma_Standard_Library_Reference.md` in place of `Luma_User_Manual.md`.

---

## 19 — Anti-Patterns

- **Unnecessary mutability.** Do not use `mutable` when an immutable variable suffices.
- **Ignoring** `result<T>`. Always handle both `success` and `failure` arms.
- **String concatenation for formatting.** Use string interpolation with `${}`.
- **Deep nesting.** Use early returns, pipes, or helper functions to flatten logic.
- **Bare `spawn`/`await`.** Prefer `task_scope` for automatic cleanup and cancellation.

---

## 20 — Checklist

- [ ] Is this the simplest correct solution?
- [ ] No semicolons in Luma source.
- [ ] Every variable has an explicit type annotation.
- [ ] `mutable` is used only when mutation is necessary.
- [ ] All `result<T>` values are handled — no naked `Result.unwrap`.
- [ ] Names follow the conventions in §6.
- [ ] Functions are small — one logical operation each.
- [ ] Each record, choice type, and namespace models one concept.
- [ ] String interpolation is used instead of concatenation.
- [ ] Pipes are used for three or more chained transformations.
- [ ] Concurrent tasks use `task_scope` — no bare `spawn`/`await`.
- [ ] Tests use `@test`, `assert`, and contain no `@main`.
- [ ] Files end with a single trailing newline.
