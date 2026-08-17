# Luma — Coding Guidelines

> Conventions and best practices for writing clear, idiomatic Luma source code.

This document is the definitive style guide for Luma programs. It is derived from the language design goals in [Luma_Initial_Concept.md](Luma_Initial_Concept.md), the architecture described in [Luma_Software_Architecture.md](Luma_Software_Architecture.md), the syntax defined in [Luma_User_Manual.md](Luma_User_Manual.md), and the patterns established in the [examples](../examples/) and [test suites](../tests/features/).

---

## Table of Contents

1. [Design Philosophy](#1--design-philosophy)
2. [Naming Conventions](#2--naming-conventions)
3. [Formatting and Whitespace](#3--formatting-and-whitespace)
4. [Comments](#4--comments)
5. [Variables and Mutability](#5--variables-and-mutability)
6. [Type Annotations](#6--type-annotations)
7. [Functions](#7--functions)
8. [Control Flow](#8--control-flow)
9. [Error Handling](#9--error-handling)
10. [Match Expressions](#10--match-expressions)
11. [Collections](#11--collections)
12. [Strings](#12--strings)
13. [Pipe Operator](#13--pipe-operator)
14. [Lambdas and Higher-Order Functions](#14--lambdas-and-higher-order-functions)
15. [Records](#15--records)
16. [Choice Types](#16--choice-types)
17. [Choice Types — Advanced](#17--choice-types--advanced)
18. [Interfaces](#18--interfaces)
19. [Namespaces](#19--namespaces)
20. [Concurrency](#20--concurrency)
21. [Testing](#21--testing)
22. [File Organisation](#22--file-organisation)
23. [Anti-Patterns](#23--anti-patterns)
24. [Security and Resource Limits](#24--security-and-resource-limits)

- [See Also](#see-also)

---

## 1 — Design Philosophy

Luma is designed for beginners. Every guideline in this document serves three goals:

- **Clarity.** A reader should understand what the code does within seconds.
- **Safety.** Immutability by default, static types, and explicit error handling prevent entire classes of bugs.
- **Simplicity.** Prefer the straightforward approach. If there is a simpler way, use it.

When two approaches are equally correct, choose the one that is easier to read.

---

## 2 — Naming Conventions

| Entity          | Convention            | Examples                                    |
| --------------- | --------------------- | ------------------------------------------- |
| Booleans        | Phrased as a question | `is_valid`, `has_children`, `should_retry`  |
| Choice types    | `PascalCase`          | `Direction`, `Status`, `Shape`              |
| Choice variants | `PascalCase`          | `Direction.North`, `Shape.Circle`           |
| File names      | `snake_case.luma`     | `math_utils.luma`, `user_model.luma`        |
| Functions       | `snake_case`          | `calculate_area`, `is_prime`, `safe_divide` |
| Namespaces      | `PascalCase`          | `Geometry`, `Validation`                    |
| Records         | `PascalCase`          | `Point`, `UserProfile`, `HttpResponse`      |
| Type aliases    | `PascalCase`          | `UserId`, `Score`, `UserList`               |
| Variables       | `snake_case`          | `total_count`, `user_name`, `is_valid`      |

### Naming Tips

- Choose descriptive names. `connection_timeout` — good. `ct` — bad.
- Functions that return a boolean start with `is_`, `has_`, `can_`, or `should_`.
- Functions that perform an action use a verb: `calculate_distance`, `parse_header`.
- Avoid abbreviations unless they are universally understood (`id`, `url`, `max`).
- Use the discard variable `_` when a loop variable is intentionally unused.

```luma
# Good — descriptive names with clear intent
function boolean is_positive(integer n) {
    return n > 0
}

for _ in 0..10 {
    count++
}

# Bad — unclear names
function boolean ip(integer n) {
    return n > 0
}
```

---

## 3 — Formatting and Whitespace

### Indentation

Use four spaces per indentation level. Do not use tabs.

### Braces

Always use braces for `if`, `else`, `for`, `match`, and function bodies — even for single-statement bodies. Place the opening brace on the same line as the keyword.

```luma
# Good
if score >= 90 {
    grade = "A"
}

# Bad — no braces
if score >= 90
    grade = "A"
```

### Blank Lines

Use one blank line to separate logical sections within a function and between top-level declarations. Do not use multiple consecutive blank lines.

```luma
function result<integer> process(array<integer> data) {
    # Filter
    result<array<integer>> positive = Array.filter(data, (integer x) -> x > 0)

    # Transform
    result<array<integer>> doubled = positive
        !> Array.map((integer x) -> x * 2)

    # Aggregate
    return doubled !> Array.sum()
}
```

### Line Length

Aim for a maximum of 100 characters per line. Break long expressions at logical boundaries — pipe operators, function arguments, or binary operators.

### Semicolons

Do not use semicolons. Luma does not require them, and the codebase omits them everywhere.

---

## 4 — Comments

Comments start with `#`. Use them to explain **why**, not **what**. The code itself should make the _what_ obvious through clear naming and structure.

```luma
# Good — explains a non-obvious decision
# Range is exclusive on the right, so 101 includes 100
for i in 1..101 {
    print(i)
}

# Bad — restates the code
# Loop from 1 to 100
for i in 1..101 {
    print(i)
}
```

### Section Separators

Use comment separators to group related declarations within a file. Follow the pattern established in the test suites:

```luma
# ─── Section Name ───

@test
function void test_something() {
    assert(true)
}
```

For file headers, use the double-line separator:

```luma
# ═══════════════════════════════════════════════════════════
# Luma v1.0 — Feature Tests — Arrays
#
# Feature Tests are based on Luma_Initial_Concept.md and
# Luma_User_Manual.md.
# ═══════════════════════════════════════════════════════════
```

---

## 5 — Variables and Mutability

Variables are immutable by default. This is a core language principle — not an afterthought.

### Prefer Immutable Values

Declare variables without `mutable` unless mutation is genuinely required.

```luma
# Good — immutable by default
string name = "Alice"
integer count = 10
array<integer> nums = [1, 2, 3]

# Only use mutable when the variable must change
mutable integer total = 0

for n in nums {
    total += n
}
```

### Minimise Mutable Scope

When mutation is needed, keep the mutable variable as close to its use as possible and limit the scope of mutation.

```luma
# Good — mutable variable used briefly, then consumed
mutable string result = ""

for i, fruit in fruits {
    result = result + "${i}: ${fruit}\n"
}

print(result)
```

### Compound Assignment Operators

Use compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`) and increment/decrement (`++`, `--`) when they express intent more clearly than the expanded form.

```luma
# Preferred
total += price
counter++

# Equivalent but verbose
total = total + price
counter = counter + 1
```

---

## 6 — Type Annotations

Luma requires explicit type annotations on all variable declarations, function parameters, and return types. This is by design — types serve as documentation.

### Always Annotate

```luma
# Good — types are explicit
number count = 10
number price = 19.99
string name = "Alice"
array<number> scores = [95.0, 87.5, 92.0]
dictionary<number> ages = {"alice": 30, "bob": 25}

# Function signatures
function number calculate_area(number radius) {
    return Math.pi * radius * radius
}
```

### Use Type Aliases for Domain Concepts

When a primitive type carries domain meaning, define a type alias to communicate intent.

```luma
type UserId = string
type Score = number
type UserList = array<string>

function Score lookup(UserId id) {
    # The parameter name and type alias together convey meaning
}
```

### Integer Versus Number

`integer` and `number` are distinct types. Integer division truncates; mixed arithmetic promotes the integer to a number.

```luma
7 / 2   # 3 (integer division, truncates toward zero)
7 // 2  # 3 (integer division — explicit, integer-only operator)
7.0 / 2 # 3.5 (number division, integer 2 promoted)
7 / 2.0 # 3.5 (number division, integer 7 promoted)
```

Use `//` when both operands are `integer` and you want to make the integer-division intent explicit in the code.

**Rule: `integer` is for indices and range bounds.** Use `integer` for values that index into a collection or string (array subscripts, string offsets, loop counters used as indices) and for range end-points (`0..n`). Use `number` for everything else — counts, sizes, quantities, scores, IDs, measurements, and mathematical computations. This makes intent immediately visible: a variable typed `integer` addresses a position or delimits a range; a variable typed `number` holds a value.

```luma
# Good — integer for indices, number for values
number count = 10
number price = 19.99
number age = 30

for integer i in 0..Array.length(items) {
    print("Item ${i}: ${items[i]}")
}

integer index = String.index_of(text, "x") |> Result.unwrap()
string char = text[index]
```

```luma
# Bad — using integer for non-index values
integer count = 10    # count is not an index
integer age = 30      # age is not an index
integer score = 100   # score is not an index
```

### Prefer Concrete Types

Luma's type system is static and manifest — always declare the most specific concrete type. Use generic type parameters (`<T>`) when a function or record must work across multiple types rather than duplicating code.

Use plain `downcast<T>` when the type mismatch is recoverable. Use `trusted_downcast<T>` only when you are certain the type matches and want to skip the `result` unwrapping ceremony.

---

## 7 — Functions

### Signature Conventions

- State the return type explicitly. Use `void` for functions that return nothing.
- Place optional parameters after required parameters.
- Use named arguments when a call site has multiple arguments of the same type and the meaning is not obvious.

```luma
# Good — clear signature with return type
function result<number> safe_divide(number a, number b) {
    if b == 0 {
        return failure("division by zero")
    }

    return success(a / b)
}

# Good — optional parameter with default
function string greet(string name, string prefix = "Hello") {
    return "${prefix}, ${name}!"
}

# Good — named arguments clarify meaning at call site
string user = create_user(name: "Alice", age: 30, active: true)
```

### Keep Functions Small

A function performs one logical operation. If a function name needs the word "and", it does too much.

### Entry Point

Every runnable program has exactly one `@main`-annotated function. It takes no parameters. Place helper functions before `@main`, so the entry point is at the bottom of the file.

```luma
function integer fibonacci(integer n) {
    if n <= 1 {
        return n
    }

    return fibonacci(n - 1) + fibonacci(n - 2)
}

@main
function void main() {
    for i in 0..10 {
        print("fibonacci(${i}) = ${fibonacci(i)}")
    }
}
```

---

## 8 — Control Flow

### `if` / `else if` / `else`

Braces are required on every branch.

```luma
if score >= 90 {
    grade = "A"
} else if score >= 80 {
    grade = "B"
} else {
    grade = "F"
}
```

### `if` as Expression

When both branches produce a value of the same type, use `if` as an expression to avoid mutable variables.

```luma
# Good — expression, no mutable
string label = if score >= 60 { "pass" } else { "fail" }

# Less idiomatic — mutable variable
mutable string label = "fail"

if score >= 60 {
    label = "pass"
}
```

### `for` Loops

Use `for ... in` with ranges or collections. Use `_` for the loop variable when it is unused.

```luma
# Range (exclusive on the right)
for i in 0..10 {
    print(i)
}

# Collection
for fruit in fruits {
    print(fruit)
}

# With index
for i, fruit in fruits {
    print("${i}: ${fruit}")
}

# Discard variable
for _ in 0..5 {
    retry()
}
```

### `break` and `continue`

Use sparingly. Prefer filtering or early returns over complex loop control.

---

## 9 — Error Handling

Luma distinguishes two categories of failure. Choosing the right mechanism is essential for clear, correct code.

| Category              | When to Use                                             | Mechanism                                   |
| --------------------- | ------------------------------------------------------- | ------------------------------------------- |
| **Domain failures**   | Expected conditions the caller is responsible for       | `result<T>`, `optional<T>`                  |
| **Programmer errors** | Conditions that should never occur in a correct program | Runtime error + `try` / `catch` / `finally` |

> **Design rule:** Use `result<T>` or `optional<T>` for recoverable domain failures — missing files, invalid input, timeouts. Use `try`/`catch` only for programmer errors — division by zero, out-of-bounds access, failed assertions. Do not mix the two categories.

For the full specification, see [Luma_Error_Handling.md](Luma_Error_Handling.md).

### `result<T>`

`result<T>` is the primary error-handling mechanism. It represents either `success(value)` or `failure(message)`.

Use `result<T, E>` with a second type parameter when you need a typed error value instead of a plain string:

```luma
choice ParseError { InvalidSyntax UnexpectedToken }

result<integer, ParseError> parse(string input) {
    return failure(ParseError.InvalidSyntax)
}
```

### Returning Results

Functions that can fail return `result<T>` instead of throwing or returning sentinel values.

```luma
function result<number> safe_divide(number a, number b) {
    if b == 0 {
        return failure("division by zero")
    }

    return success(a / b)
}
```

### Handling Results

Prefer `match` for exhaustive handling. Use `??` or `Result.unwrap_or` for safe defaults. Avoid naked `Result.unwrap` unless the success case is guaranteed.

```luma
# Preferred — exhaustive match
match safe_divide(10.0, 0.0) {
    success(value) { print("result: ${value}") }
    failure(msg)   { print("error: ${msg}") }
}

# Acceptable — safe default with ??
number value = safe_divide(10.0, 0.0) ?? 0.0

# Also acceptable — safe default with Result.unwrap_or
number value2 = Result.unwrap_or(safe_divide(10.0, 0.0), 0.0)
```

### Propagating Results with `?`

Inside a function that returns `result<T>`, the postfix `?` operator unwraps `success(v)` to `v` or immediately returns the `failure` to the caller. This avoids verbose `match` boilerplate in multi-step fallible functions.

```luma
function result<number> parse_and_double(string s) {
    number n = Converter.to_number(s)?  # propagates failure if conversion fails

    return success(n * 2)
}
```

`?` is a type error if the enclosing function does not return `result<T>` or `optional<T>`.

### Chaining Results

Use the `!>` error-pipe operator for linear fallible pipelines. It short-circuits on the first `failure` and always produces `result<T>`. Use `|>` with `Result` module functions for other compositions.

```luma
# Good — !> for linear fallible chains
result<integer> r = success(raw_input) !> parse() !> validate() !> transform()

# Also valid — |> with Result module functions
result<number> final = safe_divide(100.0, 5.0)
    |> Result.map_number((number v) -> v * 2)
    |> Result.filter((number v) -> v > 0, "must be positive")
```

### Never Ignore Results

Every `result<T>` must be handled — matched, unwrapped with a default (`??`), or propagated (`?`). Silently dropping a result hides errors. The type checker emits a warning when a `result<T>` return value is discarded. Assign to `_` to explicitly acknowledge and suppress the warning.

```luma
_ = parse(input) # explicitly discard — warning suppressed
```

### `optional<T>`

`optional<T>` represents a value that may or may not be present: either `some(value)` or `none`. Use it when absence is a normal outcome rather than an error — for example, looking up a key that might not exist.

```luma
# Exhaustive match
match Dictionary.get(config, "host") {
    case some(host) { connect(host) }
    case none       { print("no host configured") }
}

# Default with ??
string host = Dictionary.get(config, "host") ?? "localhost"
```

Use the postfix `?` operator inside a function that returns `optional<T>` to propagate `none`:

```luma
function optional<string> display_name(optional<User> user) {
    User u = user?                  # returns none if user is none
    string name = find_name(u.id)?  # returns none if name not found

    return name
}
```

Use `?.` to safely access a field on an optional record and `?[` to safely index an optional array. The result is `optional<T>`:

```luma
record Address { string city }
record User    { string name, optional<Address> address }

optional<User> user = find_user(id)

string city = user?.address?.city ?? "unknown"
```

### `try` / `catch` / `finally`

`try`/`catch`/`finally` handles **programmer errors only** — division by zero, out-of-bounds access, `assert` failures, and failed `Result.unwrap` calls. It does not intercept syntax or type errors.

```luma
try {
    integer result = 10 / 0
} catch(err) {
    print("caught: ${err}")  # caught: division by zero
} finally {
    print("always runs")
}
```

Do not use `try`/`catch` to handle expected domain failures. If a function returns `result<T>`, use `match`, `??`, or `?` — not `try` around `Result.unwrap`.

### Standard Library Error Conventions

The standard library follows consistent rules that your code should rely on:

- **Infallible operations return plain values.** No wrapping needed: `Converter.to_string(42)` returns `string`.
- **Fallible operations return `result<T>`.** The type system forces handling: `FileSystem.read_file(path)` returns `result<string>`.
- **Callbacks that throw produce `failure`.** Higher-order functions like `Array.map` convert a runtime error inside the callback into `failure(message)` rather than crashing.

Follow these same conventions in your own code — see [Luma_Error_Handling.md §6–§7](Luma_Error_Handling.md#6--standard-library-conventions) for full details.

---

## 10 — Match Expressions

### Exhaustiveness

Match expressions must be exhaustive:

- `boolean` — cover `true` and `false`.
- Choice type — cover every variant.
- `result<T>` — cover `success` and `failure`.
- Other types — use `else` as a catch-all.

```luma
# Boolean — both branches required
match flag {
    case true  { print("enabled") }
    case false { print("disabled") }
}

# Choice type — all variants required
match direction {
    case Direction.North { print("↑") }
    case Direction.South { print("↓") }
    case Direction.East  { print("→") }
    case Direction.West  { print("←") }
}

# Result — ok and fail required
match safe_divide(a, b) {
    success(value) { print("result: ${value}") }
    failure(msg)   { print("error: ${msg}") }
}

# Comparison — else required
match score {
    case >= 90 { "A" }
    case >= 80 { "B" }
    else       { "F" }
}

# Integer literal — else required
match day {
    case 1 { "Monday" }
    case 2 { "Tuesday" }
    else   { "other" }
}
```

### Match as Expression

Use match as an expression when all arms produce the same type. This avoids mutable variables.

```luma
string grade = match score {
    case >= 90 { "A" }
    case >= 80 { "B" }
    case >= 70 { "C" }
    else       { "F" }
}
```

### Alignment

Align match arms for readability. Place the opening brace on the same line as the arm condition.

```luma
match command {
    case "quit" { stop() }
    case "help" { show_help() }
    else           { print("unknown: ${command}") }
}
```

### Multiple Patterns Per Arm

Use `|` to combine several patterns in a single arm. The arm matches if any of the alternatives match. Only simple patterns (boolean, integer, choice variant, string literal, comparison, `none`) may be combined.

```luma
match command {
    case "quit" | "exit" | "q" { stop() }
    case "help" | "?"          { show_help() }
    else                       { print("unknown") }
}

match code {
    case 1 | 2 | 3 { "low" }
    case 4 | 5 | 6 { "mid" }
    else           { "high" }
}
```

---

## 11 — Collections

### Choosing the Right Collection

Luma offers several collection types. Choose based on the access pattern:

| Collection      | Use When                                                              |
| --------------- | --------------------------------------------------------------------- |
| `array<T>`      | Default choice. Ordered data with index-based access.                 |
| `dictionary<V>` | Key-value lookups with string keys.                                   |
| `set<T>`        | Unordered unique elements. Membership testing.                        |
| `queue<T>`      | FIFO semantics — producer-consumer patterns.                          |
| `stack<T>`      | LIFO semantics — depth-first traversal, undo buffers.                 |

Prefer `array<T>` for ordered, index-accessed data and `set<T>` when membership testing is the primary operation. All collections have value semantics — operations return new copies rather than mutating in place.

### Performance: Immutability Costs

Luma collections are immutable. Every mutation — `Array.push`, `Dictionary.set`, `Set.add` — returns a **new copy** of the collection (O(n)). This guarantees safety but has a cost.

**Build collections in a single pass** rather than appending one element at a time in a loop. Use `Array.map`, `Array.filter`, or `Array.reduce` to transform collections without repeated copying.

```luma
# Good — single pass, one copy
result<array<integer>> doubled = Array.map(nums, (integer x) -> x * 2)

# Bad — O(n²) from n copies inside the loop
mutable array<integer> out = []

for x in nums {
    out = Array.push(out, x * 2)
}
```

### Arrays

Arrays are homogeneous, ordered sequences typed as `array<T>`.

```luma
array<integer> nums = [1, 2, 3, 4, 5]
array<string> empty = []
```

**Safe access.** Use `Array.get` (returns `result<T>`) instead of direct indexing when bounds are uncertain. Use `??` to provide a default on failure.

```luma
# Safe — returns result
result<integer> r = Array.get(nums, 10)

# Safe — returns default on out-of-bounds
integer v = Array.get(nums, 10) ?? 0
```

**Functional transformations.** Prefer `Array.map`, `Array.filter`, `Array.reduce`, and other standard library functions over mutable loops. Standard library functions return new arrays — they never modify the original.

```luma
result<array<integer>> doubled = Array.map(nums, (integer x) -> x * 2)
result<array<integer>> evens = Array.filter(nums, (integer x) -> x % 2 == 0)
result<integer> total = Array.sum(nums)
```

### Dictionaries

Dictionaries are string-keyed, homogeneous maps typed as `dictionary<T>` where `T` is the value type. Keys are always strings.

```luma
dictionary<number> scores = {"alice": 95.0, "bob": 87.0}
```

**Always use safe access.** `Dictionary.get` returns `result<T>`. Use `Dictionary.get_or` for fallback values.

```luma
number score = Dictionary.get_or(scores, "alice", 0.0)
```

**Modifications return new dictionaries.** The original is never mutated.

```luma
dictionary<number> updated = Dictionary.set(scores, "carol", 91.0)
dictionary<number> merged = Dictionary.merge(base, overlay)
```

### Tuples

Tuples hold two to four elements of potentially different types. Access fields by zero-based index.

```luma
(integer, string) pair = (42, "hello")
print(pair.0) # 42
print(pair.1) # "hello"

# Destructuring
(integer id, string name) = get_user()
```

Use tuples to return multiple values from a function. If the tuple grows beyond four fields or carries domain meaning, use a record instead.

---

## 12 — Strings

### Prefer Interpolation over Concatenation

String interpolation is clearer than concatenation for assembling messages.

```luma
# Good — interpolation
string msg = "Hello, ${name}! You scored ${score} points."

# Less readable — concatenation
string msg = "Hello, " + name + "! You scored " + Converter.to_string(score) + " points."
```

### Multi-Line Strings

Use triple-quoted strings for multi-line text. Indentation relative to the closing `"""` is removed automatically.

```luma
string letter = """
    Dear ${name},
    Thank you for your message.
    Best regards.
    """
```

### String Building Performance

Building strings by repeated concatenation in a loop creates a new string on each iteration — O(n²) for n iterations. Use `Array.map` with `String.join` instead.

```luma
# Good — single pass
array<string> parts = Array.map(items, (string item) -> item)
string result = String.join(parts, ", ")

# Bad — O(n²) from repeated concatenation
mutable string result = ""

for item in items {
    result = result + item + ", "
}
```

### String Containment

Use the `in` operator for substring checks. It reads naturally and is consistent with array and dictionary membership checks.

```luma
boolean found = "world" in "hello world" # true
```

---

## 13 — Pipe Operator

The pipe operator `|>` passes the left-hand value as the first argument to the right-hand function. It is the idiomatic way to chain operations.

### Prefer Pipes over Nested Calls

```luma
# Good — reads left to right
string result = "  hello world  "
    |> String.trim()
    |> String.uppercase()

# Less readable — reads inside out
string result = String.uppercase(String.trim("  hello world  "))
```

### Multi-Line Pipelines

Break long pipelines across multiple lines. Indent continuation lines by four spaces. Place each `|>` at the start of a new line.

```luma
result<array<string>> processed = raw_lines
    !> Array.map((string s) -> String.trim(s))
    !> Array.filter((string s) -> !String.is_empty(s))
    !> Array.map((string s) -> String.uppercase(s))
    !> Array.sort_by((string s) -> s)
```

### Extract Named Functions for Readability

When a lambda within a pipeline is complex, extract it into a named function.

```luma
# Good — named function clarifies intent
function boolean is_positive(integer n) {
    return n > 0
}

function integer double(integer n) {
    return n * 2
}

result<array<integer>> result = numbers
    !> Array.filter(is_positive)
    !> Array.map(double)

# Less readable — inline lambdas obscure intent
result<array<integer>> result = numbers
    !> Array.filter((integer n) -> n > 0)
    !> Array.map((integer n) -> n * 2)
```

### Error-Pipe Operator `!>`

Use `!>` to chain fallible operations where any failure should short-circuit the pipeline. Unlike `|>`, the `!>` operator unwraps an `success` value before passing it to the next step, and propagates a `failure` immediately without calling subsequent steps. The overall expression always produces `result<T>`.

```luma
result<integer> r = success("42")
    !> parse_positive()
    !> double()
```

If `parse_positive` returns `failure(...)`, `double` is never called and the failure is the result of the whole expression. A plain (non-`result`) value on the left is treated as `success(value)`.

---

## 14 — Lambdas and Higher-Order Functions

### Single-Expression Lambdas

Use the arrow syntax for single-expression lambdas.

```luma
(integer x) -> x * 2
(string s) -> String.uppercase(s)
() -> 42
```

### Multi-Statement Lambdas

Use a block body and an explicit `return` when the lambda contains more than one expression.

```luma
(integer x) -> {
    integer y = x * x
    return y + 1
}
```

### Capture Semantics

Lambdas capture variables by value at creation time. Mutations to the captured variable after lambda creation do not affect the lambda.

```luma
integer threshold = 60
array<integer> passing = Array.filter(scores, (integer s) -> s >= threshold) # threshold is captured as 60 — later changes are invisible to the lambda
```

### Storing Lambdas

Use the `function` type annotation to store a lambda in a variable.

```luma
function(integer) -> boolean is_even = (integer x) -> x % 2 == 0
print(is_even(4)) # true
```

---

## 15 — Records

### Declaration

Declare fields on separate lines, separated by commas. Use `PascalCase` for the record name and `snake_case` for field names.

```luma
record Point {
    number x,
    number y
}

record UserProfile {
    string name,
    integer age,
    boolean active
}
```

### Instantiation

Provide all fields when creating a record. Field order does not matter.

```luma
Point origin = Point { x = 0.0, y = 0.0 }
UserProfile user = UserProfile { name = "Alice", age = 30, active = true }
```

### Mutation

Record variables must be `mutable` for field assignment.

```luma
mutable Point p = Point { x = 0.0, y = 0.0 }
p.x = 5.0
p.y = 10.0
```

### Keep Records Focused

A record models one concept. Every field should belong to that concept. If a record accumulates unrelated fields, split it into smaller records or use composition.

```luma
# Good — each record models one concept
record Address {
    string street,
    string city,
    string country
}

record Customer {
    string name,
    Address address
}

# Bad — unrelated concerns in one record
record Customer {
    string name,
    string street,
    string city,
    string country,
    number account_balance,
    string last_login_ip
}
```

### Records Versus Tuples

Use a record when the data has domain meaning or more than two or three fields. Use a tuple for transient groupings like returning two values from a function.

---

## 16 — Choice Types

Choice types define a closed set of variants where each variant can optionally carry data. A choice with all unit variants (no data) serves the same role as an enumeration.

### Declaration

Use `PascalCase` for the choice name and each variant. List each variant on a separate line for readability.

```luma
choice Direction {
    North
    South
    East
    West
}

choice Status {
    Active
    Paused
    Archived
}

choice Shape {
    Circle(number radius)
    Rectangle(number width, number height)
    Point
}
```

### Usage

Always use qualified variant names (`ChoiceName.Variant`).

```luma
Direction heading = Direction.North

if heading == Direction.North {
    print("heading north")
}
```

### One Concept Per Choice Type

A choice type represents one decision or one dimension of variation. Do not merge unrelated alternatives into a single choice. If two groups of variants are independent, define separate choice types.

```luma
# Good — each choice models one concept
choice Colour { Red  Green  Blue }
choice Size   { Small  Medium  Large }

# Bad — unrelated concepts merged
choice Attribute {
    Red
    Green
    Blue
    Small
    Medium
    Large
}
```

### Always Match Exhaustively

A `match` on a choice type must cover every variant. Missing a variant is a compile-time error. Destructure data-variant fields directly in the arm:

```luma
match status {
    case Status.Active   { print("running") }
    case Status.Paused   { print("paused") }
    case Status.Archived { print("done") }
}

number area = match shape {
    case Shape.Circle(r)       { 3.14159 * r * r }
    case Shape.Rectangle(w, h) { w * h }
    case Shape.Point           { 0.0 }
}
```

---

## 17 — Choice Types — Advanced

### Recursive Choice Types

Choice types can reference themselves to build tree and list structures. Combine with generics for reusable data structures:

```luma
choice List<T> {
    Nil
    Cons(T head, List<T> tail)
}
```

Process recursive choices with recursive functions and match destructuring. Ensure every recursive function has a base case that terminates (e.g., `Nil` for lists, `Leaf` for trees).

---

## 18 — Interfaces

Interfaces define a set of fields that a record must provide to satisfy the interface. Luma uses structural typing — no `implements` keyword is needed.

```luma
interface Named {
    string name
}

record Player {
    string name,
    integer level
}

function string greet(Named entity) {
    return "Hello, ${entity.name}!"
}

# Player satisfies Named because it has a 'name' field of type string
Player p = Player { name = "Alice", level = 5 }
print(greet(p))
```

Use interfaces when a function only needs a subset of a record's fields. This keeps functions loosely coupled and reusable across different record types.

---

## 19 — Namespaces

### Declaring Namespaces

Use `PascalCase` for namespace names. Group related functions, records, choice types, type aliases, and interfaces together.

### Keep Namespaces Cohesive

A namespace groups declarations that belong to one domain concept. Every member should relate to the namespace's name. If a namespace grows to cover multiple unrelated concerns, split it into separate namespaces.

```luma
# Good — focused namespaces
namespace Geometry {
    function number area(number radius) { ... }
    function number perimeter(number radius) { ... }
}

namespace Colour {
    function string to_hex(integer r, integer g, integer b) { ... }
    function (integer, integer, integer) from_hex(string hex) { ... }
}

# Bad — unrelated concerns in one namespace
namespace Utils {
    function number area(number radius) { ... }
    function string to_hex(integer r, integer g, integer b) { ... }
    function string format_date(string date) { ... }
}
```

```luma
namespace Geometry {
    record GeoPoint {
        number x,
        number y
    }

    choice Quadrant { I  II  III  IV }

    type Distance = number

    function Distance distance(GeoPoint a, GeoPoint b) {
        number dx = a.x - b.x
        number dy = a.y - b.y

        return Result.unwrap(Math.square_root(dx * dx + dy * dy))
    }
}
```

### Qualified Access

All namespace members support the `Namespace.member` qualified syntax. For records and type aliases, qualify the type annotation and creation expression. For choice variants, use `Namespace.Choice.Variant`.

```luma
# Qualified record type annotation and creation.
Geometry.GeoPoint p = Geometry.GeoPoint { x = 3.0, y = 4.0 }

# Qualified choice variant.
Geometry.Quadrant q = Geometry.Quadrant.I

# Qualified type alias.
Geometry.Distance d = Geometry.distance(p, origin)
```

### Bare Names Require `use`

Namespace members are not available as bare (unqualified) names unless explicitly imported with `use Namespace`. Always use `use` when you reference a namespace's members frequently, and prefer qualified names when clarity matters or when two namespaces define the same name.

```luma
# Without use — only qualified names work.
Geometry.GeoPoint p = Geometry.GeoPoint { x = 3.0, y = 4.0 }

# With use — bare names become available.
use Geometry

GeoPoint p = GeoPoint { x = 3.0, y = 4.0 }
Distance d = distance(p, origin)

# Qualified access always works regardless of use.
Geometry.Distance d = Geometry.distance(p, origin)
```

### Internal Members

Use the `internal` keyword to hide implementation details that should not be part of the namespace's public API. Internal members are only accessible from within the namespace body; they cannot be called from outside, and `use Namespace` never imports them.

```luma
namespace Formatter {
    # Public API.
    function string format(string text) {
        return Formatter.capitalise(Formatter.strip(text))
    }

    # Internal helpers — invisible outside this namespace.
    internal function string strip(string text) {
        return String.trim(text)
    }

    internal function string capitalise(string text) {
        return String.title_case(text)
    }

    internal record Options { boolean preserve_case }

    internal choice Mode { Title, Lower, Upper }

    internal type Tags = array<string>
}
```

**Rules:**

- The `internal` keyword appears immediately before the declaration keyword (`function`, `record`, `choice`, `interface`, `type`) inside a namespace body.
- Internal members remain in the namespace's environment and are callable by other functions within the same namespace.
- Using `internal` at the top level (outside any namespace) is a syntax error.
- `use Namespace` skips all internal members.

### Standard Library Modules

Standard library modules are always available by qualified name. No `use` is needed.

```luma
# All standard library calls use Module.function style
String.uppercase("hello")
Array.filter(nums, predicate)
Math.square_root(25.0)
Dictionary.get(scores, "alice")
```

---

## 20 — Concurrency

### Tasks

Use `spawn` to run a function in the background. Always `await` the task to retrieve its result.

```luma
task<number> t = spawn calculate(data)

# Do other work while the task runs

number result = await t
```

Tasks receive a deep copy of their environment. They do not share mutable state with the spawning scope. Communicate between tasks using channels.

### Structured Concurrency

Prefer `task_scope` over bare `spawn`/`await` to ensure all tasks complete before continuing. A `task_scope` block collects child results in spawn order and cancels remaining siblings on first failure.

```luma
array<number> results = task_scope {
    spawn calculate(data1)
    spawn calculate(data2)
    spawn calculate(data3)
}
```

Using `spawn` outside a `task_scope` is allowed but produces a type-checker warning. Fire-and-forget tasks are still permitted when structured lifetimes are not needed.

### Cooperative Cancellation

Use `Task.cancel(t)` to request cancellation and `Task.is_cancelled(t)` to check the status. Tasks check cancellation cooperatively at function calls, loop iterations, and `await` points.

```luma
task_scope {
    task<integer> long_task = spawn expensive_search(1000000)

    # Request cancellation — the task will stop at its next check point
    Task.cancel(long_task)

    boolean cancelled = Task.is_cancelled(long_task)
    print("Cancelled: ${cancelled}")  # true
}
```

**Cancel-on-first-failure:** When any child task in a `task_scope` throws, all remaining sibling tasks are automatically cancelled and the error propagates to the scope.

**Race cancellation:** `Task.race` cancels all losing tasks once the first task completes.

**Task.all cancellation:** `Task.all` cancels remaining tasks on the first failure.

### Channels

Channels are typed, thread-safe message queues.

```luma
# Unbuffered — send blocks until a receiver is ready
channel<integer> ch = Channel.new()

# Buffered — allows limited queueing
channel<integer> ch = Channel.new_buffered(10)

Channel.send(ch, 42)
integer msg = Channel.receive(ch)
```

Values are deep-copied on send. The sender and receiver work with independent copies.

---

## 21 — Testing

### Writing Tests

Annotate test functions with `@test`. Test functions take no parameters and have a `void` return type. Each test runs in an isolated environment — no shared mutable state between tests.

```luma
@test
function void test_addition() {
    assert(1 + 1 == 2)
    assert(2 + 3 == 5)
}
```

### Assertions

Use `assert(condition)` to fail a test immediately when the condition is false. Provide a custom message as the second argument when the default `"assertion failed"` is not descriptive enough.

```luma
@test
function void test_with_message() {
    integer result = compute()

    assert(result == 42, "expected 42 but got ${result}")
}
```

### Test Structure

Follow the **Arrange–Act–Assert** pattern. Separate the three sections with blank lines and label them with comments when the test is non-trivial.

```luma
@test
function void test_array_pipeline() {
    # Arrange
    array<integer> nums = [1, 2, 3, 4, 5]

    # Act
    result<array<integer>> evens = nums
        !> Array.filter((integer x) -> x % 2 == 0)
        !> Array.map((integer x) -> x * 10)

    # Assert
    assert(evens == success([20, 40]))
}
```

### Test Naming

Name test functions `test_<feature_being_tested>`. Use `snake_case`. The name should describe the behaviour, not the implementation.

```luma
# Good
test_safe_divide_by_zero
test_array_filter_removes_negatives
test_record_field_assignment

# Bad
test1
test_thing
test_it_works
```

### Running Tests

```bash
luma --test <file.luma>
```

The exit code is non-zero if any test fails. This makes `--test` suitable for CI pipelines.

### Organising Test Files

- Place feature-specific test suites in `tests/features/language/` (core language features) or `tests/features/stdlib/` (standard library modules) with descriptive file names.
- Each test file is self-contained — no shared state between files.

```text
tests/
└── features/
    ├── array_functions.luma
    ├── arrays.luma
    ├── control_flow.luma
    ├── dictionaries.luma
    ├── enums.luma
    ├── functions.luma
    ├── interfaces.luma
    ├── lambdas.luma
    ├── match.luma
    ├── math_functions.luma
    ├── mutability.luma
    ├── operators.luma
    ├── records.luma
    ├── result_functions.luma
    ├── string_functions.luma
    ├── tuples.luma
    └── ...
```

---

## 22 — File Organisation

### Program Files

Place helper functions and type definitions before the `@main` function. The entry point should be the last declaration in the file, giving a top-down reading order.

```luma
# Type definitions
record Config {
    string host,
    integer port
}

# Helper functions
function Config load_config() {
    return Config { host = "localhost", port = 8080 }
}

function void start_server(Config cfg) {
    print("Starting on ${cfg.host}:${cfg.port}")
}

# Entry point — last
@main
function void main() {
    Config cfg = load_config()

    start_server(cfg)
}
```

### Test Files

Test files do not have a `@main` function. Place helper functions and type definitions at the top, followed by `@test` functions grouped by feature.

```luma
# ═══════════════════════════════════════════════════════════
# Luma v1.0 — Feature Tests — Arrays
# ═══════════════════════════════════════════════════════════

# ─── Creation ───

@test
function void test_array_creation() {
    array<integer> nums = [1, 2, 3]

    assert(Array.length(nums) == 3)
}

# ─── Transformation ───

@test
function void test_array_map() {
    result<array<integer>> doubled = Array.map([1, 2, 3], (integer x) -> x * 2)

    assert(doubled == success([2, 4, 6]))
}
```

### One Responsibility Per File

Each file should focus on one feature, one namespace, or one closely related group of types and functions. When a file grows to cover multiple unrelated concerns, split it into separate files with descriptive names.

```text
# Good — each file owns one concept
geometry.luma      # Geometry namespace and related types
validation.luma    # Input validation functions
user_model.luma    # User record and helpers

# Bad — catch-all file
utils.luma         # geometry + validation + formatting + …
```

### Include Files

When the project grows, use `include` to split code across files. Paths are resolved relative to the including file.

```luma
include "models.luma"
include "utils.luma"
```

Each file is included at most once automatically. Circular includes are a compile-time error.

---

## 23 — Anti-Patterns

### Ignoring Result Values

```luma
# Wrong — potential failure silently discarded
Array.get(arr, idx)

# Correct — handle the result
match Array.get(arr, idx) {
    success(v) { print(v) }
    failure(m) { print("error: ${m}") }
}
```

### Imperative Loops Instead of Library Functions

```luma
# Less idiomatic — manual loop with mutation
mutable integer total = 0

for x in nums {
    if x > 0 {
        total += x * 2
    }
}

# Idiomatic — functional pipeline
result<integer> total = nums
    !> Array.filter((integer x) -> x > 0)
    !> Array.map((integer x) -> x * 2)
    !> Array.sum()
```

### Nested Function Calls Instead of Pipes

```luma
# Hard to read — inside-out evaluation
string result = String.uppercase(String.trim(String.reverse(input)))

# Clear — left-to-right evaluation
string result = input
    |> String.reverse()
    |> String.trim()
    |> String.uppercase()
```

### Unnecessary Mutability

```luma
# Wrong — mutable variable never reassigned
mutable integer count = 10
print(count)

# Correct — immutable is sufficient
integer count = 10
print(count)
```

### Non-Exhaustive Match

```luma
# Wrong — missing choice variants
match direction {
    case Direction.North { print("north") }
    # South, East, West are unhandled
}

# Correct — all variants covered
match direction {
    case Direction.North { print("north") }
    case Direction.South { print("south") }
    case Direction.East  { print("east") }
    case Direction.West  { print("west") }
}
```

### Using `try`/`catch` for Expected Failures

```luma
# Wrong — catching an expected condition with try/catch
try {
    string content = Result.unwrap(FileSystem.read_file(path))
    process(content)
} catch(err) {
    print("file not found")
}

# Correct — use match or ?? for domain failures
match FileSystem.read_file(path) {
    success(content) { process(content) }
    failure(msg)     { print("could not read file: ${msg}") }
}
```

### Returning Sentinel Values Instead of `result<T>`

```luma
# Wrong — caller cannot distinguish "not found" from a legitimate empty string
function string find_user(integer id) {
    if id < 0 {
        return ""   # sentinel for "not found"
    }
    ...
}

# Correct — the type communicates the possibility of failure
function result<string> find_user(integer id) {
    if id < 0 {
        return failure("user not found")
    }
    ...
}
```

### Wrapping Infallible Functions in `result<T>`

```luma
# Wrong — forces callers to handle a failure that can never occur
function result<string> to_string(integer n) {
    return success(Converter.to_string(n))
}

# Correct — if the operation cannot fail, return the plain value
function string to_string(integer n) {
    return Converter.to_string(n)
}
```

### O(n²) Collection Building in Loops

```luma
# Wrong — each push copies the entire array
mutable array<integer> out = []

for x in data {
    out = Array.push(out, x * 2)
}

# Correct — single-pass transformation
result<array<integer>> out = Array.map(data, (integer x) -> x * 2)
```

---

## 24 — Security and Resource Limits

### Sandbox Mode

Use `--box` when running untrusted Luma files. Sandbox mode disables all OS-accessing modules (`Console`, `FileSystem`, `Process`, `Socket`, `Http`, `Csv`, `Xml`, `KeyValueStore`) and individual file-I/O functions in otherwise-safe modules (`Log.set_output`, `Compression.gzip_file`, `Compression.gunzip_file`, `Hash.sha256_file`, `Hash.sha512_file`).

```bash
luma --box untrusted_script.luma
```

### Resource Limits

The interpreter enforces several resource limits to prevent denial-of-service. The [resource-limit table](Luma_Performance_Guide.md#6--resource-limits) in the Performance Guide is the canonical list of values and their `LUMA_LIMIT_*` overrides; most violations raise a runtime error, while the socket and regex limits return a `failure`. Always `await` tasks promptly and close sockets when finished to stay within resource limits.

### Coding for Resource Limits

- **Call depth.** Prefer iterative algorithms over deeply recursive ones. If recursion is natural (tree traversal), ensure the data structure depth stays well below the limit. Use the `LUMA_LIMIT_MAX_CALL_DEPTH` environment variable to raise the limit when needed.
- **While loop iterations.** A `while` loop that exceeds the limit is terminated with a runtime error. If you need more iterations, restructure the algorithm or increase the limit via `LUMA_LIMIT_MAX_WHILE_ITERATIONS`.
- **Collection size.** Building a collection beyond the limit fails at runtime. Process large data sets in streaming fashion or in batches.
- **String size.** Avoid accumulating unbounded string data. Use file I/O or streaming when processing large text.

### HTTP Headers

When using `Http.get_with`, `Http.post_with`, or `Http.request` with custom headers, the interpreter rejects header names and values containing carriage-return or line-feed characters. This prevents CRLF header injection attacks.

---

## See Also

- [Tutorial](Luma_Tutorial.md) — a beginner's step-by-step introduction to the language
- [User Manual](Luma_User_Manual.md) — language syntax and semantics
- [Error Handling](Luma_Error_Handling.md) — error categories, `result` / `optional`, and conventions
- [Performance Guide](Luma_Performance_Guide.md) — runtime performance characteristics and optimisation advice
- [Software Architecture](Luma_Software_Architecture.md) — interpreter pipeline and module design
- [Standard Library Reference](Luma_Standard_Library_Reference.md) — the modules and functions these guidelines apply to
- [Concurrent Debugging Guide](Luma_Concurrent_Debugging_Guide.md) — debugging concurrent Luma programs
