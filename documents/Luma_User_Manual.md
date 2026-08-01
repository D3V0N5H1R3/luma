# Luma — User Manual

> A statically typed, expression-oriented language with first-class results, structural interfaces, built-in testing, and a pipe-first standard library.

---

## Table of Contents

1. [Getting Started](#1--getting-started)
2. [Types](#2--types)
3. [Variables and Mutability](#3--variables-and-mutability)
4. [Operators](#4--operators)
5. [Control Flow](#5--control-flow)
6. [Functions](#6--functions)
7. [Lambdas](#7--lambdas)
8. [Records](#8--records)
9. [Arrays](#9--arrays)
10. [Dictionaries](#10--dictionaries)
11. [Tuples](#11--tuples)
12. [Choice Types — Unit Variants](#12--choice-types--unit-variants)
13. [Choice Types (ADTs)](#13--choice-types-adts)
14. [Result and Optional](#14--result-and-optional)
15. [Match](#15--match)
16. [String Interpolation and Multi-Line Strings](#16--string-interpolation-and-multi-line-strings)
17. [Pipe Operator](#17--pipe-operator)
18. [Named Arguments](#18--named-arguments)
19. [Type Aliases](#19--type-aliases)
20. [Interfaces](#20--interfaces)
21. [Generics, `downcast`, and `is`](#21--generics-downcast-and-is)
22. [Namespaces and `use`](#22--namespaces-and-use)
23. [Ownership (`unique` and `borrow`)](#23--ownership-unique-and-borrow)
24. [Testing with `@test`](#24--testing-with-test)
25. [Including Files](#25--including-files)
26. [Standard Library Reference](#26--standard-library-reference)
27. [Linter and `--strict` Mode](#27--linter-and---strict-mode)
28. [Reserved Keywords](#28--reserved-keywords)
29. [Error Reference](#29--error-reference)
30. [Complete Programs](#30--complete-programs)
31. [Debugging](#31--debugging)
32. [Formal Grammar (EBNF)](#32--formal-grammar-ebnf)

- [See Also](#see-also)

---

## 1 — Getting Started

### Running

```bash
luma                          # interactive REPL
luma --repl, -r               # interactive REPL (explicit flag)
luma --eval, -e               # evaluate a program read from standard input (stdin)
luma <file.luma> [args...]    # run a source file (type checking runs automatically)
luma --test, -t <file.luma>   # run all @test functions and exit
luma --check, -c <file.luma>  # type-check only, do not run
luma --strict, -s <file.luma> # treat warnings as errors
luma --strict --check <file>  # strict type-check only
luma --box, -b <file.luma>    # run in sandbox mode (no I/O, network, or process access)
luma --box --test <file>      # run tests in sandbox mode
luma --optimize, -O <file>    # optimization level (0=none, 1=peephole, 2=full; default: 1)
luma --verify <file.luma>     # verify bytecode integrity before execution
luma pkg init                 # create a new luma.json manifest in the current directory
luma pkg help                 # print package manager usage
luma --version, -v            # print version
luma --help, -h               # print usage
```

Flags may appear in any order — `luma --test file.luma` and `luma file.luma --test` are equivalent. Arguments after the `.luma` file path are forwarded to the program. Mistyped flags produce a "did you mean" suggestion based on edit distance.

#### Exit Codes

| Code | Meaning                         |
| ---- | ------------------------------- |
| `0`  | Success                         |
| `1`  | Runtime error                   |
| `2`  | Type error                      |
| `3`  | Syntax error                    |
| `4`  | Compile error                   |
| `5`  | Usage error (invalid arguments) |

Type checking runs before every execution. If type errors are found, execution is aborted and the errors are printed. Use `--check` when you want to validate without running the program.

Sandbox mode (`--box`) disables all standard library modules that interact with the operating system — `Console`, `Csv`, `FileSystem`, `Http`, `KeyValueStore`, `Process`, `Socket`, and `Xml`. Within modules that remain available, individual file-I/O functions are also disabled: `Compression.gunzip_file`, `Compression.gzip_file`, `Hash.sha256_file`, `Hash.sha512_file`, and `Log.set_output`. Programs running in sandbox mode can only perform pure computation and terminal output via `print`. Attempting to call a sandboxed function produces a clear error: `'Module.function' is not available in sandbox mode (--box)`. Use sandbox mode when running untrusted `.luma` files.

Arguments after the file name are available via `Process.get_arguments()`.

### Bytecode Cache

The first time you run a program, Luma compiles it to bytecode and writes the result to a `.lumc` file next to the source — for example, running `app.luma` produces `app.lumc`. On the next run, Luma loads the cached bytecode and skips lexing, parsing, type checking, and compilation, giving near-instant startup for unchanged files.

The cache is transparent and safe to delete — it is regenerated automatically. A `.lumc` file is invalidated and recompiled whenever the source changes, the optimization level (`-O`) changes, or the interpreter's bytecode format version changes. Only running a program produces a `.lumc` file; `--check` and `--test` do not. If the file cannot be written (for example, in a read-only directory), Luma prints a warning and runs normally. These files are build artifacts and are ignored by Git.

### Hello, World

Every Luma program has exactly one `@main`-annotated function:

```luma
@main
function void main() {
    print("Hello, Luma!")
}
```

### Comments

```luma
# This is a line comment

number x = 42 # trailing comment
```

### Semicolons

Semicolons are optional in Luma. A newline ends a statement; a semicolon may be used instead or in addition but is never required:

```luma
number x = 1
number y = 2; number z = 3 # multiple statements on one line
```

### Interactive REPL

Start the REPL by running `luma` with no arguments. Variables and functions defined in one line persist for the session:

```text
luma> number x = 10
luma> x * x
=> 100
luma> :quit
```

REPL commands: `:quit` / `:q`, `:help` / `:h`, `:clear` / `:c`, `:file <path>` / `:f <path>`.

The `:file` command loads and executes a `.luma` file in the current REPL session. All declarations from the file become available for subsequent interactive use.

The REPL skips static type checking to allow incremental exploration. Type errors are caught at runtime instead of before execution.

Using `break` or `continue` outside a loop in the REPL prints a clear error message instead of crashing:

```text
luma> break
Error: 'break' used outside of a loop
luma>
```

---

## 2 — Types

### Primitive Types

| Type      | Description                         | Example literals                    |
| --------- | ----------------------------------- | ----------------------------------- |
| `boolean` | True or false                       | `true`, `false`                     |
| `integer` | 64-bit signed integer               | `0`, `42`, `-100`, `0xFF`, `0b1010` |
| `none`    | Absence of a value (empty optional) | `none`                              |
| `number`  | IEEE-754 64-bit float               | `0`, `3.14`, `-7`, `1e6`            |
| `string`  | UTF-8 text                          | `"hello"`, `""`                     |

### String Escape Sequences

The following escape sequences are recognised inside string literals and
interpolated strings:

| Sequence | Meaning                              |
| -------- | ------------------------------------ |
| `\"`     | Literal double quote                 |
| `\\`     | Literal backslash                    |
| `\$`     | Literal `$` (prevents interpolation) |
| `\0`     | Null character                       |
| `\n`     | Newline                              |
| `\r`     | Carriage return                      |
| `\t`     | Horizontal tab                       |

### String Indexing and Slicing

Strings support indexed access and range slicing using bracket notation:

```luma
string s = "hello"
string c = s[1]     # "e" — single byte character at index 1 (0-based, runtime error if out of bounds)
string t = s[1..4]  # "ell" — bytes at indices [1, 4) (exclusive end, clamped if out of range)
string u = s[1..=3] # "ell" — bytes at indices [1, 3] (inclusive end)
```

For Unicode-safe character access by codepoint index, use `String.character_at`. For Unicode-safe slicing, split with `String.characters` first.

`integer` and `number` are **distinct types**. Pure integer arithmetic never promotes to float: `7 / 2` yields `3`, not `3.5`. Mixing `integer` and `number` operands in the same expression produces a `number`.

> **When to use which:** Use `integer` for **indices and range bounds** — array subscripts, string offsets, loop counters that index into collections, and range end-points (`0..n`). Use `number` for **all other numeric values** — counts, sizes, quantities, measurements, scores, IDs, and mathematical computations. This keeps the intent clear: if a variable is typed `integer`, it addresses a position or delimits a range.

> **Floating-point precision:** `number` uses IEEE-754 64-bit representation, so some decimal values cannot be represented exactly. For example, `0.1 + 0.2 == 0.3` evaluates to `false`. When comparing `number` values for near-equality, use `Math.approximately_equal(a, b)` instead of `==`. For exact base-10 arithmetic — money in particular — use the `decimal` type and the `Decimal` module, where `0.1 + 0.2` is exactly `0.3`.

Integer literals may be written in **decimal**, **hexadecimal** (`0x` / `0X` prefix), or **binary** (`0b` / `0B` prefix):

```luma
integer a = 255        # decimal
integer b = 0xFF       # hexadecimal — same value
integer c = 0b11111111 # binary — same value
```

Hex and binary literals are especially useful with the bitwise operators:

Assigning an `integer` value to a `number` variable widens it automatically:

```luma
number x = 5     # ok — integer 5 widened to 5.0
number y = 3 + 1 # ok — integer expression widened
```

For explicit conversion use `Converter.to_number(i)` (returns `result<number>`). It also accepts a `string` — `Converter.to_number("3.14")` parses the string and returns `success(3.14)` or a failure result.

`none` is the literal representing the absence of a value. It is the empty case of the `optional<T>` generic type. `none` is only assignable to `optional<T>` variables — assigning it to a concrete type such as `integer` is a compile-time error.

Use `some(value)` to wrap a value in an optional, and `??` or `match` to safely unwrap it:

```luma
optional<integer> x = some(42)
optional<string>  s = none

integer val = x ?? 0         # 42
string  str = s ?? "default" # "default"
```

### Composite Types

| Type       | Declaration syntax     | Description                                              |
| ---------- | ---------------------- | -------------------------------------------------------- |
| Array      | `array<T>`             | Ordered, homogeneous sequence                            |
| Channel    | `channel<T>`           | Typed message-passing channel                            |
| Choice     | _TypeName_             | User-defined closed variant set (declared with `choice`) |
| Decimal    | `decimal`              | Exact base-10 decimal number (see the `Decimal` module)  |
| Dictionary | `dictionary<T>`        | String-keyed map (key is always `string`)                |
| Interface  | _TypeName_             | Structural field constraint (compile-time only)          |
| Lambda     | `function(T,...) -> T` | First-class function value                               |
| Optional   | `optional<T>`          | A value that may or may not be present                   |
| Queue      | `queue<T>`             | First-in, first-out collection                           |
| Record     | _TypeName_             | User-defined named-field struct                          |
| Result     | `result<T>`            | Value or error string                                    |
| Socket     | `socket`               | Network socket handle                                    |
| Stack      | `stack<T>`             | Last-in, first-out collection                            |
| Task       | `task<T>`              | Concurrent asynchronous computation                      |
| Tuple      | `(T, T, ...)`          | Fixed-size mixed-type value, 2–4 elements                |

---

## 3 — Variables and Mutability

### Immutable (Default)

```luma
boolean flag  = true
integer count = 10
string  name  = "Alice"
```

Attempting to reassign an immutable variable is a compile-time error.

### Mutable

Prefix with `mutable` to allow reassignment and compound assignment:

```luma
mutable number total = 0

total = total + 3.14
total += 1.0
total -= 0.5
total *= 2.0
total /= 4.0
total %= 3.0
```

Integer division compound assignment:

```luma
mutable integer x = 17
x //= 5 # x == 3
```

`+=` also works on `string` variables to concatenate:

```luma
mutable string message = "hello"

message += ", world"

print(message) # hello, world
```

### Increment and Decrement

```luma
mutable integer i = 0

i++ # i = 1
i-- # i = 0
```

Only valid on mutable `integer` and `number` variables.

### Tuple Destructuring

```luma
(number x, number y) = get_coords()

mutable (string first, string last) = split_name(full_name)
```

`mutable` applies to **all** bindings in the destructuring — individual elements cannot have mixed mutability.

### Record Destructuring

A record can be destructured by field name, binding each listed field to a
same-named local. Field types are inferred from the record definition:

```luma
record Point { number x, number y, number z }

Point p = Point { x = 1.0, y = 2.0, z = 3.0 }

Point { x, y } = p          # binds x and y; z is ignored
print(x)                    # 1.0
print(y)                    # 2.0
```

A **subset** of fields may be listed — unlisted fields are simply not bound.
Duplicate field names, unknown fields, and a right-hand value that is not an
instance of the record type are compile-time errors. As with tuples, `mutable`
applies to **all** bindings:

```luma
mutable Point { x, y, z } = p
x = x + 10.0
```

Record destructuring reads plain data — it is distinct from record _creation_,
which assigns each field (`Point { x = 1.0, ... }`). Records may also be
destructured as a `match` case pattern (see [§8 — Records](#8--records)).

### Scope and Shadowing

Declaring a variable that is already defined in the **same** scope is a runtime error. However, an inner block (e.g. inside `if`, `for`, or `{}`) creates a new scope, so inner declarations safely shadow outer ones:

```luma
integer x = 1

{
    integer x = 2 # ok — inner scope shadows outer x

    print(x)      # 2
}

print(x)          # 1 — outer x unchanged
```

### Loop Variables

The loop variable in a `for` loop is always immutable, regardless of whether the surrounding scope is mutable:

```luma
for item in [1, 2, 3] {
    # item is immutable — assigning to it is a runtime error
    print(item)
}
```

---

## 4 — Operators

### Arithmetic

| Operator | Description                      | Notes                                                |
| -------- | -------------------------------- | ---------------------------------------------------- |
| `+`      | Addition or string concatenation | `string + string` concatenates                       |
| `-`      | Subtraction or unary negation    | `-x` negates `integer` or `number`                   |
| `*`      | Multiplication or string repeat  | `"ha" * 3` → `"hahaha"`                              |
| `/`      | Division                         | Truncates toward zero for `integer` operands         |
| `//`     | Integer division                 | Both operands must be `integer`; result is `integer` |
| `%`      | Modulo                           |                                                      |

Integer division: `7 / 2 == 3`. Mixed `integer` and `number` expressions produce `number`.

The `//` operator always returns an `integer` and requires both operands to be `integer`. It truncates toward zero, matching the behaviour of `/` on integers:

```luma
10 // 3 # 3
-7 // 2 # -3
```

Use `//` when you explicitly need an integer quotient and want to document that intent.

### Bit Manipulation

Luma has no bitwise operators. Bit manipulation lives in the **`Bits`** standard-library
module — pipe-first free functions over `integer` values:

| Function                     | Description              | Example                      |
| ---------------------------- | ------------------------ | ---------------------------- |
| `Bits.and(a, b)`             | Bitwise AND              | `Bits.and(12, 10)` → `8`     |
| `Bits.or(a, b)`              | Bitwise OR               | `Bits.or(8, 4)` → `12`       |
| `Bits.xor(a, b)`             | Bitwise XOR              | `Bits.xor(15, 9)` → `6`      |
| `Bits.not(a)`                | Bitwise NOT              | `Bits.not(0)` → `-1`         |
| `Bits.shift_left(v, n)`      | Left shift               | `Bits.shift_left(1, 3)` → `8`  |
| `Bits.shift_right(v, n)`     | Right shift (arithmetic) | `Bits.shift_right(16, 2)` → `4` |

Shift amounts must be in the range `0..63`; a shift amount outside this range throws a
`RuntimeError`. `Bits.not(x)` is the two's-complement 64-bit complement, so
`Bits.not(0) == -1` and `Bits.not(-1) == 0`.

### Comparison

| Operator          | Description                            |
| ----------------- | -------------------------------------- |
| `==`              | Equal — works for all comparable types |
| `!=`              | Not equal                              |
| `<` `>` `<=` `>=` | Ordered comparison                     |

### Membership and Containment

| Operator | Description                                      |
| -------- | ------------------------------------------------ |
| `in`     | Element in array: `42 in [1, 42, 3]` → `true`    |
| `in`     | Key in dictionary: `"alice" in scores` → `true`  |
| `in`     | Substring in string: `"ell" in "hello"` → `true` |
| `in`     | Integer in range: `42 in 1..=100` → `true`       |

Range membership tests whether an `integer` falls within a range's bounds. It
honours the range form: the lower bound is always inclusive, while the upper
bound is inclusive for `a..=b` and exclusive for `a..b`. It reads more naturally
than the two-comparison idiom it replaces:

```luma
# Instead of: score >= 1 && score <= 100
boolean valid = score in 1..=100
```

The left operand must be an `integer` (ranges have integer bounds); any other
type is a compile-time `TypeError`.

### Logical

| Operator | Description                  |
| -------- | ---------------------------- |
| `&&`     | Short-circuit AND            |
| `\       | \                            |
| `!`      | Logical NOT (prefix)         |
| `??`     | Optional / Result unwrapping |

The `??` operator unwraps an `optional<T>` or `result<T>` value. If the left operand is `some(value)` or `success(value)`, it returns the inner value. If it is `none` or `failure(...)`, it returns the right operand:

```luma
optional<string> name = none
string display = name ?? "anonymous" # "anonymous"

optional<integer> value = some(42)
integer safe  = value ?? 0           # 42

result<string> content = failure("not found")
string text = content ?? ""          # ""

result<integer> parsed = success(42)
integer num = parsed ?? 0            # 42
```

### Operator Precedence

| Precedence  | Operators                              | Description                                |
| ----------- | -------------------------------------- | ------------------------------------------ |
| 1 (highest) | `()` `[]` `.` `?.` `?[` `..` `..=` `?` | Call, subscript, field, range, propagation |
| 2           | `!` `-` (prefix)                       | Unary NOT, negate                          |
| 3           | `*` `/` `//` `%`                       | Multiplicative                             |
| 4           | `+` `-`                                | Additive                                   |
| 5           | `<` `>` `<=` `>=` `in`                 | Comparison                                 |
| 6           | `==` `!=`                              | Equality                                   |
| 7           | `&&`                                   | Logical AND                                |
| 8           | `\                                     | \                                          |
| 9           | `??`                                   | Optional / Result unwrapping               |
| 10 (lowest) | `\                                     | >` `!>`                                    |

### Optional Chaining `?.` and `?[`

The `?.` operator accesses a field on a value that may be `none`. The result type is `optional<T>` where `T` is the field type. If the left-hand side is `none`, the entire expression evaluates to `none` without throwing a runtime error. If the left-hand side holds a value, it behaves identically to `.`.

```luma
record User { string name, string email }

optional<User> maybe_user = none
string name = maybe_user?.name ?? "unknown" # "unknown" — none case handled

optional<User> u = some(User { name = "Alice", email = "alice@example.com" })
string name2 = u?.name ?? "" # "Alice"
```

The `?[` operator applies the same short-circuit to index access. The result type is `optional<T>` where `T` is the element type.

```luma
optional<array<integer>> maybe_arr = none
integer v = maybe_arr?[0] ?? 0 # 0 — none case handled

optional<array<integer>> arr = some([10, 20, 30])
integer v2 = arr?[1] ?? 0 # 20
```

When a field is itself `optional<T>`, chaining auto-flattens — `x?.field` produces `optional<T>` rather than `optional<optional<T>>`:

```luma
record Inner { string value }
record Outer { optional<Inner> child }

optional<Outer> o = some(Outer { child = some(Inner { value = "hi" }) })
optional<Inner> c = o?.child # optional<Inner>, not optional<optional<Inner>>
```

Because `?.` and `?[` return `optional<T>`, always unwrap the result with `??` or a `match` expression before using the value.

### Propagation with `?`

The postfix `?` operator unwraps a `result<T>` or `optional<T>` value. If the value is `success(v)` or `some(v)`, it returns the inner value. If the value is `failure(...)` or `none`, it immediately returns from the enclosing function with that failure or `none`:

```luma
# ? on result<T> — enclosing function must return result<T>
function result<number> parse_and_double(string s) {
    number n = Converter.to_number(s)? # propagates failure if conversion fails

    return success(n * 2)
}

# ? on optional<T> — enclosing function must return optional<T>
function optional<string> get_display_name(optional<User> user) {
    User u = user?                          # returns none if user is none
    optional<string> nick = find_nickname(u.id)?

    return some(nick)
}
```

> `?` **can be used inside `@main`**. If `?` propagates a failure, the program terminates with a `RuntimeError` whose message includes the failure value. The typed error payload is preserved, so a surrounding `try`/`catch` can recover it. You can also use `??` to provide a default instead:
>
> ```luma
> @main
> function void main() {
>     integer n = fallible()?     # terminates with RuntimeError on failure
>     integer m = fallible() ?? 0 # provides a default instead
> }
> ```

> `?` **requires the enclosing function to return** `result<T>`, `optional<T>`, or to be inside `@main`. Using `?` in a function that returns a plain type (e.g. `integer`) is a type error.

### Truthiness

The type checker requires `boolean` for conditions in `if`, `while`, and `assert`. For reference, the following values are considered **falsy** at runtime; all others are **truthy**:

| Value   | Type          |
| ------- | ------------- |
| `""`    | `string`      |
| `0.0`   | `number`      |
| `0`     | `integer`     |
| `false` | `boolean`     |
| `none`  | `optional<T>` |

### Range

```luma
0..10  # integer range [0, 10) — exclusive end — for use in for-loops and subscripts
0..=10 # integer range [0, 10] — inclusive end
```

The `..` operator produces a range value where the end is **exclusive** (`0..10` yields 0–9). The `..=` operator produces a range value where the end is **inclusive** (`0..=10` yields 0–10). These ranges are usable in `for` loops and as array/string subscripts. They are **not** array literals — use `[1, 2, 3]` syntax for array values.

### Pipe

```luma
expr |> func         # func(expr)
expr |> Ns.func(arg) # Ns.func(expr, arg)
```

See [Section 17](#17--pipe-operator) for full details including `!>`.

---

## 5 — Control Flow

### If / Else If / Else

Braces are always required:

```luma
if score >= 90 {
    print("A")
} else if score >= 80 {
    print("B")
} else {
    print("F")
}
```

### If as an Expression

```luma
string label = if score >= 60 { "pass" } else { "fail" }
```

Both branches must produce the same type. `else if` chaining is supported in the expression form — the final branch must be a plain `else`.

### For … in (Range)

```luma
for i in 0..10 {
    print(i) # prints 0, 1, ..., 9 (exclusive end)
}

for i in 0..=10 {
    print(i) # prints 0, 1, ..., 10 (inclusive end)
}
```

The `a..b` range is **exclusive** on the right: it includes `a` but not `b`. The `a..=b` range is **inclusive**: it includes both `a` and `b`.

### For … in (Array)

`for` loops iterate over arrays, ranges, and dictionaries.

```luma
array<string> fruits = ["apple", "banana", "cherry"]

for fruit in fruits {
    print(fruit)
}
```

With index:

```luma
for i, fruit in fruits {
    print("${i}: ${fruit}")
}
```

### For … in (String)

Iterating over a string yields each Unicode character (codepoint) as a single-character `string`:

```luma
for ch in "hello" {
    print(ch) # h, e, l, l, o
}
```

This is equivalent to iterating over `String.characters(s)` but without allocating a temporary array.

### For … in (Dictionary)

Two-variable form iterates key–value pairs:

```luma
dictionary<integer> scores = {"alice": 95, "bob": 87}

for name, score in scores {
    print("${name}: ${score}")
}
```

The first variable receives the key (`string`), the second receives the value. A single-variable `for` over a dictionary is a **type error** — use `Dictionary.keys(d)` or `Dictionary.values(d)` to iterate keys or values only.

### Discarding the Loop Variable

Use `_` as a conventional discard identifier when the loop variable is not needed. It is a normal identifier that happens to be unused:

```luma
mutable integer count = 0

for _ in 0..10 {
    count++
}

print(count) # 10
```

### Break and Continue

```luma
for i in 0..100 {
    if i % 2 == 0 { continue }
    if i > 9      { break }

    print(i) # 1, 3, 5, 7, 9
}
```

### While

`while` repeats its body as long as the condition is truthy:

```luma
mutable integer n = 1

while n < 128 {
    n *= 2
}

print(n) # 128
```

`break` and `continue` work inside `while` the same way as in `for`.

### Try / Catch / Finally

`try` executes a block. If a runtime error is raised:

- `catch(var)` runs the catch block and binds the error message to `var`.
- `finally` always runs, whether or not an error occurred.

All three clauses are optional, but at least `catch` or `finally` must be present:

```luma
try {
    integer result = 10 / 0
} catch(err) {
    print("caught: ${err}") # caught: division by zero
} finally {
    print("always runs")
}
```

`finally` without `catch`:

```luma
try {
    print("try")
} finally {
    print("cleanup")
}
```

When `try` has no `catch` block, any error raised inside `try` is still propagated to the nearest enclosing `try`/`catch` after `finally` finishes. `finally` does not suppress the error — it only guarantees the cleanup code runs first:

```luma
try {
    try {
        integer _ = 1 / 0
    } finally {
        print("inner cleanup runs first")
    }
} catch(err) {
    print("caught: ${err}") # caught: division by zero
}
```

The error variable in `catch(var)` is an immutable `string` holding the error message. Execution resumes after the `try`/`catch`/`finally` block once the catch body finishes — the error is fully recovered.

### What `catch` Handles

`catch` intercepts **runtime errors** only. These are errors raised during program execution, for example:

- Division by zero or integer overflow
- Out-of-bounds array or string access
- `assert` failures
- Standard library errors (file not found, socket failure, etc.)

`catch` does **not** intercept:

- **Syntax errors** — rejected by the parser before execution begins
- **Type errors** — rejected by the type checker before execution begins

These two categories cause the interpreter to abort before any code runs, so no `try` block can observe them.

> **Design rule — `try`/`catch` vs `result<T>`.** Use `result<T>` for **recoverable domain failures** — conditions the caller is expected to handle, such as a file not found, a network timeout, or invalid user input. Use `try`/`catch` for **programmer errors** — conditions that should never occur in a correct program, such as an out-of-bounds access, a failed assertion, or an unexpected `none`. Mixing the two styles makes it harder to reason about which errors are expected. In particular, avoid using `try`/`catch` to swallow `Result.unwrap` failures — if a result may legitimately be `failure`, pattern-match on it with `match`, use `??`, or use `Result.unwrap_or` instead.

### Error Propagation Inside `catch`

If an error is raised inside the `catch` block itself, `finally` still runs, but the new error is **not** caught — it propagates up to the nearest enclosing `try`, or terminates the program if there is none.

---

## 6 — Functions

### Declaration

```luma
function number add(number a, number b) {
    return a + b
}
```

The return type precedes the function name. Every function must declare a return type — use `void` for functions that return nothing.

> **Missing-return error** — when a function declares a concrete return type the type checker verifies that every code path reaches a `return` statement. If any path can fall through without returning, a **TypeError** is raised and execution is aborted:
>
> ```luma
> function string greet(boolean flag) {
>     if flag {
>         return "yes"
>     }
>
>     # TypeError: function 'greet' may fall through without returning a value
> }
> ```

Parameters are **immutable** by default. Prefix with `mutable` to allow reassignment inside the function body.

```luma
function void increment(mutable integer counter) {
    counter = counter + 1
}
```

### The `@main` Annotation

Exactly one function per program should carry `@main` — it is the entry point. Only `@main` and `@test` are valid annotation names; any other annotation is a **syntax error**.

```luma
@main
function void main() {
    print("Hello!")
}
```

Top-level statements (written outside any function) execute before `@main` is called. This is useful for module-level constants, but avoid side effects there.

If no `@main` function is present, running the file or checking it with `--check` is a **TypeError**. Files run with `--test` are exempt — they do not need a `@main` function. Defining more than one `@main` is also an error — a `TypeError` when running with `--check`, and a `RuntimeError` at execution otherwise.

### Optional Parameters

Parameters with `=` default values must follow required ones. Default values are evaluated in the function's **definition scope**, not the caller's scope:

```luma
function string greet(string name, string prefix = "Hello") {
    return prefix + ", " + name + "!"
}

print(greet("Alice"))     # Hello, Alice!
print(greet("Bob", "Hi")) # Hi, Bob!
```

Functions can also be called with **named arguments** — see [Section 18](#18--named-arguments) for details:

```luma
print(greet(prefix: "Hi", name: "Carol")) # Hi, Carol!
```

### Recursion

```luma
function integer factorial(integer n) {
    if n <= 1 { return 1 }

    return n * factorial(n - 1)
}
```

The maximum recursion depth is bounded; exceeding it is a runtime error. See the [resource-limit table](Luma_Performance_Guide.md#6--resource-limits) (`LUMA_LIMIT_MAX_CALL_DEPTH`) for the default and how to change it.

### Tail Call Optimization

When a `return` statement directly returns the result of a function call (and the call is not inside a `try` block), the compiler automatically applies **tail call optimization**. This reuses the current call frame instead of pushing a new one, allowing self-recursive functions to run in constant stack space without hitting the recursion limit:

```luma
function integer factorial_acc(integer n, integer acc) {
    if n <= 1 { return acc }

    return factorial_acc(n - 1, n * acc) # tail call — no stack growth
}
```

Tail call optimization applies to any direct function call in tail position, not just self-recursion. Named arguments disable the optimization.

### Namespace-Qualified Functions

```luma
namespace Geometry {
    record Point { number x, number y }

    function number area(number r) {
        return Math.pi * r * r
    }
}

number a = Geometry.area(5)

# Records, enums, and type aliases are also accessible via qualified names.
Geometry.Point p = Geometry.Point { x = 1, y = 2 }
```

---

## 7 — Lambdas

Lambdas are anonymous functions passed as values.

### Inline Syntax

```luma
(number x) -> x * 2
(number a, number b) -> a + b
() -> 42
```

Multi-statement bodies use a block:

```luma
(number x) -> {
    number y = x * x

    return y + 1
}
```

### Passing to Higher-Order Functions

```luma
array<number> nums    = [1, 2, 3, 4, 5]
array<number> doubled = Result.unwrap(Array.map(nums, (number x) -> x * 2)) # [2, 4, 6, 8, 10]
```

### Storing and Calling Lambdas

Lambda values can be stored in variables and called like named functions:

```luma
function(number) -> boolean is_positive = (number x) -> x > 0
print(is_positive(5))  # true
print(is_positive(-3)) # false

function() -> number forty_two = () -> 42
print(forty_two()) # 42

function(number, number) -> number add = (number a, number b) -> a + b
print(add(3, 4)) # 7
```

### Captures

Lambdas capture surrounding variables **by value** at creation time:

```luma
number threshold = 60

array<number> passing = Array.filter(scores, (number s) -> s >= threshold) # threshold = 60 is captured when the lambda is created
```

### Limitations

Inline lambdas (`(x) -> expr`) do not support default parameter values or named arguments at call sites — only positional arguments are accepted.

---

## 8 — Records

### Declaration

Fields are separated by commas. A trailing comma before the closing `}` is
allowed but not required.

```luma
record Point {
    number x,
    number y
}
```

### Default Field Values

Fields may be given a default expression. When a default is provided, the field may be omitted from a record literal and the default is used:

```luma
record Config {
    string  host   = "localhost",
    integer port   = 8080,
    boolean secure = false
}

Config default = Config {}                            # all defaults
Config custom  = Config { port = 443, secure = true } # host uses default
```

Fields **without** a default remain required. If a required field is omitted, the type checker reports a missing-field error. The type of the default expression must match the declared field type.

### Creating Instances

```luma
Point origin = Point { x = 0, y = 0 }
Point p      = Point { x = 3.5, y = -1.2 }
```

All fields must be provided and fields may be given in any order. Missing or extra fields are caught by the type checker before execution.

### Reading Fields

```luma
print(p.x) # 3.5
print(p.y) # -1.2
print(p)   # Point { x = 3.5, y = -1.2 }
```

### Destructuring Fields

Instead of repeating `p.x`, `p.y`, `p.z`, a record can be destructured to bind
several fields to same-named locals at once. Field types are inferred from the
record definition, and a subset of fields may be listed:

```luma
Point { x, y } = p       # binds x and y
mutable Point { x, y } = p # all bindings are mutable
```

Records can also be destructured as a `match` case pattern, binding the named
fields inside the arm. Because a record has a single shape, an unguarded record
pattern (or an `else` arm) makes the match exhaustive:

```luma
record Shape { number width, number height }

number area = match s {
    case Shape { width, height } { width * height }
}
```

Guards combine with record patterns, in which case an `else` arm is required:

```luma
string kind = match s {
    case Shape { width, height } when width == height { "square" }
    else { "rectangle" }
}
```

### Equality

Record equality with `==` is **structural** — two records are equal when they have the same type name and all fields compare equal:

```luma
Point a = Point { x = 1, y = 2 }
Point b = Point { x = 1, y = 2 }
Point c = Point { x = 9, y = 2 }

print(a == b) # true
print(a == c) # false
print(a != c) # true
```

Field values are compared recursively, so nested records are also compared structurally.

### Value Semantics

Records are **copied by value** when passed to functions or assigned to variables. Mutating a field on a copy does not affect the original:

```luma
function Point zero_x(mutable Point p) {
    p.x = 0

    return p
}

Point original = Point { x = 5, y = 3 }
Point modified = zero_x(original)

print(original.x) # 5 — unchanged
print(modified.x) # 0
```

### Updating Fields

Record variables must be `mutable`:

```luma
mutable Point p = Point { x = 0, y = 0 }
p.x = 5
p.y = 10
```

### Copying with Field Overrides

Use `with` to create a modified copy without mutating the original:

```luma
Point p1 = Point { x = 1, y = 2 }
Point p2 = p1 with { x = 5 } # p2 is { x = 5, y = 2 }; p1 is unchanged
```

Multiple fields can be overridden in a single `with` expression:

```luma
Point p3 = p1 with { x = 10, y = 20 }
```

`with` is a compile-time error if an override field does not exist in the record type. The type of the result is the same as the base record type.

### Field Assignment on Array Elements

```luma
mutable array<Point> pts = [Point { x = 0, y = 0 }, Point { x = 1, y = 1 }]
pts[0].x = 99
```

### Using Records in Functions

```luma
function number distance(Point a, Point b) {
    number dx = a.x - b.x
    number dy = a.y - b.y

    result<number> d = Math.square_root(dx*dx + dy*dy)

    return Result.unwrap(d)
}
```

---

## 9 — Arrays

### Creating Arrays

```luma
array<number>  nums  = [1, 2, 3, 4, 5]
array<string>  words = ["hello", "world"]
array<boolean> flags = [true, false, true]
array<number>  empty = []
```

### Accessing Elements (Zero-Based)

```luma
print(nums[0]) # 1
print(nums[4]) # 5
```

Out-of-bounds access is a runtime error. For safe access use `Array.get`.

### Safe Access

```luma
result<number> r = Array.get(nums, 10) # failure("index 10 out of bounds")
result<number> v = Array.get(nums, 2)  # success(3)
```

### Mutating Elements

```luma
mutable array<number> list = [1, 2, 3]
list[1] = 99 # [1, 99, 3]
```

### Common `Array` Functions

| Function                      | Description                                                                                                                 |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `Array.all(arr, fn)`          | `result<boolean>` — `true` if all elements match; `failure` if predicate throws                                             |
| `Array.any(arr, fn)`          | `result<boolean>` — `true` if any element matches; `failure` if predicate throws                                            |
| `Array.chunk(arr, n)`         | `result<array<array<T>>>` — split into sub-arrays of size `n`; `failure` if `n` is not > 0                                  |
| `Array.contains(arr, v)`      | `boolean` — `true` if `v` is present in the array                                                                           |
| `Array.count(arr, fn)`        | `result<integer>` — number of matching elements; `failure` if predicate throws                                              |
| `Array.drop_while(arr, fn)`   | `result<array<T>>` — drop elements while predicate is true; `failure` if predicate throws                                   |
| `Array.each(arr, fn)`         | `result<none>` — iterate for side effects; `failure` if callback throws                                                     |
| `Array.enumerate(arr)`        | Array of `(index, value)` tuples                                                                                            |
| `Array.filter(arr, fn)`       | `result<array<T>>` — keep elements where `fn` returns `true`; `failure` if predicate throws                                 |
| `Array.find(arr, fn)`         | `result<T>` — first matching element                                                                                        |
| `Array.find_index(arr, fn)`   | `result<integer>` — index of first matching element                                                                         |
| `Array.first(arr)`            | `result<T>` — first element                                                                                                 |
| `Array.flat_map(arr, fn)`     | `result<array<T>>` — map then flatten one level; `failure` if callback throws                                               |
| `Array.flatten(arr)`          | `array<T>` — flatten one nesting level                                                                                      |
| `Array.get(arr, i)`           | `result<T>` — safe indexed access                                                                                           |
| `Array.group_by(arr, fn)`     | `result<dictionary<array<T>>>` — group elements by key returned by `fn`; `failure` if callback throws                       |
| `Array.index_of(arr, v)`      | `result<integer>` — first index, `failure` if not found                                                                     |
| `Array.is_empty(arr)`         | `true` if the array has no elements                                                                                         |
| `Array.join(arr, sep)`        | Concatenate elements as strings separated by `sep`                                                                          |
| `Array.last(arr)`             | `result<T>` — last element                                                                                                  |
| `Array.length(arr)`           | `integer` — number of elements                                                                                              |
| `Array.map(arr, fn)`          | `result<array<T>>` — transform each element; `failure` if callback throws                                                   |
| `Array.max(arr)`              | `result<T>` — maximum value; `failure` if array is empty                                                                    |
| `Array.min(arr)`              | `result<T>` — minimum value; `failure` if array is empty                                                                    |
| `Array.pop(arr)`              | `result<(array, T)>` — `success((array, last))` or `failure` if empty                                                       |
| `Array.push(arr, v)`          | `array<T>` — new array with `v` appended                                                                                    |
| `Array.range(start, end)`     | `result<array<integer>>` — generate `[start, start+1, ..., end-1]`; `failure` if range exceeds size limit                   |
| `Array.reduce(arr, init, fn)` | `result<T>` — fold left with `fn(accumulator, element)`; `failure` if third argument is not callable                        |
| `Array.repeat(v, n)`          | `result<array<T>>` — `n` copies of `v`; `failure` if `n` is negative or exceeds size limit                                  |
| `Array.reverse(arr)`          | Return reversed copy                                                                                                        |
| `Array.slice(arr, from, to)`  | `result<array<T>>` — `success` with subarray `[from, to)`; `failure` if `from` or `to` is negative, or `from > to`          |
| `Array.sort(arr, fn)`         | `result<array<T>>` — sort by comparator; `fn(a, b)` must return negative, zero, or positive; `failure` if comparator throws |
| `Array.sort_by(arr, fn)`      | `result<array<T>>` — sort by key; `fn(elem)` returns a string or number key; `failure` if key function throws               |
| `Array.sum(arr)`              | `result<integer \| number>` — sum numeric elements; `failure` if array contains non-numeric element                         |
| `Array.take_while(arr, fn)`   | `result<array<T>>` — take elements while predicate is true; `failure` if predicate throws                                   |
| `Array.unique(arr)`           | Deduplicate                                                                                                                 |
| `Array.zip(a, b)`             | Pair elements into tuples; truncates to the shorter array                                                                   |
| `Array.concat(a, b)`          | `array<T>` — concatenate two arrays                                                                                         |
| `Array.intersperse(arr, sep)` | `array<T>` — insert `sep` between each pair of elements                                                                     |
| `Array.partition(arr, fn)`    | `result<(array<T>, array<T>)>` — split into `(matches, rest)` by predicate; `failure` if predicate throws                   |
| `Array.rotate(arr, n)`        | `array<T>` — rotate elements left by `n` positions (negative rotates right)                                                 |
| `Array.scan(arr, init, fn)`   | `result<array<T>>` — like reduce but returns all intermediate accumulator values; `failure` if callback throws              |
| `Array.transpose(arr)`        | `result<array<array<T>>>` — transpose a 2D array; `failure` if rows have unequal length                                     |
| `Array.windows(arr, n)`       | `result<array<array<T>>>` — overlapping sliding windows of size `n`; `failure` if `n` is not > 0                            |

> **Callback exception handling.** Higher-order functions that accept callbacks — such as `Array.map`, `Array.filter`, `Array.sort`, `Array.sort_by`, `Array.take_while`, `Array.drop_while`, `Array.flat_map`, `Array.reduce`, `Array.each`, `Array.find`, `Array.find_index`, `Array.all`, `Array.any`, `Array.count`, `Array.group_by`, `Array.scan`, and `Array.partition` — automatically convert any runtime exception thrown inside the callback into a `failure` result. The original exception message becomes the failure's error string. This means that out-of-bounds errors, division by zero, or any other runtime error inside a callback will not crash the program — instead the enclosing function returns `failure("…")`. To observe the original exception, inspect the failure with `Result.error()` or `match`.

### Pipeline Example

```luma
array<number> result = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    |> Array.filter((number x) -> x % 2 == 0)
    |> Result.unwrap_or([])
    |> Array.map((number x) -> x * x)
    |> Result.unwrap_or([])

print(result) # [4, 16, 36, 64, 100]
```

---

## 10 — Dictionaries

All keys are strings. Entries are stored and iterated in insertion order.

### Creating Dictionaries

```luma
dictionary<number>  scores = {"alice": 95, "bob": 87}
dictionary<boolean> flags  = {"debug": true, "verbose": false}
dictionary<string>  empty  = {}
```

### Reading Values

```luma
result<number> v = Dictionary.get(scores, "alice") # success(95)
result<number> m = Dictionary.get(scores, "dave")  # failure(...)

number n = Dictionary.get_or(scores, "alice", 0) # 95
number d = Dictionary.get_or(scores, "dave",  0) # 0
```

### Modifying Dictionaries

Dictionary operations return new dictionaries — the original is not mutated:

```luma
dictionary<number> updated = Dictionary.set(scores, "dave", 88)
dictionary<number> removed = Dictionary.remove(scores, "bob")
```

### Common `Dictionary` Functions

| Function                          | Description                                                                                        |
| --------------------------------- | -------------------------------------------------------------------------------------------------- |
| `Dictionary.has_value(d, v)`      | `true` if any value equals `v`                                                                     |
| `Dictionary.count(d, fn)`         | `integer` — count entries matching predicate                                                       |
| `Dictionary.deep_merge(a, b)`     | Recursive merge; `b` wins on conflicts                                                             |
| `Dictionary.each(d, fn)`          | `result<none>` — iterate key-value pairs; `failure` if callback throws                             |
| `Dictionary.filter(d, fn)`        | `result<dictionary<T>>` — keep entries where `fn` returns `true`; `failure` if callback throws     |
| `Dictionary.find(d, fn)`          | `result<(string, T)>` — first entry matching predicate; `failure` if not found                     |
| `Dictionary.from_entries(arr)`    | Create from array of `(key, value)` tuples                                                         |
| `Dictionary.from_keys(keys, def)` | Create from key list with default                                                                  |
| `Dictionary.get(d, k)`            | `result<T>` — safe lookup                                                                          |
| `Dictionary.get_or(d, k, def)`    | Lookup with default                                                                                |
| `Dictionary.has(d, k)`            | Key membership test                                                                                |
| `Dictionary.invert(d)`            | Swap keys and values                                                                               |
| `Dictionary.is_empty(d)`          | True if empty                                                                                      |
| `Dictionary.keys(d)`              | `array<string>` of keys                                                                            |
| `Dictionary.length(d)`            | Number of entries                                                                                  |
| `Dictionary.map(d, fn)`           | `result<dictionary<T>>` — transform every entry via `fn(key, value)`; `failure` if callback throws |
| `Dictionary.map_values(d, fn)`    | `result<dictionary<T>>` — transform every value; `failure` if callback throws                      |
| `Dictionary.merge(a, b)`          | Merge; `b` wins on conflicts                                                                       |
| `Dictionary.omit(d, keys)`        | New dictionary excluding entries whose keys are in `keys`                                          |
| `Dictionary.pick(d, keys)`        | New dictionary containing only entries whose keys are in `keys`                                    |
| `Dictionary.reduce(d, init, fn)`  | `result<T>` — fold entries via `fn(acc, key, value)`; `failure` if callback throws                 |
| `Dictionary.remove(d, k)`         | Return new dictionary without key                                                                  |
| `Dictionary.set(d, k, v)`         | Return new dictionary with key set                                                                 |
| `Dictionary.to_array(d)`          | `array<KeyValue>` — each element is a record with `.key` (`string`) and `.value` fields            |
| `Dictionary.to_entries(d)`        | `array<(string, T)>` — each element is a `(key, value)` tuple                                      |
| `Dictionary.values(d)`            | `array<T>` of values                                                                               |

---

## 11 — Tuples

Tuples hold 2–4 values of mixed types and are immutable. The 2–4 element constraint is enforced by the static type checker before execution.

### Creating and Accessing

Tuple literals use comma-separated values inside parentheses:

```luma
(integer, string) pair  = (1, "hello")
(number, number)  point = (1.0, 2.0)

print(pair.0) # 1
print(pair.1) # "hello"
```

When accessing a tuple element with a **compile-time integer literal** index, the type checker infers the exact declared element type — no downcast needed:

```luma
(integer, string) t = (42, "hi")

integer n = t[0] # type: integer
string  s = t[1] # type: string
```

### Destructuring

```luma
(number x, number y) = get_coords()

print("x=${x}, y=${y}")
```

Use `mutable` to make all bindings mutable (there is no way to make only some bindings mutable):

```luma
mutable (number x, number y) = get_coords()
x = x + 1
```

### Equality

Tuple equality with `==` is **structural** — two tuples are equal when they have the same number of elements and each element compares equal:

```luma
(integer, string) a = (1, "hi")
(integer, string) b = (1, "hi")
(integer, string) c = (2, "hi")

print(a == b) # true
print(a == c) # false
```

Element values are compared recursively using the same rules as the rest of the language.

### Returning Multiple Values

Functions may declare a tuple return type using `(T, T)` syntax:

```luma
function (number, number) min_max(array<number> arr) {
    return (Result.unwrap(Array.min(arr)), Result.unwrap(Array.max(arr)))
}

(number lo, number hi) = min_max([3, 1, 4, 1, 5, 9])

print(lo) # 1
print(hi) # 9
```

---

## 12 — Choice Types — Unit Variants

A choice type where every variant carries no data serves the same role as an enumeration. Use the `choice` keyword to declare one.

### Declaration

```luma
choice Direction { North  South  East  West }

choice Status {
    Active
    Paused
    Archived
}
```

Variant names conventionally start with an uppercase letter. This convention is not enforced by the interpreter.

### Using Choice Values

```luma
Direction heading = Direction.North
Status    state   = Status.Active

print(heading) # Direction.North
print(state)   # Status.Active
```

### Comparison and Match

```luma
if heading == Direction.North {
    print("heading north")
}

match heading {
    case Direction.North { print("↑") }
    case Direction.South { print("↓") }
    case Direction.East  { print("→") }
    case Direction.West  { print("←") }
}
```

Match on choice types must be exhaustive — missing a variant is a type error that aborts execution before the program runs. An `else` arm counts as covering all remaining variants:

```luma
match heading {
    case Direction.North { print("north") }
    else                 { print("other") }
}
```

---

## 13 — Choice Types (ADTs)

Choice types (algebraic data types) declare a closed set of variants where each variant can optionally carry data. They are declared with the `choice` keyword.

### Declaration

```luma
choice Color { Red Green Blue }

choice Shape {
    Circle(number radius)
    Rectangle(number width, number height)
    Point
}
```

Variants that carry no data (like `Point` and `Red`) are called _unit variants_. Variants with fields (like `Circle` and `Rectangle`) are called _data variants_.

### Creating Choice Values

Unit variants are accessed with qualified names. Data variants are called like functions:

```luma
Color c = Color.Red
Shape s = Shape.Circle(5.0)
Shape p = Shape.Point
```

### Matching Choice Types

Match on a choice type must be exhaustive — every variant must be covered. Data variants can be destructured to bind their fields:

```luma
number area = match s {
    case Shape.Circle(r)       { 3.14159 * r * r }
    case Shape.Rectangle(w, h) { w * h }
    case Shape.Point           { 0.0 }
}
```

Unit variants match without parentheses:

```luma
match c {
    case Color.Red   { print("red") }
    case Color.Green { print("green") }
    case Color.Blue  { print("blue") }
}
```

### Generic Choice Types

Choice types can be generic:

```luma
choice Option<T> {
    Some(T value)
    None
}
```

### Recursive Choice Types

Choice types can reference themselves in their variant fields, enabling tree and list structures:

```luma
# Non-generic recursive choice — expression tree
choice Expr {
    Num(integer value)
    Add(Expr left, Expr right)
    Mul(Expr left, Expr right)
}

function integer evaluate(Expr e) {
    return match e {
        case Expr.Num(v)    { v }
        case Expr.Add(l, r) { evaluate(l) + evaluate(r) }
        case Expr.Mul(l, r) { evaluate(l) * evaluate(r) }
    }
}

# (2 + 3) * 4 = 20
Expr expr = Expr.Mul(
    Expr.Add(Expr.Num(2), Expr.Num(3)),
    Expr.Num(4)
)

integer result = evaluate(expr) # 20
```

Generic choice types can also be recursive:

```luma
# Generic linked list
choice List<T> {
    Nil
    Cons(T head, List<T> tail)
}

function<T> integer length(List<T> xs) {
    return match xs {
        case List.Nil         { 0 }
        case List.Cons(_h, t) { 1 + length(t) }
    }
}

List<integer> nums = List.Cons(1, List.Cons(2, List.Cons(3, List.Nil)))

integer len = length(nums) # 3
```

When matching a generic recursive choice, the type checker infers the concrete type parameters from the match subject. For example, matching on `List<integer>` binds `h` to `integer` and `t` to `List<integer>` in the `Cons` arm.

---

## 14 — Result and Optional

`result<T>` represents either a successful value (`success`) or a failure (`failure`). It is the primary error-handling mechanism.

### Creating Results

The inner value of `failure` can be any type, though strings are the convention:

```luma
result<number> good = success(42)
result<number> bad  = failure("something went wrong")
```

### Typed Error Values

By default, the error type is `string`. You can specify a second type parameter to use a custom error type:

```luma
choice MathError { DivisionByZero Overflow }

result<number, MathError> r = failure(MathError.DivisionByZero)
```

When matching on a typed-error result, the `failure` binding has the error type:

```luma
match r {
    success(value) { print("got ${value}") }
    failure(err)   { # err is MathError, not string
        match err {
            case MathError.DivisionByZero { print("div by zero") }
            case MathError.Overflow       { print("overflow") }
        }
    }
}
```

The `failure(...)` expression automatically records the source line and column where it was written. When `Result.unwrap` is called on such a value and throws, the error message includes a `[line:col]` prefix identifying the origin of the failure.

### Checking and Unwrapping

```luma
if Result.is_success(r) {
    number v = Result.unwrap(r) # runtime error if called on failure

    print(v)
}

number v = Result.unwrap_or(r, 0) # returns default value on failure
number w = r ?? 0                  # equivalent shorthand using ??
```

### Matching on Results

```luma
match safe_divide(10, 0) {
    success(value) { print("result: ${value}") }
    failure(msg)   { print("error: ${msg}") }
}
```

### Functions That Return Results

```luma
function result<number> safe_divide(number a, number b) {
    if b == 0 { return failure("division by zero") }

    return success(a / b)
}
```

### Chaining Results with the Pipe Operator

```luma
result<number> final = safe_divide(100, 5)
    |> Result.map_number((number v) -> v * 2)

print(final) # success(40)
```

### Chaining Results with the Error-Pipe Operator

The `!>` operator chains fallible operations. If the left-hand value is a `failure`, the failure is propagated immediately and subsequent steps are skipped. If the left-hand value is `success(v)`, the inner value `v` is unwrapped and passed as the first argument to the right-hand function. A plain (non-`result`) value is treated as `success(value)`.

The overall expression always produces a `result<T>`.

```luma
function result<integer> parse_positive(string s) {
    result<integer> r = Converter.to_integer(s)

    match r {
        failure(e) { return failure(e) }
        success(n) {
            if n > 0 { return success(n) }

            return failure("not positive")
        }
    }
}

function integer double_it(integer n) {
    return n * 2
}

result<integer> r = success("4") !> parse_positive() !> double_it()
# success("4") — left is success, unwrap "4"
# parse_positive("4") returns success(4)
# double_it(4) returns 8, wrapped in success(8)
# r == success(8)

result<integer> bad = success("-1") !> parse_positive() !> double_it()
# success("-1") — left is success, unwrap "-1"
# parse_positive("-1") returns failure("not positive")
# double_it is skipped
# bad == failure("not positive")
```

### Discarded Results

Calling a function that returns `result<T>` and ignoring the return value produces a **type warning**. Assign to `_` to explicitly suppress it:

```luma
function result<integer> parse(string s) { ... }

parse("42")                     # warning: result<integer> returned by 'parse' is discarded
_ = parse("42")                 # OK — suppressed
result<integer> r = parse("42") # OK — assigned
```

### Discarded Values

Calling any non-void function and ignoring the return value produces a **type warning**. This catches accidental omissions such as forgetting to use a computed result. Suppress with `_ = expr`:

```luma
Channel.send(ch, 42)              # warning: boolean return value is unused
_ = Channel.send(ch, 42)          # OK — suppressed
boolean ok = Channel.send(ch, 42) # OK — assigned
```

### Result Functions

| Function                         | Description                                                                                  |
| -------------------------------- | -------------------------------------------------------------------------------------------- |
| `Result.collect(arr)`            | `array<result<T>>` → `result<array<T>>`                                                      |
| `Result.error(r)`                | Inner error value — runtime error if called on `success`                                     |
| `Result.error_code(r)`           | Error code string if `failure`; empty string if `success`                                    |
| `Result.expect(r, msg)`          | Value — runtime error combining `msg` with the underlying error if `failure`                 |
| `Result.filter(r, fn, msg)`      | Failure with `msg` if predicate is false                                                     |
| `Result.flat_map(r, fn)`         | Chain a function that returns `result<T>`; throws if the callback does not return a `result` |
| `Result.flatten(r)`              | `result<result<T>>` → `result<T>`                                                            |
| `Result.is_failure(r)`           | `true` if `failure`                                                                          |
| `Result.is_success(r)`           | `true` if `success`                                                                          |
| `Result.map(r, fn)`              | Transform value if `success` (any inner type)                                                |
| `Result.map_boolean(r, fn)`      | Transform if `success` and inner value is `boolean`; passes through otherwise                |
| `Result.map_failure(r, fn)`      | Transform error value if `failure`; passes through `success` unchanged                       |
| `Result.map_integer(r, fn)`      | Transform if `success` and inner value is `integer`; passes through otherwise                |
| `Result.map_number(r, fn)`       | Transform if `success` and inner value is `number` or `integer`; passes through otherwise    |
| `Result.map_string(r, fn)`       | Transform if `success` and inner value is `string`; passes through otherwise                 |
| `Result.or(r, fallback)`         | Use fallback `result` if `failure`                                                           |
| `Result.or_else(r, fn)`          | Like `or`, but lazily compute the fallback by applying `fn` to the error message             |
| `Result.recover(r, fn)`          | If `failure`, apply `fn` to the inner value and wrap the return in `success`                 |
| `Result.source_function(r)`      | Name of the function that produced the error; empty string if `success`                      |
| `Result.tap(r, fn)`              | Side-effect on `success` value; passes through unchanged                                     |
| `Result.to_optional(r)`          | `success(v)` → `some(v)`, `failure(...)` → `none`                                            |
| `Result.unwrap(r)`               | Value — runtime error if `failure`                                                           |
| `Result.unwrap_or(r, default)`   | Value or default                                                                             |
| `Result.zip(a, b)`               | `success((a_val, b_val))` if both succeed; returns first `failure` otherwise                 |
| `Result.bimap(r, ok_fn, err_fn)` | Map both branches: apply `ok_fn` to success value, `err_fn` to failure error                 |
| `Result.fold(r, ok_fn, err_fn)`  | Extract a value from either branch; both functions must return the same type                 |

### Optional

`optional<T>` represents a value that may or may not be present. It is the safe replacement for nullable values. The type checker enforces that you always handle both cases.

#### Creating Optional Values

```luma
optional<integer> x = some(42) # some — wraps a value
optional<string>  s = none     # none — empty optional
```

Any `T` value is implicitly assignable to `optional<T>`, so functions that return `optional<T>` can `return value` directly without wrapping in `some()`. Use `some(value)` when you need to be explicit.

#### Unwrapping with `??`

The `??` operator provides a default value when the optional is `none`:

```luma
integer val = x ?? 0     # 42
string  str = s ?? "n/a" # "n/a"
```

The `??` operator also works with `result<T>` — it unwraps `success(value)` or returns the right operand on `failure`:

```luma
result<integer> r = String.parse_integer("abc")
integer safe = r ?? 0  # 0 — parse failed
```

#### Unwrapping with `match`

Use `match` for exhaustive handling. Both `some(binding)` and `none` arms are required (or an `else` arm):

```luma
match x {
    case some(v) { print("got ${v}") }
    case none    { print("nothing") }
}
```

#### Optional Chaining

Use `?.` to safely access a field on an optional record, and `?[` to safely index an optional array. The result type is `optional<T>`. When the accessed field is itself optional, chaining auto-flattens — the result stays `optional<T>` instead of becoming `optional<optional<T>>`:

```luma
record User { string name }

optional<User> u = some(User { name = "Alice" })

string name = u?.name ?? "unknown" # "Alice"
```

#### Optional Functions

The `Optional` module provides functional combinators:

```luma
optional<integer> doubled = Optional.map(x, (integer i) -> i * 2) # some(84)
integer           safe    = Optional.unwrap_or(s, "default")      # "default"
result<integer>   r       = Optional.to_result(none, "missing")   # failure("missing")
```

See the [Standard Library Reference — §25 Optional](Luma_Standard_Library_Reference.md#25--optional) for the full function reference.

---

## 15 — Match

Match dispatches on the value of an expression. Arms are evaluated in declaration order and the first matching arm executes. Match is exhaustive for booleans, choice types, `result<T>`, and `optional<T>` — a type error is raised if arms are missing and execution is aborted before the program runs.

### Comparison Arms

All six comparison operators are supported. Comparison matches require an `else` arm (or a `case !=` arm, which acts as a catch-all for the type checker). The `==` operator compares the subject against a **non-literal** expression; to match a literal by value, use the bare-literal form below rather than `case == <literal>`:

```luma
match score {
    case >= 90    { print("A") }
    case >= 80    { print("B") }
    case 0        { print("absent") }    # bare literal — not `case == 0`
    case != 50    { print("not fifty") }
    case == cutoff { print("at cutoff") } # `==` against a variable is allowed
    else          { print("F") }
}
```

### Matching Strings

String arms are written as a bare string literal:

```luma
match command {
    case "quit" { stop() } # bare string literal
    case "help" { show_help() }
    else        { print("unknown: ${command}") }
}
```

Writing `case == "quit"` is a **syntax error** — a string literal has one obvious spelling, the bare form. (The `==` operator is still valid against a non-literal expression, such as another `string` variable.)

String match arms always require an `else` arm.

### Matching Integers

Integer arms are written as bare literals — the equality check is implicit:

```luma
match day_of_week {
    case 1 { "Monday" }
    case 2 { "Tuesday" }
    case 3 { "Wednesday" }
    else   { "other" }
}
```

Writing `case == 1` is a **syntax error**: match integer literals by value with the bare `case 1` form. (A `number` literal such as `case == 3.14` has no bare form and remains a valid comparison arm, as does `case == n` against a non-literal.)

Integer match arms always require an `else` arm.

### Matching Integer Ranges

An arm can match a whole **integer range** using the same range syntax as loops and range membership. `lo..=hi` is inclusive of both bounds; `lo..hi` is half-open (excludes the upper bound):

```luma
string grade = match score {
    case 90..=100 { "A" }
    case 80..=89  { "B" }
    case 70..=79  { "C" }
    else          { "F" }
}
```

A range arm matches when the subject satisfies the same bounds test as `subject in lo..hi` — that is, `subject >= lo && (inclusive ? subject <= hi : subject < hi)`. The bounds must be integer literals, and the match subject must be an `integer`.

Range arms combine with `|` alternatives, and can be mixed with plain integer arms:

```luma
match code {
    case 0..=9 | 20..=29 { "special" }
    case 10 | 11..=19    { "teens-ish" }
    else                 { "other" }
}
```

Like plain integer arms, range arms are open-ended, so a match using them always requires an `else` arm.

### Matching Booleans

```luma
match flag {
    case true  { print("enabled") }
    case false { print("disabled") }
}
```

### Matching Results

```luma
match safe_divide(a, b) {
    success(value) { print("result: ${value}") }
    failure(msg)   { print("error: ${msg}") }
}
```

### Matching Choice Types

```luma
match status {
    case Status.Active   { print("running") }
    case Status.Paused   { print("paused") }
    case Status.Archived { print("done") }
}
```

### Matching Optionals

`optional<T>` match is exhaustive — both `some(x)` and `none` arms are required (or an `else` arm):

```luma
optional<integer> maybe_value = some(42)

match maybe_value {
    case some(v) { print("got: ${v}") }
    case none    { print("nothing here") }
}
```

The `case some(x)` arm binds the inner value to `x` within the arm body. The `case none` arm fires when the optional is empty.

Match expressions on `optional<T>` work the same way:

```luma
string label = match maybe_value {
    case some(v) { "value is ${v}" }
    case none    { "no value" }
}
```

### Multiple Patterns Per Arm

Use `|` to combine several patterns in a single arm. The arm matches if **any** of the alternatives match:

```luma
choice Color { Red, Green, Blue }

mutable Color c = Color.Red

match c {
    case Color.Red | Color.Blue { print("extreme") }
    case Color.Green            { print("middle") }
}
```

Multiple patterns work with booleans, choice variants, string literals, integer literals, integer ranges, comparison operators, and `none`:

```luma
match command {
    case "quit" | "exit" | "q" { stop() }
    case "help" | "?"          { show_help() }
    else                       { print("unknown") }
}

match code {
    case 1 | 2 | 3 { print("low") }
    case 4 | 5 | 6 { print("mid") }
    else           { print("high") }
}

match score {
    case 0 | 1 { print("very low") }
    case >= 90       { print("high") }
    else             { print("other") }
}

match score {
    case 0..=59  | 60..=69 { print("failing-ish") }
    case 70..=100          { print("passing") }
    else                   { print("out of range") }
}
```

> **Note:** Binding patterns (`success`, `failure`, `some`, and choice destructuring) cannot be combined with `|` because each alternative would need its own binding name.

### Guards (`when` clauses)

Any pattern arm can be refined with a `when` clause — a boolean condition that must also hold for the arm to fire. If the pattern matches but the guard is `false`, matching **falls through** to the next arm:

```luma
match n {
    case >= 0 when n == 0 { print("zero") }
    case >= 0             { print("positive") }
    else                  { print("negative") }
}
```

A guard can reference the bindings introduced by its pattern, so guards combine naturally with `some`, `success`, `failure`, and choice destructuring:

```luma
match maybe_value {
    case some(v) when v > 10 { print("big: ${v}") }
    case some(v)             { print("small: ${v}") }
    case none                { print("nothing") }
}
```

Guards work in expression position too, including on `result` arms:

```luma
string label = match parse(input) {
    success(v) when v > 0 { "positive ${v}" }
    success(v)            { "non-positive ${v}" }
    failure(e)            { "error: ${e}" }
}
```

The guard expression must be of type `boolean`. Guards attach to pattern arms (`case`, `some`, `success`, `failure`); the `else` arm is the unconditional fallback and takes no `when` clause.

> **Exhaustiveness.** A guarded arm never counts toward exhaustiveness, because its guard may be `false` at runtime. Each guarded arm therefore needs an unguarded fallback — another arm for the same pattern, or an `else` — so a value can never fall through with no matching arm. The following is a type error because the only `some` arm is guarded:
>
> ```luma
> match maybe_value {
>     case some(v) when v > 10 { "big" }
>     case none                { "none" }
> }
> # TypeError: match on optional must cover both 'some' and 'none'
> ```

### Match as an Expression

All arm bodies are enclosed in `{ }` blocks. The last expression in each block is the arm's value:

```luma
string grade = match score {
    case >= 90 { "A" }
    case >= 80 { "B" }
    case >= 70 { "C" }
    else       { "F" }
}

print(grade) # B (for score = 85)
```

Multi-statement arm bodies are also supported:

```luma
number result = match x {
    case 0 {
        print("zero case")

        0
    }
    else {
        x * 2
    }
}
```

---

## 16 — String Interpolation and Multi-Line Strings

### Interpolation

Embed any expression using `${expr}`:

```luma
string  name = "Luma"
integer ver  = 1

print("Welcome to ${name} ${ver}.0!")
```

Complex expressions work too:

```luma
print("Total: ${Result.unwrap(Array.sum(prices))}")
print("Status: ${if ok { "good" } else { "bad" }}")
```

### Multi-Line Strings

Triple-quoted strings span multiple lines. Common leading whitespace is automatically stripped (dedented) to align with the content:

```luma
string message = """
    Hello, World!
    This is a multi-line string.
    Indentation is removed automatically.
    """

print(message)
# Hello, World!
# This is a multi-line string.
# Indentation is removed automatically.
```

Triple-quoted strings support interpolation:

```luma
string name   = "Alice"
string letter = """
    Dear ${name},
    Thank you for your message.
    """
```

> **Tip — dictionary-driven substitution:** When you need to fill multiple named placeholders from data stored in a dictionary, use `String.template` instead of interpolation. See the [Standard Library Reference — §36 String](Luma_Standard_Library_Reference.md#36--string) for details.
>
> ```luma
> dictionary<string> ctx = {"name": "Alice", "day": "Monday"}
>
> string msg = String.template("Hello, {name}! Today is {day}.", ctx)
> ```

> **Warning:** Interpolating a function value (e.g. `"${my_func}"`) or a namespace (e.g. `"${Math}"`) produces `<function ...>` or `<namespace>` at runtime. The linter warns about this — you probably meant to call the function: `"${my_func()}"`.

---

## 17 — Pipe Operator

The `|>` operator passes the left-hand value as the **first argument** to the right-hand function:

```luma
# These are equivalent:
string result = String.trim(String.uppercase("hello world"))
string result = "hello world" |> String.uppercase |> String.trim
```

### With Extra Arguments

The piped value is inserted as the first argument; additional arguments follow:

```luma
array<number> result = [1, 2, 3, 4, 5, 6]
    |> Array.filter((number x) -> x % 2 == 0)
    |> Result.unwrap_or([])
    |> Array.map((number x) -> x * x)
    |> Result.unwrap_or([])
```

### Multi-Line Pipes

`|>` may appear at the start of a new line:

```luma
array<string> processed = raw_lines
    |> Array.map((string s) -> String.trim(s))
    |> Result.unwrap_or([])
    |> Array.filter((string s) -> !String.is_empty(s))
    |> Result.unwrap_or([])
    |> Array.map((string s) -> String.uppercase(s))
    |> Result.unwrap_or([])
```

### Pipes with Named Arguments

```luma
string padded = "42"
    |> String.pad_left(width: 6, fill: "0")
```

### Error-Pipe Operator `!>`

`!>` is a short-circuiting variant of `|>` for fallible pipelines. See [§14 Result and Optional](#14--result-and-optional) for full details and examples.

---

## 18 — Named Arguments

Any function may be called with named arguments. Named arguments may appear in any order, but all positional arguments must come first:

```luma
function string create_user(string name, integer age, boolean active) {
    return "${name} (${age})"
}

# Positional
string u1 = create_user("Alice", 30, true)

# All named, any order
string u2 = create_user(active: true, age: 30, name: "Alice")

# Mixed: positional first, then named
string u3 = create_user("Alice", active: true, age: 30)
```

Named arguments are especially clear for boolean flags and functions with many same-typed parameters:

```luma
string padded = String.pad_left(s: "007", width: 6, fill: "0") # "000007"
```

---

## 19 — Type Aliases

A type alias gives a new name to an existing type:

```luma
type UserId   = string
type Score    = number
type UserList = array<string>

UserId id     = "user-42"
Score  points = 99
```

Type aliases are purely a compile-time concept — they introduce no runtime overhead and no new type. The alias name and the underlying type are fully interchangeable. Use `--check` when you want to validate types without running.

Do not create self-referential or mutually recursive aliases — the type checker detects the cycle and reports a `TypeError`:

```luma
# Both of these are type errors:
type A = A # self-referential
type B = C # mutually recursive
type C = B
```

---

## 20 — Interfaces

Structural interfaces describe the shape a record must have. Any record that has all required fields automatically satisfies the interface — no explicit `implements` declaration is needed.

Interfaces are a compile-time-only concept enforced before execution. Interface annotations in function parameters are checked when the type checker runs.

### Declaring an Interface

```luma
interface Named {
    string name
}

interface Scored {
    string name,
    number score
}
```

### Using an Interface as a Parameter Type

```luma
function string greet(Named entity) {
    return "Hello, ${entity.name}!"
}

record Player { string name, number score, integer level }

Player p = Player { name = "Alice", score = 99, level = 5 }

print(greet(p)) # Hello, Alice!

# Player satisfies Named automatically — it has a 'name' field of type string
```

### Field Type Matching

A record field satisfies an interface field when its type is assignable to the interface field's type. Because `integer` is assignable to `number`, a record with an `integer` field satisfies an interface that declares that field as `number`:

```luma
interface Measurable {
    number value
}

record Reading { integer value }

# Reading satisfies Measurable under --check (integer is assignable to number)
```

### Interface-to-Interface Assignability

An interface-typed value satisfies another interface when every field required by the target interface is present with a compatible type in the source interface:

```luma
interface Named {
    string name
}

interface NamedAndScored {
    string name,
    number score
}

function string greet(Named entity) {
    return "Hello, ${entity.name}!"
}

record Player { string name, number score, integer level }

Player p = Player { name = "Alice", score = 99, level = 5 }

# Player directly satisfies NamedAndScored.
NamedAndScored ns = p

# NamedAndScored also satisfies Named (it has the required 'name' field).
greet(ns) # ok — interface-to-interface assignability
```

---

## 21 — Generics, `downcast`, and `is`

Luma supports generic type parameters on functions, records, interfaces, and type aliases. A type parameter is declared in angle brackets after the name and may appear anywhere a type is expected in that declaration.

### Generic Functions

Declare a function with one or more type parameters by writing `<T>` (or `<T, U>` etc.) after the function name. The type checker infers the concrete type from the call-site arguments:

```luma
function<T> T identity(T value) {
    return value
}

integer n = identity(42)      # T inferred as integer
string  s = identity("hello") # T inferred as string
```

Multiple type parameters:

```luma
function<T, U> (U, T) swap(T first, U second) {
    return (second, first)
}

(string, integer) pair = swap(1, "hello") # T=integer, U=string
```

### Generic Records

Declare a record with type parameters to create reusable container types:

```luma
record Box<T> {
    T value
}

Box<integer> int_box = Box<integer> { value = 42 }
Box<string>  str_box = Box<string>  { value = "hi" }

integer n = int_box.value # 42
string  s = str_box.value # "hi"
```

### Generic Interfaces

Interfaces may carry type parameters, which are resolved when a record is checked for structural conformance:

```luma
interface Container<T> {
    T get()
}
```

Any record whose `get()` method returns the matching type satisfies the interface.

### Generic Type Aliases

```luma
type Pair<T> = (T, T)

Pair<integer> coords = (10, 20)
Pair<string>  names  = ("Alice", "Bob")
```

### Bounded Generic Parameters

A type parameter can be constrained by one or more interface bounds. The type checker enforces bounds at every call site — passing a type that does not structurally satisfy the bound produces a compile-time error:

```luma
interface Printable {
    string to_string()
}

function<T: Printable> string render(T item) {
    return item.to_string()
}
```

Multiple bounds are separated with `+`:

```luma
function<T: Printable + Comparable> string render_sorted(array<T> items) {
    return items
        |> Array.sort((T a, T b) -> a.compare(b))
        |> Array.map((T item) -> item.to_string())
        |> Array.join(", ")
}
```

### Turbofish — Explicit Type Arguments

When the type checker cannot infer the type parameter from the arguments alone, you can supply explicit type arguments using the turbofish operator `::< >`:

```luma
function<T> T default_value() {
    return match type_name_of(T) {
        case "integer" { 0 }
        case "string"  { "" }
        else           { none }
    }
}

integer n = default_value::<integer>() # T = integer
string  s = default_value::<string>()  # T = string
```

Multiple type arguments are separated with commas:

```luma
(string, integer) pair = swap::<integer, string>(1, "hello")
```

When type arguments can be inferred from the call-site arguments, the turbofish is optional — `identity(42)` and `identity::<integer>(42)` are equivalent.

### Narrowing with `downcast<T>`

`downcast<T>` tests the runtime type of a value and returns `result<T>` — it succeeds if the runtime type matches:

```luma
number val = 3.14

result<number> r = downcast<number>(val)

match r {
    success(n) { print("number: ${n}") }
    failure(m) { print("not a number: ${m}") }
}
```

```luma
string s = "hello"

result<number> bad  = downcast<number>(s) # failure("downcast failed: value is not of type 'number'")
result<string> good = downcast<string>(s) # success("hello")
```

### Narrowing with `trusted_downcast<T>`

`trusted_downcast<T>` is the asserting variant of `downcast`. It returns `T` directly instead of `result<T>`. If the runtime type does not match, it throws a `RuntimeError` immediately.

Use it when you have already confirmed the type (e.g. via `is<T>`) and want to avoid unwrapping a `result` just to get the value:

```luma
integer val = 42
integer n   = trusted_downcast<integer>(val) # returns 42 directly
```

If the type does not match at runtime:

```luma
string  s = "hello"
integer n = trusted_downcast<integer>(s) # RuntimeError: trusted_downcast failed: value is not of type 'integer'
```

> **When to prefer `downcast` vs `trusted_downcast`.** Use plain `downcast<T>` (returns `result<T>`) when the type mismatch is a recoverable condition. Use `trusted_downcast<T>` when a type mismatch is a programming error that should never happen — the same way you would use `Result.unwrap` for a result you are certain is `success`.

Choice types, tuples, `result`, `channel`, `task`, and `socket` are all supported:

```luma
choice Status { Active  Paused }

Status         e  = Status.Active
result<Status> rs = downcast<Status>(e) # success(Status.Active)

(integer, string)         t  = (1, "hi")
result<(integer, string)> rt = downcast<(integer, string)>(t) # success((1, "hi"))

result<integer>         r  = success(42)
result<result<integer>> rr = downcast<result<integer>>(r) # **recursively** success(success(42))
```

> **Note — `result<failure>` skips the inner type check.** When a `result` value holds a `failure`, its inner payload is an error message string rather than a value of type `T`. Consequently, `downcast<result<T>>` always succeeds for a `failure` result regardless of `T`. Only `success` variants have their inner value verified against `T`.

> **Warning — redundant `downcast<T>`.** If the type checker can already prove that a value is of type `T`, writing `downcast<T>(value)` is unnecessary. The type checker will emit a warning: `redundant downcast: value is already of type 'T'`. Use the value directly or, if a `result<T>` is needed, wrap it with `success(value)`.

### Supported Target Types

`downcast<T>` recognises the following target types:

| Category     | Target types                                               |
| ------------ | ---------------------------------------------------------- |
| Arrays       | `array<T>` — element type verified at every nesting level  |
| Channel/Task | `channel<T>`, `task<T>` — base type only (see note below)  |
| Choice types | Choice types (matched by type name)                        |
| Collections  | `queue<T>`, `stack<T>`, `set<T>` — element type verified   |
| Dictionaries | `dictionary<V>` — value type verified at every nesting level |
| Optionals    | `optional<T>` — `none`, or inner value verified against `T` |
| Primitives   | `boolean`, `integer`, `none`, `number`, `string`           |
| Records      | Record types (matched by type name)                        |
| References   | `reference<T>` — referenced value verified against `T`     |
| Results      | `result<T>` — inner type verified for `success` variants   |
| Tuples       | `(T, U, …)` — element count and each element type verified |

Two important notes for numeric types:

- `downcast<number>` **accepts** `integer` **values** (same widening rule as `integer -> number` assignment). `downcast<integer>` does **not** accept `number` values — there is no automatic narrowing.
- **Widening for element types.** `downcast<array<number>>` and `downcast<(number, ...)>` also accept `integer` elements. Bare `downcast<array>(val)` succeeds for any array without checking elements.
- **Recursive element validation.** Type parameters are checked at **every** nesting level for arrays, dictionaries, results, optionals, references, tuples, and the `queue`/`stack`/`set` collections. For example `downcast<dictionary<array<integer>>>` verifies that every value is an array **and** that every inner element is an integer.

> **Note — `channel<T>` and `task<T>` verify the base type only.** A channel's pending values and a task's eventual result cannot be inspected without consuming them, so their element type `T` is not checked at runtime. `downcast<channel<integer>>` succeeds for any channel, and `downcast<task<integer>>` for any task, regardless of `T`.

### Checking with `is<T>`

`is<T>` tests whether a value matches a type and returns `boolean` — no `result` wrapper needed:

```luma
integer val = 42

boolean yes = is<integer>(val) # true
boolean no  = is<string>(val)  # false
```

`is<T>` uses the same type rules as `downcast<T>`: `is<number>` accepts `integer` values, container element types are checked recursively at every nesting level, choice types are matched by type name, and `is<none>` returns `true` for none values.

```luma
array<integer> nums = [1, 2, 3]

if is<array<integer>>(nums) {
    print("got integer array")
}

optional<integer> maybe = none

if is<none>(maybe) {
    print("value is none")
}
```

Use `is<T>` when you only need to branch on the type without binding the narrowed value. Use `downcast<T>` when you also need to use the typed value.

**Unknown type names are a compile-time error.** If the type passed to `is<T>` or `downcast<T>` does not exist in the program, the type checker reports an error immediately rather than silently failing at runtime.

---

## 22 — Namespaces and `use`

### Declaring a Namespace

Namespaces group related functions, records, choice types, type aliases, and interfaces together. All members can be accessed with or without the namespace qualifier.

```luma
namespace Geometry {
    record Point { number x, number y }

    choice Quadrant { I  II  III  IV }

    type Distance = number

    function number distance(Point a, Point b) {
        result<number> d = Math.square_root((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y))

        return Result.unwrap(d)
    }
}
```

### Qualified Access

All namespace members — functions, records, choice types, type aliases, and interfaces — support the `Namespace.member` qualified syntax. For choice variants, use the three-part form `Namespace.Choice.Variant`.

```luma
# Qualified record type annotation and creation.
Geometry.Point p = Geometry.Point { x = 3, y = 4 }
Geometry.Point o = Geometry.Point { x = 0, y = 0 }

# Qualified function call.
Geometry.Distance d = Geometry.distance(p, o)

# Qualified choice variant.
Geometry.Quadrant q = Geometry.Quadrant.I
```

### Bare Names Require `use`

Namespace members are **not** available as bare (unqualified) names unless you explicitly import them with `use Namespace`. Without `use`, only the qualified form works:

```luma
# Without use — only qualified access works.
Geometry.Point p = Geometry.Point { x = 3, y = 4 }
number         d = Geometry.distance(p, Geometry.Point { x = 0, y = 0 })
```

### `match` with Namespace-Qualified Choice Types

Choice type cases inside `match` statements accept the fully qualified `Namespace.Choice.Variant` pattern as well as the bare `Choice.Variant` form.

```luma
match q {
    case Geometry.Quadrant.I   { print("top right") }
    case Geometry.Quadrant.II  { print("top left") }
    case Geometry.Quadrant.III { print("bottom left") }
    case Geometry.Quadrant.IV  { print("bottom right") }
}
```

### Wildcard Import

`use Namespace` imports all functions **and** type members from that namespace as unqualified names. If a simple (non-dotted) short name is already defined by user code in the current scope, a **TypeError** is raised:

```text
import conflict: 'distance' is already defined;
use 'Geometry.distance' to access the imported name
```

Names that were themselves registered by a namespace (i.e. bare names introduced by `namespace` declarations) are silently skipped — importing the same namespace twice or importing two namespaces that share a member name is not an error. The fully qualified form always works regardless.

```luma
use Geometry

Point p2 = Point { x = 0, y = 0 }

distance(p, p2)          # imported from Geometry
Geometry.distance(p, p2) # always works regardless of use
```

For choice types, `use Namespace` makes the bare variant names available too:

```luma
use Geometry

Geometry.Quadrant q2 = Quadrant.I # Quadrant.I imported as bare name
```

### Specific Import

`use Namespace.name` imports a single member. If the name does not exist, a runtime error is raised when the `use` line executes:

```luma
use Geometry.distance

number d = distance(p1, p2)
```

### Internal Members

Use the `internal` keyword before a declaration inside a namespace to make it **namespace-private**. Internal members are fully accessible from within the namespace but are invisible and inaccessible from outside it. The `internal` keyword is only allowed directly inside a namespace body; using it at top level is a syntax error.

```luma
namespace Formatter {
    # Public API — callable from anywhere.
    function string format_title(string text) {
        string cleaned = Formatter.strip(text) # calling internal from within — OK

        return Formatter.capitalise(cleaned)
    }

    # Internal helpers — hidden from external code.
    internal function string strip(string text) {
        return String.trim(text)
    }

    internal function string capitalise(string text) {
        return String.title_case(text)
    }

    internal record FormatOptions {
        boolean preserve_case,
        string separator
    }

    internal choice FormatMode { TitleCase, LowerCase, UpperCase }

    internal type TagList = array<string>
}
```

Attempting to access an internal member from outside the namespace is a **TypeError**:

```luma
# Type error: 'strip' is internal to namespace 'Formatter'
string s = Formatter.strip("hello")
```

`use Namespace` wildcard imports skip internal members — they are never promoted to bare names:

```luma
use Formatter

# 'format_title' is imported (public)
string t = format_title("hello")

# 'strip' is NOT imported — the following would be an undefined-variable error:
# string s = strip("hello")
```

### Declaration Order

`use` declarations are resolved after all namespace declarations have been registered, so the order of `namespace` and `use` in the source file does not matter — `use Geometry` may appear before the `namespace Geometry { ... }` block.

### Standard Library Namespaces

- `Array`
- `Bits`
- `Calculus`
- `Channel`
- `Color`
- `Compression`
- `Console`
- `Converter`
- `Csv`
- `DateTime`
- `Decimal`
- `Dictionary`
- `Encoder`
- `FileSystem`
- `GraphicalUi`
- `Hash`
- `Http`
- `Json`
- `KeyValueStore`
- `LinearAlgebra`
- `Log`
- `Math`
- `Optional`
- `Order`
- `Process`
- `Queue`
- `Random`
- `Reference`
- `RegularExpression`
- `Resource`
- `Result`
- `Set`
- `Socket`
- `Stack`
- `Statistics`
- `String`
- `Task`
- `Terminal`
- `Xml`

All 39 are available fully qualified without `use`.

---

## 23 — Ownership (`unique` and `borrow`)

Luma provides optional ownership annotations for values that must be consumed exactly once or that are borrowed references.

### Unique Values

A `unique` variable represents exclusive ownership of a value. It must be consumed (passed to a function or assigned away) exactly once. Using it a second time is a type error, and leaving scope without consuming it produces a warning.

```luma
unique string handle = acquire_resource()

process(handle)    # consumed here
# process(handle) — ERROR: already consumed
```

### Borrowed References

A `borrow` variable is a read-only reference to a value. It cannot be consumed or moved:

```luma
borrow string name = get_name()

print(name) # OK — reading is allowed
```

### Suppressing the `_` Prefix

Variables prefixed with `_` suppress the "unused" and "unconsumed" warnings:

```luma
unique string _ignored = acquire()
# no warning even though _ignored is never consumed
```

---

## 24 — Testing with `@test`

### Writing Tests

`@test` functions must take **no parameters** and have a `void` return type. They run in isolation — each test gets its own local scope — but all tests share the same global environment, so mutations to top-level mutable variables in one test are visible to subsequent tests.

```luma
@test
function void test_addition() {
    assert(2 + 2 == 4)
    assert(10 % 3 == 1)
}

@test
function void test_strings() {
    assert(String.length("hello") == 5)
    assert(String.uppercase("hi") == "HI")
}
```

`assert(condition)` fails the test with the message `"assertion failed"`. An optional second argument overrides the message:

```luma
@test
function void test_with_message() {
    integer result = 2 + 2

    assert(result == 4, "expected 4 but got ${result}")
}
```

Any runtime error inside a `@test` function — not just assertion failures — causes that test to be marked `[FAIL]`. The error message is displayed next to the test name.

### Running Tests

```bash
luma --test myfile.luma
```

Output:

```text
[PASS] test_addition
[PASS] test_strings
[PASS] test_with_message

3 passed, 0 failed.
```

A non-zero exit code is returned if any test fails (exit code 1 — runtime error), making `--test` suitable for CI pipelines.

---

## 25 — Including Files

```luma
include "utils.luma"
include "models.luma"
```

Paths are resolved relative to the directory of the including file. When including from the REPL or a source with no file path, the current working directory is used as the base instead.

If an included file is not found relative to the including file's directory, the interpreter searches the directories listed in the `LUMA_PATH` environment variable (in order). Separate directories with `;` on Windows or `:` on Unix:

```bash
# Windows
set LUMA_PATH=C:\libs\luma;D:\shared\luma
luma program.luma

# Unix
export LUMA_PATH=/usr/local/lib/luma:/home/user/libs
luma program.luma
```

Each file is included at most once per compilation. Deduplication is based on the canonical file path, so two `include` statements that resolve to the same file via different relative paths are still treated as one inclusion.

### Circular Includes

Circular includes are safe and do not produce an error. Because each file is included at most once (see above), a cycle such as `a.luma → b.luma → a.luma` simply resolves the repeated include as a no-op — each file's declarations are merged exactly once.

As a safeguard against pathological nesting, an include chain more than 64 levels deep is rejected:

```text
include depth limit exceeded (64) while including 'b.luma'
```

### Symbolic Links

Including a file that is a symbolic link is rejected with a compile error:

```text
include rejected: 'utils.luma' is a symbolic link
```

### Top-Level Statements

An included file may contain top-level statements in addition to declarations. Those statements are executed as part of the including program, before `@main` runs. For predictable behaviour, limit included files to declarations only (functions, records, choice types, type aliases, interfaces).

> **Warning — top-level statements in included files.**
> When the compiler detects that an included file contains top-level statements, it emits a warning to stderr:
>
> ```text
> warning: included file 'utils.luma' contains top-level statements that will execute before @main
> ```
>
> Side-effecting statements in included files (such as `print` calls or mutable variable declarations with initialisers) can be surprising to the reader. Keep included files declaration-only to avoid this warning.

---

## 26 — Standard Library Reference

The complete standard library reference now lives in its own document: **[Luma — Standard Library Reference](Luma_Standard_Library_Reference.md)**.

It documents all built-in functions and every standard library module — `String`, `Array`, `Dictionary`, `Math`, `GraphicalUi`, and more — with parameter types, return types, and descriptions.

---

## 27 — Linter and `--strict` Mode

The type checker includes a built-in linter that emits warnings for suspicious or error-prone patterns. These warnings do not prevent execution by default.

### Warning Categories

| Warning                      | Description                                                                                                |
| ---------------------------- | ---------------------------------------------------------------------------------------------------------- |
| Always-false condition       | `while false { ... }` — loop body will never execute                                                       |
| Discarded result             | A function returning `result<T>` is called and the return value is not used                                |
| Discarded value              | A non-void function call whose return value is not assigned, piped, or consumed — suppress with `_ = expr` |
| Empty body                   | A function, `if`, `for`, or `while` block has an empty body — likely incomplete code                       |
| Downcast always fails        | `downcast<T>` on a value whose type is incompatible with `T` — will always fail at runtime                 |
| Floating-point equality      | `==` or `!=` on `number` values — may give unexpected results due to rounding                              |
| Function interpolation       | Interpolating a function value in a string — did you mean to call it?                                      |
| Included file side-effects   | An included file contains top-level statements that will execute before `@main`                            |
| Incompatible comparison      | `==` or `!=` between unrelated types (e.g. `integer == string`) — result is always `false` / `true`        |
| Mutable but never mutated    | A variable or parameter is declared `mutable` but is never reassigned                                      |
| Namespace interpolation      | Interpolating a namespace in a string — unlikely to be useful                                              |
| Not a Luma keyword           | Using `var` or `let` — these are not Luma keywords; use `type name = value`                                |
| Optional chain not unwrapped | `?.` or `?[` result assigned to a non-`optional<T>` variable without a `??` fallback                       |
| Redundant boolean            | Comparing a boolean with `true` or `false` (e.g. `x == true`) — use the boolean directly                   |
| Redundant downcast           | `downcast<T>` on a value already known to be `T` — the cast is unnecessary                                 |
| Self-assignment              | Assigning a variable to itself has no effect                                                               |
| Shadow variable              | A local variable shadows a variable from an outer scope — prefix with `_` to suppress                      |
| Unconsumed unique            | A `unique` variable leaves scope without being consumed                                                    |
| Unnecessary semicolon        | Luma does not use semicolons — they can be safely removed                                                  |
| Unreachable code             | Code appears after a `return`, `break`, or `continue`                                                      |
| Unstructured spawn           | `spawn` used outside a `task_scope` block — task runs fire-and-forget                                      |
| Unsafe trusted_downcast      | `trusted_downcast` on an unrefined stdlib value has no compile-time safety guarantee                       |
| Unused function              | A function is declared but never called — prefix with `_` to suppress                                      |
| Unused parameter             | A function parameter is never used — prefix with `_` to suppress                                           |
| Unused variable              | A variable is declared but never read — prefix with `_` to suppress                                        |
| Void assignment              | A variable is assigned the return value of a function that returns nothing                                 |

### `--strict` / `-s` Mode

By default, linter warnings are informational. With `--strict` (or `-s`), all linter warnings become errors and halt execution:

```bash
luma --strict program.luma
luma -s program.luma
```

Combine with `--check` to run only the type checker in strict mode without executing:

```bash
luma --check --strict program.luma
```

### Error Hints

When a type mismatch occurs, the type checker provides a hint suggesting how to fix the error. For example, assigning a `string` to a `number` variable suggests using `Converter.to_number()`.

---

## 28 — Reserved Keywords

All 47 identifiers below are reserved and cannot be used as variable, function, record, choice type, or namespace names:

| Keyword      | Keyword        | Keyword       | Keyword            |
| ------------ | -------------- | ------------- | ------------------ |
| `array`      | `await`        | `boolean`     | `borrow`           |
| `break`      | `case`         | `catch`       | `choice`           |
| `continue`   | `decimal`      | `dictionary`  | `downcast`         |
| `else`       | `failure`      | `false`       | `finally`          |
| `for`        | `function`     | `if`          | `in`               |
| `include`    | `integer`      | `interface`   | `internal`         |
| `is`         | `match`        | `mutable`     | `namespace`        |
| `none`       | `number`       | `optional`    | `record`           |
| `result`     | `return`       | `some`        | `spawn`            |
| `string`     | `success`      | `task_scope`  | `true`             |
| `trusted_downcast` | `try`    | `type`        | `unique`           |
| `use`        | `while`        | `with`        |                    |

The container and handle types — `channel`,
`key_value_store`, `queue`, `reference`, `set`, `socket`, `stack`,
`task`, `widget`, and `xml` — are **not** reserved words. They are ordinary
identifiers that name built-in generic types, so `queue<integer> q = …` still
declares a typed variable while `integer queue = …` is also allowed.

---

## 29 — Error Reference

Luma reports four distinct error categories. Each category maps to a specific exit code (see §1 — Exit Codes):

- **SyntaxError** (exit code 3) — lexer or parser could not process the source text.
- **CompileError** (exit code 4) — include resolution failed (circular, missing, or symlink).
- **TypeError** (exit code 2) — `--check` found a type or structural violation.
- **RuntimeError** (exit code 1) — execution encountered an illegal operation.

Diagnostic output is colourised when writing to an interactive terminal (ANSI colours). When output is piped or redirected, colours are automatically disabled.

### Syntax Errors

| Error                                                                                   | Cause                                                            |
| --------------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| `'internal' keyword is only allowed inside a namespace`                                 | `internal` used at the top level                                 |
| `'internal' must be followed by 'function', 'record', 'choice', 'interface', or 'type'` | `internal` precedes an unsupported declaration                   |
| `'\                                                                                     | ' alternatives cannot be used with patterns that bind variables` |
| `binary literal has no digits`                                                          | No digits after `0b` prefix                                      |
| `cannot open file 'path'`                                                               | Source file could not be opened                                  |
| `choice destructuring is not allowed in '\                                              | ' alternatives`                                                  |
| `expected annotation name after '@'`                                                    | `@` not followed by an identifier                                |
| `expected declaration`                                                                  | File must contain a function, record, or other declaration       |
| `expected field name after '.'`                                                         | Field access operator not followed by a valid field name         |
| `expected field name after '?.'`                                                        | Optional field access not followed by a valid field name         |
| `expected string continuation`                                                          | Mismatched interpolation bracket inside a string                 |
| `expected type annotation, got 'x'`                                                     | Type annotation expected but not found                           |
| `expected X, got Y 'z'`                                                                 | Required syntax element missing                                  |
| `expected '>', got Y 'z'`                                                               | Generic type argument list not closed                            |
| `hex literal has no digits`                                                             | No digits after `0x` prefix                                      |
| `integer literal out of range: lexeme`                                                  | Integer constant does not fit in `int64_t`                       |
| `invalid alternative pattern after '\                                                   | '`                                                               |
| `invalid integer literal: lexeme`                                                       | Integer literal format is invalid                                |
| `invalid match arm`                                                                     | Expected a literal, variable binding, choice variant, or `_`     |
| `invalid number literal: lexeme`                                                        | Number literal format is invalid                                 |
| `maximum nesting depth exceeded`                                                        | Expression or structure nested too deeply (limit 128)            |
| `maximum string interpolation nesting depth exceeded`                                   | String interpolation nesting too deep                            |
| `missing return type for function 'name'`                                               | Function declaration must have a return type                     |
| `number literal out of range: lexeme`                                                   | Number constant is not a valid `double`                          |
| `positional argument after named argument`                                              | Positional arguments must precede named arguments                |
| `try must have at least a 'catch' or 'finally' block`                                   | `try` missing both `catch` and `finally`                         |
| `unexpected character '...'`                                                            | Invalid character in source text                                 |
| `unexpected token 'x'`                                                                  | Expected an expression but found an unexpected token             |
| `unexpected '{' — block expressions are not supported outside of control flow`          | Block expression outside `if`, `match`, `for`, or `while`        |
| `unknown annotation @x`                                                                 | Annotation is not `@main` or `@test`                             |
| `unknown escape sequence '\c'`                                                          | Invalid escape; valid are `\n \t \r \0 \\ \" \$`                 |
| `unterminated string`                                                                   | String literal has no closing quote                              |
| `'kw' is a reserved keyword and cannot be used as an identifier`                        | Reserved keyword used as a variable or function name             |

### Type Errors

Detected before execution. If any type errors are found, execution does not start.

#### Variables and Scope

| Error                                            | Cause                                 |
| ------------------------------------------------ | ------------------------------------- |
| `undefined variable 'x'`                         | `x` is not in scope                   |
| `variable 'x' is already declared in this scope` | Duplicate variable declaration        |
| `unknown type 'T'`                               | Unrecognised type name in declaration |

#### Mutability and Ownership

| Error                                                                                          | Cause                                                                    |
| ---------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| `cannot assign to immutable variable 'x'`                                                      | Writing to a non-`mutable` binding                                       |
| `cannot assign to borrowed variable 'x' — borrow variables are read-only`                      | Assignment to a borrow reference                                         |
| `cannot assign to consumed unique variable 'x'`                                                | Assignment to an already-consumed unique value                           |
| `cannot assign to field of immutable variable 'x'`                                             | Field mutation on a const variable                                       |
| `cannot assign to field of borrowed variable 'x' — borrow variables are read-only`             | Field mutation on a borrow reference                                     |
| `cannot assign to element of immutable variable 'x'`                                           | Element mutation on a const variable                                     |
| `cannot assign to element of borrowed variable 'x' — borrow variables are read-only`           | Element mutation on a borrow reference                                   |
| `cannot compound-assign to immutable variable 'x'`                                             | `+=` etc. on a non-`mutable` binding                                     |
| `cannot compound-assign to borrowed variable 'x' — borrow variables are read-only`             | `+=` etc. on a borrow reference                                          |
| `cannot compound-assign to field of immutable variable 'x'`                                    | Field compound assignment on a const variable                            |
| `cannot compound-assign to field of borrowed variable 'x' — borrow variables are read-only`    | Field compound assignment on a borrow reference                          |
| `cannot compound-assign to element of borrowed variable 'x' — borrow variables are read-only`  | Element compound assignment on a borrow reference                        |
| `cannot increment immutable variable 'x'`                                                      | `++` on a non-`mutable` binding                                          |
| `cannot increment borrowed variable 'x' — borrow variables are read-only`                      | `++` on a borrow reference                                               |
| `cannot decrement immutable variable 'x'`                                                      | `--` on a non-`mutable` binding                                          |
| `cannot decrement borrowed variable 'x' — borrow variables are read-only`                      | `--` on a borrow reference                                               |
| `cannot pass borrowed variable 'x' to unique parameter 'p' — borrow values cannot be consumed` | Borrowed value passed to a consuming parameter                           |
| `cannot pipe borrowed variable 'x' to unique parameter 'p' of 'f'`                             | Borrowed value piped to a consuming parameter                            |
| `cannot consume unique variable 'x' inside a loop — it would be consumed on every iteration`   | Unique value consumed in a loop body                                     |
| `use of consumed unique variable 'x'`                                                          | Unique value used after being consumed                                   |
| `invalid assignment target`                                                                    | Target is not a variable, element, or field                              |
| `self-assignment has no effect`                                                                | Assigning a variable to itself (warning promoted to error by `--strict`) |

#### Type Mismatches

| Error                                                                                   | Cause                                                 |
| --------------------------------------------------------------------------------------- | ----------------------------------------------------- |
| `type mismatch: cannot assign 'T' to variable of type 'U'`                              | Value type incompatible with declared type            |
| `type mismatch in assignment: cannot assign 'T' to 'U'`                                 | Assigned value has wrong type                         |
| `argument N type mismatch: expected 'T', got 'U'`                                       | Function argument has wrong type                      |
| `wrong number of arguments: expected N, got M`                                          | Too many or too few arguments                         |
| `wrong number of arguments: expected N-M, got K`                                        | Argument count outside the valid range                |
| `named argument 'name' type mismatch: expected 'T', got 'U'`                            | Named argument has wrong type                         |
| `return type mismatch: expected 'T', got 'U'`                                           | Function body returns wrong type                      |
| `return without value in function expecting 'T'`                                        | Bare `return` in a typed function                     |
| `lambda body type 'T' does not match declared return type 'U'`                          | Lambda body type does not match its annotation        |
| `if branches have different types: 'T' and 'U'`                                         | Branches of an expression-`if` return different types |
| `match arms have different types: 'T' and 'U'`                                          | Arms of an expression-`match` return different types  |
| `tuple destructuring: expected N elements, got M`                                       | Right-hand tuple has wrong element count              |
| `tuple destructuring: cannot assign 'T' to 'U'`                                         | Tuple element type mismatch                           |
| `default value type 'T' does not match parameter type 'U'`                              | Default parameter value has wrong type                |
| `default value for field 'f' in record 'R' has type 'T' which is not assignable to 'U'` | Record field default value has wrong type             |
| `field 'f': expected 'T', got 'U'`                                                      | Record field value has wrong type                     |

#### Operators

| Error                                                                         | Cause                                            |
| ----------------------------------------------------------------------------- | ------------------------------------------------ |
| `operator '+' requires numeric or string operands, got 'T' and 'U'`           | Invalid operand types for addition               |
| `operator '*' requires numeric operands or string * integer, got 'T' and 'U'` | Invalid operand types for multiplication         |
| `arithmetic operator requires numeric operands, got 'T' and 'U'`              | Invalid operand types for arithmetic             |
| `operator '//' requires integer operands, got 'T' and 'U'`                    | Invalid operand types for integer division       |
| `operator 'op' requires integer operands, got 'T' and 'U'`                    | Invalid operand types for bitwise operator       |
| `comparison operator requires numeric or string operands, got 'T' and 'U'`    | Invalid operand types for comparison             |
| `logical operator requires boolean operands, got 'T'`                         | Invalid operand for logical `and` / `or`         |
| `logical NOT requires boolean operand, got 'T'`                               | Invalid operand for `not`                        |
| `unary minus requires numeric operand, got 'T'`                               | Invalid operand for unary `-`                    |
| `bitwise NOT '~' requires an integer operand, got 'T'`                        | Invalid operand for `~`                          |
| `'in' on dictionary requires a string key, got 'T'`                           | Non-string key in `x in dict`                    |
| `'in' on string requires a string operand, got 'T'`                           | Non-string value in `x in str`                   |
| `'in' on a range requires an integer, got 'T'`                                | Non-integer value in `x in a..=b`                |
| `'in' operator requires array, dictionary, string, or range on the right, got 'T'` | Invalid right operand for `in`              |
| `compound assignment 'op' requires integer type, got 'T'`                     | Bitwise compound assignment on non-integer       |
| `compound assignment 'op' requires integer value, got 'T'`                    | Bitwise compound assignment value is non-integer |
| `compound assignment requires numeric type, got 'T'`                          | Numeric compound assignment on non-numeric       |
| `compound assignment requires numeric value, got 'T'`                         | Numeric compound assignment value is non-numeric |
| `increment requires a variable`                                               | `++` on a non-variable expression                |
| `increment requires numeric type, got 'T'`                                    | `++` on a non-numeric variable                   |
| `decrement requires a variable`                                               | `--` on a non-variable expression                |
| `decrement requires numeric type, got 'T'`                                    | `--` on a non-numeric variable                   |

#### Compile-Time Arithmetic

| Error                                      | Cause                                              |
| ------------------------------------------ | -------------------------------------------------- |
| `division by zero`                         | Literal zero divisor detected at compile time      |
| `integer overflow in addition`             | Constant integer addition exceeds `int64_t` bounds |
| `integer overflow in subtraction`          | Constant integer subtraction exceeds bounds        |
| `integer overflow in multiplication`       | Constant integer multiplication exceeds bounds     |
| `integer overflow in division`             | Constant integer division exceeds bounds           |
| `integer overflow in modulo`               | Constant integer modulo exceeds bounds             |
| `integer overflow in negation`             | Negation of `INT64_MIN` exceeds bounds             |
| `shift amount out of range`                | Shift amount outside `0..63`                       |
| `string repeat count must be non-negative` | Negative string repeat count                       |
| `string repeat count exceeds maximum`      | String repeat count too large                      |

#### Records, Choices, Interfaces, and Namespaces

| Error                                                                   | Cause                                            |
| ----------------------------------------------------------------------- | ------------------------------------------------ |
| `unknown record type 'T'`                                               | Record constructor uses unknown type name        |
| `record 'R' has no field 'f'`                                           | Field not declared in record type                |
| `missing field 'f' in record creation of 'R'`                           | Required field omitted in record creation        |
| `unknown field 'f' in record 'R'`                                       | Extra field not declared in record type          |
| `choice 'C' has no variant 'V'`                                         | Unknown variant used in choice type or match     |
| `choice variant 'C.V' has N field(s) but M binding(s) provided`         | Binding count mismatch in match pattern          |
| `interface 'I' has no field 'f'`                                        | Field not declared in interface                  |
| `'x' is internal to namespace 'Ns' and cannot be accessed from outside` | Accessing an `internal` member externally        |
| `namespace 'Ns' has no member 'm'`                                      | Member not found in namespace                    |
| `'with' expression requires a record value`                             | `with` used on a non-record value                |
| `'with' expression: record 'R' has no field 'f'`                        | Unknown field in `with` expression               |
| `unknown type 'T' for field 'f' in variant 'V' of choice 'C'`           | Unknown field type in choice variant declaration |
| `unknown type 'T' in type alias 'A'`                                    | Unknown type in type alias definition            |

#### Collections and Indexing

| Error                                                                     | Cause                                         |
| ------------------------------------------------------------------------- | --------------------------------------------- |
| `array elements must have the same type: first is 'T', element N is 'U'`  | Heterogeneous array literal                   |
| `dictionary keys must be strings, got 'T'`                                | Non-string dictionary key in literal          |
| `dictionary values must have the same type: first is 'T', entry N is 'U'` | Heterogeneous dictionary values in literal    |
| `tuples must have 2 to 4 elements, got N`                                 | Tuple literal has fewer than 2 or more than 4 |
| `array index must be integer or range, got 'T'`                           | Invalid array index type                      |
| `index N out of bounds for array of length M`                             | Constant array index out of range             |
| `dictionary key must be string, got 'T'`                                  | Non-string key in dictionary access           |
| `tuple index N out of bounds (tuple has M elements)`                      | Constant tuple field access out of range      |
| `index N out of bounds for tuple of length M`                             | Constant tuple index out of range             |
| `index access requires an array, string, dictionary, or tuple, got 'T'`   | Indexing on a non-indexable type              |

#### Control Flow

| Error                                                                                                                                  | Cause                                |
| -------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------ |
| `'break' is only allowed inside a loop`                                                                                                | `break` outside a loop               |
| `'continue' is only allowed inside a loop`                                                                                             | `continue` outside a loop            |
| `return statement outside of function`                                                                                                 | `return` at top level                |
| `if condition must be boolean, got 'T'`                                                                                                | Non-boolean `if` condition           |
| `while condition must be boolean, got 'T'`                                                                                             | Non-boolean `while` condition        |
| `for loop requires an iterable (array, range, string, or dictionary), got 'T'`                                                         | Non-iterable value in `for` loop     |
| `for over a dictionary requires two loop variables — use 'for key, value in dict', 'Dictionary.keys(d)', or 'Dictionary.values(d)'...` | Single variable for dictionary `for` |

#### Match Exhaustiveness

| Error                                                     | Cause                                |
| --------------------------------------------------------- | ------------------------------------ |
| `match must have at least one arm`                        | Empty match statement                |
| `match on boolean must cover both 'true' and 'false'`     | Non-exhaustive boolean match         |
| `match on choice 'C' is missing variant 'V'`              | Non-exhaustive choice match          |
| `match on result must cover both 'success' and 'failure'` | Non-exhaustive result match          |
| `match on optional must cover both 'some' and 'none'`     | Non-exhaustive optional match        |
| `match with comparison arms must include an 'else' arm`   | Comparison match without a catch-all |
| `match guard: expected boolean, got 'T'`                  | A `when` guard is not a boolean      |

#### Functions and Declarations

| Error                                                                                  | Cause                                                                                  |
| -------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| `function 'f' declares return type 'T' but may fall through without returning a value` | Function body has a code path with no `return`                                         |
| `@main function must take no parameters`                                               | `@main` function declares parameters                                                   |
| `@test function must take no parameters`                                               | `@test` function declares parameters                                                   |
| `multiple @main functions found; exactly one is required`                              | More than one `@main` in a program (TypeError under `--check`; RuntimeError otherwise) |
| `no @main function found`                                                              | File has no `@main` function (not raised for `--test` files)                           |
| `recursive type alias: 'T'`                                                            | Self-referential or mutually recursive `type` alias                                    |
| `type parameter 'P' bound violation: 'T' does not satisfy interface 'I'`               | Generic type argument does not satisfy the interface bound                             |

#### Generics, Pipes, and Error Handling

| Error                                                                                                                   | Cause                                                |
| ----------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| `argument 1 type mismatch: expected 'U', got 'T'`                                                                       | Value piped (`\|>`) into a function is incompatible with its first parameter |
| `pipe operator requires a function call on the right side`                                                              | Right side of `\                                     |
| `error pipe '!>' requires a function call on the right side`                                                            | Right side of `!>` is not a function call            |
| `error propagation '?' can only be used inside a function that returns 'result<T>' or 'optional<T>', or inside '@main'` | `?` used in a function not returning result/optional |
| `propagation '?' requires result or optional type, got 'T'`                                                             | `?` applied to a non-result, non-optional value      |
| `import conflict: 'x' is already defined; use 'Ns.x' to access it`                                                      | `use Namespace` conflicts with a user-defined name   |

#### Concurrency and Miscellaneous

| Error                                                     | Cause                                    |
| --------------------------------------------------------- | ---------------------------------------- |
| `spawn requires a function call expression`               | `spawn` applied to a non-call expression |
| `await requires a task value, got 'T'`                    | `await` applied to a non-task value      |
| `cannot wrap 'none' in 'some'; use none literal directly` | `some(none)` is meaningless              |
| `range start must be integer, got 'T'`                    | Non-integer range start                  |
| `range end must be integer, got 'T'`                      | Non-integer range end                    |
| `unknown type 'T' in downcast`                            | Unknown target type in `downcast<T>`     |
| `unknown type 'T' in is<T>`                               | Unknown target type in `is<T>`           |

### Type Warnings

Warnings do not prevent execution but indicate likely programmer mistakes.

| Warning                                                                                  | Cause                                                                                           |
| ---------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `assigning result of void function to a variable has no effect`                          | A variable is assigned the return value of a `void` function                                    |
| `comparing floating-point numbers with '==' can give unexpected results due to rounding` | Floating-point equality check — consider a tolerance                                            |
| `comparison 'op' between incompatible types 'T' and 'U' — result is always false`        | `==` or `!=` applied to values of unrelated types                                               |
| `condition is always false; loop body will never execute`                                | `while false { ... }` — loop body is dead code                                                  |
| `downcast from 'T' to 'U' will always fail at runtime`                                   | `downcast<T>` target type is incompatible with the value                                        |
| `function 'f' is declared but never called`                                              | Function is defined but never used — prefix with `_` to suppress                                |
| `interpolating a function value will produce '<function ...>'`                           | String interpolation of a function — call it with `()` instead                                  |
| `interpolating a namespace value is unlikely to be useful`                               | String interpolation of a namespace module                                                      |
| `optional chain result not unwrapped`                                                    | `?.` or `?[` result assigned to a non-`optional<T>` variable without a `??` fallback            |
| `parameter 'p' is declared mutable but is never mutated`                                 | `mutable` parameter is never reassigned — remove the keyword                                    |
| `redundant comparison of boolean with 'true' or 'false'`                                 | Comparing a boolean with a literal — use the boolean directly                                   |
| `redundant downcast: value is already of type 'T'`                                       | `downcast<T>` on a value already known to be `T`                                                |
| `self-assignment has no effect`                                                          | Assigning a variable to itself does nothing                                                     |
| `spawn outside task_scope — task runs unstructured (fire-and-forget)`                    | `spawn` used outside a `task_scope` block — wrap in `task_scope { }` for structured concurrency |
| `'name' shadows an outer variable`                                                       | A local variable shadows a variable from an outer scope                                         |
| `'trusted_downcast' on unrefined stdlib value has no compile-time safety guarantee`      | Use `downcast<T>` with a match instead of `trusted_downcast`                                    |
| `unique variable 'x' was never consumed`                                                 | A `unique` variable leaves scope without being consumed                                         |
| `unreachable code after return statement`                                                | Code after `return` will never execute                                                          |
| `unreachable code after break statement`                                                 | Code after `break` will never execute                                                           |
| `unreachable code after continue statement`                                              | Code after `continue` will never execute                                                        |
| `unused parameter 'p'`                                                                   | A function parameter is never used — prefix with `_` to suppress                                |
| `unused result: the result<T> value is silently discarded`                               | A call returns `result<T>` and the value is not used — suppress with `_ = expr`                 |
| `discarded value: the 'T' return value is unused`                                        | A non-void function call whose return value is not consumed — suppress with `_ = expr`          |
| `unused variable 'x'`                                                                    | A variable is declared but never read — prefix with `_` to suppress                             |
| `variable 'x' is declared mutable but is never mutated`                                  | `mutable` variable is never reassigned — remove the keyword                                     |

### Compile Errors

Include resolution errors. Execution does not start.

| Error                                                   | Cause                                                        |
| ------------------------------------------------------- | ------------------------------------------------------------ |
| `include depth limit exceeded (N) while including 'path'` | Includes are nested more than 64 levels deep                 |
| `include rejected: 'path' contains directory traversal` | Include path uses `..` to escape the source directory        |
| `include rejected: 'path' is a symbolic link`           | Symbolic links are not allowed in include paths for security |

### Runtime Errors

| Error                                                                         | Cause                                                                                               |
| ----------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| `assertion failed`                                                            | `assert(false)` (custom message if provided)                                                        |
| `await called on an already-consumed task`                                    | Awaiting a task that has already been awaited — each task can only be awaited once                  |
| `cannot assign to immutable variable 'x'`                                     | Assigning to a non-`mutable` binding                                                                |
| `cannot resolve import '...'`                                                 | `use Namespace.name` does not resolve                                                               |
| `division by zero`                                                            | Integer or number `/ 0`, `// 0`, or `% 0`                                                           |
| `index N out of bounds`                                                       | Array element `[N]` is outside `[0, length)`                                                        |
| `integer overflow in addition` / `subtraction` / `multiplication`             | Arithmetic result does not fit in `int64_t`                                                         |
| `integer overflow in decrement`                                               | `i--` when `i` equals `-9223372036854775808` (minimum `integer`)                                    |
| `integer overflow in increment`                                               | `i++` when `i` equals `9223372036854775807` (maximum `integer`)                                     |
| `maximum recursion depth (256) exceeded`                                      | Call stack exceeds the maximum recursion depth                                                     |
| `non-exhaustive match: no arm matched value V`                                | No `match` arm matched the subject value                                                            |
| `number overflow in addition` / `subtraction` / `multiplication` / `division` | `number` arithmetic with finite inputs produced `±Infinity`                                         |
| `range bounds must be integers`                                               | `start..end` expression uses non-integer bounds                                                     |
| `shift amount out of range`                                                   | `<<` or `>>` with a shift amount outside `0..63`                                                    |
| `spawn requires a function call expression`                                   | `spawn` applied to a non-call expression at runtime                                                 |
| `string concatenation exceeds maximum size`                                   | String `+` result exceeds the size limit                                                            |
| `string repeat count must be non-negative`                                    | `str * n` with `n < 0`                                                                              |
| `task queue is full — too many pending tasks`                                 | More pending tasks than the task-queue limit allows                                                 |
| `trusted_downcast failed: value is not of type 'T'`                           | `trusted_downcast<T>` used on a value of wrong type                                                 |
| `tuple destructuring: expected N elements, got M`                             | Right-hand tuple has wrong element count                                                            |
| `undefined variable 'x'`                                                      | `x` is not in scope at runtime                                                                      |
| `unknown named argument 'name'`                                               | Named argument matches no parameter of the called function                                          |
| `unwrap called on fail: msg`                                                  | `Result.unwrap` on a `failure` value (location prefix added when `failure()` was written in source) |
| `variable 'x' is already defined in this scope`                               | Re-declaring a name in the same block                                                               |
| `no @main function found`                                                     | File has no `@main` function (also a TypeError under `--check`)                                     |
| `while loop exceeded maximum iteration limit (1000000000)`                    | A `while` loop executed more than 1 billion iterations                                              |

**Note:** `downcast<T>` never throws a RuntimeError — it returns `failure("downcast failed: value is not of type 'T'")` as a `result<T>` value. `trusted_downcast<T>` **does** throw a RuntimeError when the type does not match.

**Note:** The numeric limits referenced in these messages (such as recursion depth and pending-task count) are documented canonically in the [resource-limit table](Luma_Performance_Guide.md#6--resource-limits), along with their `LUMA_LIMIT_*` environment-variable overrides.

---

## 30 — Complete Programs

### 30.1 FizzBuzz

```luma
@main
function void main() {
    for i in 1..101 {
        if i % 15 == 0     { print("FizzBuzz") }
        else if i % 3 == 0 { print("Fizz") }
        else if i % 5 == 0 { print("Buzz") }
        else               { print(i) }
    }
}
```

### 30.2 Fibonacci

```luma
function integer fib(integer n) {
    if n <= 1 { return n }

    return fib(n - 1) + fib(n - 2)
}

@main
function void main() {
    for i in 0..11 {
        print("fib(${i}) = ${fib(i)}")
    }
}
```

### 30.3 Word Frequency Counter

```luma
@main
function void main() {
    string text = "the quick brown fox jumps over the lazy dog the fox"

    array<string> words = String.split(text, " ")

    mutable dictionary<number> freq = {}

    for word in words {
        number count = Dictionary.get_or(freq, word, 0)

        freq = Dictionary.set(freq, word, count + 1)
    }

    for key in Dictionary.keys(freq) {
        number n = Dictionary.get_or(freq, key, 0)

        print("${key}: ${n}")
    }
}
```

### 30.4 CSV Parser with Validation

```luma
record Contact {
    string name,
    string email,
    number age
}

function result<Contact> parse_contact(string line) {
    array<string> parts = String.split(line, ",")

    if Array.length(parts) != 3 {
        return failure("expected 3 fields, got ${Array.length(parts)}")
    }

    string name  = String.trim(parts[0])
    string email = String.trim(parts[1])
    number age   = String.parse_number(String.trim(parts[2]))?

    return success(Contact { name = name, email = email, age = age })
}

@main
function void main() {
    string csv = """
        Alice, alice@example.com, 30
        Bob, not-an-email, 25
        Carol, carol@example.com, abc
        Dave, dave@example.com, 40
        """

    array<string> lines = String.split(String.trim(csv), "\n")

    array<string> rows = Array.filter(lines, (string s) -> !String.is_empty(String.trim(s)))

    for row in rows {
        match parse_contact(row) {
            success(c) { print("OK: ${c.name} <${c.email}> age ${c.age}") }
            failure(m) { print("ERR: ${m}") }
        }
    }
}
```

### 30.5 Pipeline Data Transformation

```luma
record Product {
    string name,
    number price,
    integer stock
}

@main
function void main() {
    array<Product> catalog = [
        Product { name = "Widget",  price = 9.99,  stock = 100 },
        Product { name = "Gadget",  price = 24.99, stock = 0   },
        Product { name = "Doohick", price = 4.99,  stock = 250 },
        Product { name = "Thingum", price = 49.99, stock = 12  },
        Product { name = "Whatsit", price = 14.99, stock = 75  }
    ]

    array<string> display = catalog
        |> Array.filter((Product p) -> p.stock > 0)
        |> Result.unwrap_or([])
        |> Array.filter((Product p) -> p.price < 20)
        |> Result.unwrap_or([])
        |> Array.sort((Product a, Product b) -> a.price - b.price)
        |> Result.unwrap_or([])
        |> Array.map((Product p) -> "${String.pad_right(p.name, 10)} $${String.format_number(p.price, 2)}")
        |> Result.unwrap_or([])

    print("Available products under $20:")

    for line in display {
        print("  ${line}")
    }
}
```

### 30.6 Grade Calculator with Match Expressions

```luma
function string letter_grade(integer score) {
    return match score {
        case >= 90 { "A" }
        case >= 80 { "B" }
        case >= 70 { "C" }
        case >= 60 { "D" }
        else       { "F" }
    }
}

@test
function void test_grades() {
    assert(letter_grade(95) == "A")
    assert(letter_grade(82) == "B")
    assert(letter_grade(55) == "F")
    assert(letter_grade(70) == "C")
}

@main
function void main() {
    array<integer> scores = [95, 82, 70, 55, 88, 63]

    for score in scores {
        print("${score} → ${letter_grade(score)}")
    }
}
```

### 30.7 Error Handling with Result

```luma
function result<string> fetch_user(integer id) {
    if id <= 0 { return failure("invalid id: ${id}") }

    return success("user-${id}")
}

@test
function void test_fetch_valid() {
    result<string> r = fetch_user(42)

    assert(Result.is_success(r))
    assert(Result.unwrap(r) == "user-42")
}

@test
function void test_fetch_invalid() {
    result<string> r = fetch_user(-1)

    assert(!Result.is_success(r))
}

@main
function void main() {
    array<integer> ids = [1, 2, -1, 3, 0, 4]

    for id in ids {
        match fetch_user(id) {
            success(name) { print("Fetched: ${name}") }
            failure(msg)  { print("Error: ${msg}") }
        }
    }
}
```

---

## 31 — Debugging

Luma includes a Debug Adapter Protocol (DAP) server — `luma_dap` — that enables interactive debugging in any DAP-capable editor (VS Code, Zed, etc.).

### Capabilities

| Feature              | Description                                               |
| -------------------- | --------------------------------------------------------- |
| Breakpoints          | Set breakpoints by source file and line number            |
| Step In / Over / Out | Step through code one statement at a time                 |
| Pause / Continue     | Pause a running program or resume execution               |
| Stack Traces         | View the call stack with file names and line numbers      |
| Local Variables      | Inspect local variables in any stack frame                |
| Expression Eval      | Evaluate expressions in the context of the current frame  |
| Output Capture       | `print` output is forwarded to the editor's debug console |
| Stop on Entry        | Optionally pause at the first statement of `@main`        |

### Usage

The editor spawns `luma_dap` as a child process and communicates over standard input/output using Content-Length framed JSON messages. You do not run `luma_dap` directly — your editor launches it automatically when you start a debug session.

For VS Code, add a `launch.json` entry as shown in the [Setup guide](Luma_Setup.md#8--editor-integration); `luma_dap` is launched automatically when you start a debug session.

---

## 32 — Formal Grammar (EBNF)

This appendix gives the complete formal grammar of Luma in Extended Backus–Naur Form (EBNF). The notation used is:

| Notation     | Meaning                                               |
| ------------ | ----------------------------------------------------- |
| `=`          | Defines a production rule                             |
| `;`          | Ends a production rule                                |
| `\           | `                                                     |
| `{ ... }`    | Zero or more repetitions                              |
| `[ ... ]`    | Optional (zero or one)                                |
| `( ... )`    | Grouping                                              |
| `"..."`      | Terminal string (keyword or symbol)                   |
| `UPPER_CASE` | Lexical token defined in the Lexical Elements section |

### 32.1 Lexical Elements

```ebnf
LETTER        = "a" | ... | "z" | "A" | ... | "Z" | "_" ;
DIGIT         = "0" | ... | "9" ;
HEX_DIGIT     = DIGIT | "a" | ... | "f" | "A" | ... | "F" ;

IDENTIFIER    = LETTER { LETTER | DIGIT } ;
INTEGER       = DIGIT { DIGIT }
              | "0x" HEX_DIGIT { HEX_DIGIT }
              | "0b" ( "0" | "1" ) { "0" | "1" }
              | "0o" ("0"|...|"7") { "0"|...|"7" } ;
NUMBER        = DIGIT { DIGIT } "." DIGIT { DIGIT }
              | DIGIT { DIGIT } ( "e" | "E" ) [ "+" | "-" ] DIGIT { DIGIT }
              | DIGIT { DIGIT } "." DIGIT { DIGIT } ( "e" | "E" ) [ "+" | "-" ] DIGIT { DIGIT } ;
BOOLEAN       = "true" | "false" ;
NONE          = "none" ;

STRING        = '"' { char | escape_sequence } '"'
              | '"""' { char | escape_sequence | NEWLINE } '"""' ;
INTERP_STRING = '"' { char } "${"  (* starts interpolation *)
              | "}" { char } "${"  (* continues interpolation *)
              | "}" { char } '"' ; (* ends interpolation *)
              (* triple-quoted strings also support interpolation with the same ${ } syntax *)

ANNOTATION    = "@" IDENTIFIER ;
COMMENT       = "#" { char } NEWLINE ;
```

### 32.2 Program Structure

```ebnf
program        = { top_level_item } ;

top_level_item = declaration
               | statement ;

declaration    = annotated_function
               | function_decl
               | record_decl
               | choice_decl
               | interface_decl
               | namespace_decl
               | type_alias_decl
               | include_decl
               | use_decl ;
```

### 32.3 Type Annotations

```ebnf
type_annotation = [ "unique" | "borrow" ] base_type ;

base_type       = primitive_type
                | generic_type
                | tuple_type
                | function_type
                | qualified_type ;

primitive_type  = "boolean" | "integer" | "number" | "decimal" | "string"
                | "none" | "optional" ;
                (* container/handle type names — queue, stack, set, task,
                   channel, socket, xml, reference, decimal,
                   key_value_store, widget — are ordinary
                   IDENTIFIERs resolved as built-in types via generic_type /
                   qualified_type, not reserved primitive keywords. *)

generic_type    = ( "array" | "dictionary" | "result" | "optional" | IDENTIFIER )
                  "<" type_annotation { "," type_annotation } ">" ;

tuple_type      = "(" type_annotation "," type_annotation
                      { "," type_annotation } ")" ;

function_type   = "function" "(" [ type_annotation { "," type_annotation } ] ")"
                  "->" type_annotation ;

qualified_type  = IDENTIFIER { "." IDENTIFIER }
                  [ "<" type_annotation { "," type_annotation } ">" ] ;

type_param_list = "<" type_param { "," type_param } ">" ;
type_param      = IDENTIFIER [ ":" IDENTIFIER { "+" IDENTIFIER } ] ;
```

### 32.4 Declarations

```ebnf
annotated_function = ANNOTATION function_decl ;

function_decl      = "function" [ type_param_list ]
                     type_annotation IDENTIFIER
                     "(" [ parameter_list ] ")"
                     "{" block_body "}" ;

parameter_list     = parameter { "," parameter } ;
parameter          = [ "mutable" ] type_annotation IDENTIFIER
                     [ "=" expression ] ;

record_decl        = "record" IDENTIFIER [ type_param_list ]
                     "{" [ record_field { "," record_field } [ "," ] ] "}" ;

record_field       = type_annotation IDENTIFIER [ "=" expression ] ;

choice_decl        = "choice" IDENTIFIER [ type_param_list ]
                     "{" [ choice_variant { [ "," ] choice_variant } ] "}" ;

choice_variant     = IDENTIFIER
                     [ "(" parameter { "," parameter } ")" ] ;

interface_decl     = "interface" IDENTIFIER [ type_param_list ]
                     "{" [ interface_field { "," interface_field } [ "," ] ] "}" ;

interface_field    = type_annotation IDENTIFIER ;

namespace_decl     = "namespace" IDENTIFIER
                     "{" { [ "internal" ] declaration } "}" ;

type_alias_decl    = "type" IDENTIFIER [ type_param_list ]
                     "=" type_annotation ;

include_decl       = "include" STRING ;

use_decl           = "use" IDENTIFIER { "." IDENTIFIER } ;
```

### 32.5 Statements

```ebnf
block_body          = { statement } ;

statement           = variable_decl
                   | mutable_decl
                   | tuple_destructuring
                   | return_stmt
                   | for_stmt
                   | while_stmt
                   | try_stmt
                   | if_stmt
                   | match_stmt
                   | "break"
                   | "continue"
                   | expression_stmt ;

variable_decl       = type_annotation IDENTIFIER "=" expression ;
mutable_decl        = "mutable" ( variable_decl | tuple_destructuring ) ;

tuple_destructuring = "(" binding { "," binding } ")" "=" expression ;
binding             = type_annotation IDENTIFIER ;

return_stmt         = "return" [ expression ] ;

for_stmt            = "for" [ IDENTIFIER "," ] IDENTIFIER "in" expression
                      "{" block_body "}" ;
                      (* with comma: first IDENTIFIER is the index variable, second is the value *)

while_stmt          = "while" expression "{" block_body "}" ;

try_stmt            = "try" "{" block_body "}"
                      [ "catch" "(" IDENTIFIER ")" "{" block_body "}" ]
                      [ "finally" "{" block_body "}" ] ;
                      (* at least one of catch or finally must be present *)

if_stmt             = "if" expression "{" block_body "}"
                      [ "else" ( if_stmt | "{" block_body "}" ) ] ;

match_stmt          = "match" expression "{" { match_arm } "}" ;

expression_stmt     = expression
                      ( "=" expression
                      | compound_op expression
                      | "++"
                      | "--"
                      | (* empty — bare expression *) ) ;

compound_op         = "+=" | "-=" | "*=" | "/=" | "//=" | "%=" ;
```

### 32.6 Match Arms

```ebnf
match_arm       = result_arm | case_arm ;

result_arm      = ( "success" "(" IDENTIFIER ")"
                  | "failure" "(" IDENTIFIER ")" )
                  "{" block_body "}" ;

case_arm        = ( "else"
                  | "case" case_pattern { "|" alt_pattern } )
                  "{" block_body "}" ;

case_pattern    = BOOLEAN
                | INTEGER
                | NONE
                | "some" "(" IDENTIFIER ")"
                | STRING
                | comparison_op addition_expr
                | variant_pattern ;
                (* a bare literal after "==" (e.g. `case == 1`) is rejected —
                   match literals by value with the bare form `case 1`. *)

alt_pattern     = BOOLEAN
                | INTEGER
                | NONE
                | STRING
                | comparison_op addition_expr
                | [ IDENTIFIER "." ] IDENTIFIER "." IDENTIFIER ;

variant_pattern = [ IDENTIFIER "." ] IDENTIFIER "." IDENTIFIER
                  [ "(" IDENTIFIER { "," IDENTIFIER } ")" ] ;

comparison_op   = "==" | "!=" | "<" | ">" | "<=" | ">=" ;
```

### 32.7 Expressions

Expressions are listed from lowest to highest precedence.

```ebnf
expression           = pipe_expr [ "with" "{" field_override { "," field_override } "}" ] ;

field_override       = IDENTIFIER "=" expression ;

pipe_expr            = null_coalescing_expr
                       { ( "|>" | "!>" ) null_coalescing_expr } ;

null_coalescing_expr = or_expr { "??" or_expr } ;

or_expr              = and_expr { "||" and_expr } ;

and_expr             = equality_expr { "&&" equality_expr } ;

equality_expr        = comparison_expr { ( "==" | "!=" ) comparison_expr } ;

comparison_expr      = addition_expr
                       { ( "<" | ">" | "<=" | ">=" | "in" ) addition_expr } ;

addition_expr        = multiplication_expr { ( "+" | "-" ) multiplication_expr } ;

multiplication_expr  = unary_expr { ( "*" | "/" | "//" | "%" ) unary_expr } ;

unary_expr           = ( "!" | "-" ) unary_expr
                     | postfix_expr ;

postfix_expr         = primary_expr
                       { postfix_op } ;

postfix_op           = "." field_name
                     | "?." field_name
                     | "[" expression "]"
                     | "?[" expression "]"
                     | "(" argument_list ")"
                     | "::" "<" type_annotation { "," type_annotation } ">"
                       "(" argument_list ")"
                     | ".." addition_expr
                     | "..=" addition_expr
                     | "?" ;

field_name           = IDENTIFIER | INTEGER ;   (* e.g. record field or tuple.0 *)

argument_list        = [ positional_arg { "," positional_arg }
                         { "," named_arg } ]
                     | [ named_arg { "," named_arg } ] ;
positional_arg       = expression ;
named_arg            = IDENTIFIER ":" expression ;

primary_expr         = INTEGER
                     | NUMBER
                     | BOOLEAN
                     | NONE
                     | STRING
                     | interp_string
                     | "success" "(" expression ")"
                     | "failure" "(" expression ")"
                     | "some" "(" expression ")"
                     | "downcast" "<" type_annotation ">" "(" expression ")"
                     | "trusted_downcast" "<" type_annotation ">" "(" expression ")"
                     | "is" "<" type_annotation ">" "(" expression ")"
                     | "spawn" postfix_expr
                     | "await" unary_expr
                     | task_scope_expr
                     | if_expr
                     | match_expr
                     | array_literal
                     | dict_literal
                     | lambda_expr
                     | tuple_literal
                     | "(" expression ")"
                     | record_creation
                     | IDENTIFIER ;

task_scope_expr      = "task_scope" "{" block_body "}" ;

interp_string        = STRING_START ( "${" expression "}" STRING_MIDDLE )*
                                 "${" expression "}" STRING_END ;
                       (* STRING_START contains the text before the first interpolation;
                          STRING_MIDDLE contains text between two interpolations;
                          STRING_END contains the text after the last interpolation *)

if_expr              = "if" expression "{" if_body "}"
                       "else" ( if_expr | "{" if_body "}" ) ;
if_body              = expression | block_body ;

match_expr           = "match" expression "{" { match_arm } "}" ;

array_literal        = "[" [ expression { "," expression } [ "," ] ] "]" ;

dict_literal         = "{" "}"
                     | "{" STRING ":" expression { "," STRING ":" expression } "}" ;

lambda_expr          = "(" [ parameter_list ] ")" "->"
                       ( "{" block_body "}" | expression ) ;

tuple_literal        = "(" expression "," expression { "," expression } ")" ;

record_creation      = [ IDENTIFIER "." ] IDENTIFIER
                       [ "<" type_annotation { "," type_annotation } ">" ]
                       "{" [ field_init { "," field_init } [ "," ] ] "}" ;

field_init           = IDENTIFIER "=" expression ;
```

### 32.8 Operator Precedence Summary

From lowest to highest:

| Level | Operators                                 | Associativity  |
| ----- | ----------------------------------------- | -------------- |
| 1     | `with { ... }` (record update)            | Postfix        |
| 2     | `\                                        | >` `!>` (pipe) |
| 3     | `??` (null coalescing)                    | Left           |
| 4     | `\                                        | \              |
| 5     | `&&` (logical and)                        | Left           |
| 6     | `==` `!=`                                 | Left           |
| 7     | `<` `>` `<=` `>=` `in`                    | Left           |
| 8     | `+` `-`                                   | Left           |
| 9     | `*` `/` `//` `%`                          | Left           |
| 10    | `!` `-` (unary prefix)                    | Right          |
| 11    | `.` `?.` `[…]` `?[…]` `()` `..` `..=` `?` | Left           |

---

## See Also

- [Tutorial](Luma_Tutorial.md) — a gentle, step-by-step introduction for newcomers to programming
- [Standard Library Reference](Luma_Standard_Library_Reference.md) — complete API for every built-in module
- [Error Handling](Luma_Error_Handling.md) — error conventions, recovery, and library policy
- [Coding Guidelines](Luma_Coding_Guidelines.md) — idiomatic Luma style and conventions
- [Performance Guide](Luma_Performance_Guide.md) — runtime performance characteristics
- [REPL Guide](Luma_REPL_Guide.md) — interactive exploration of the language
- [Solaris Guide](Luma_Solaris_Guide.md) — building graphical applications the beginner-first way
- [GraphicalUi Guide](Luma_GraphicalUi_Guide.md) — the low-level webview GUI engine beneath Solaris
- [Concurrent Debugging Guide](Luma_Concurrent_Debugging_Guide.md) — debugging tasks and channels
- [Software Architecture](Luma_Software_Architecture.md) — how the interpreter executes the language
- [Initial Concept](Luma_Initial_Concept.md) — the original design goals and motivation
- [Documentation Index](README.md) — index of all Luma documentation
