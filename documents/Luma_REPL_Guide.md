# Luma — REPL Guide

> Interactive exploration of Luma — evaluate expressions, define functions, and experiment with the standard library without writing a file.

---

## Table of Contents

1. [Starting the REPL](#1--starting-the-repl)
2. [Evaluating Expressions](#2--evaluating-expressions)
3. [Variables and Functions](#3--variables-and-functions)
4. [Multi-Line Input](#4--multi-line-input)
5. [Pipe Operator](#5--pipe-operator)
6. [Using Standard Library Modules](#6--using-standard-library-modules)
7. [REPL Commands](#7--repl-commands)
8. [Line Editing and Tab Completion](#8--line-editing-and-tab-completion)
9. [Error Handling](#9--error-handling)
10. [Tips and Tricks](#10--tips-and-tricks)

- [See Also](#see-also)

---

## 1 — Starting the REPL

Run `luma` with no arguments to enter the interactive REPL:

```bash
luma
```

You can also use the explicit flag:

```bash
luma --repl
luma -r
```

The REPL prints a banner on startup:

```text
Luma 1.0 REPL — type ':quit' to exit, ':help' for commands
```

To start the REPL in sandbox mode (no file I/O, network, or process access):

```bash
luma --box --repl
```

Exit the REPL with `:quit`, `:q`, or by pressing `Ctrl+D` on an empty line.

---

## 2 — Evaluating Expressions

Type any Luma expression at the prompt. If the expression produces a value, the REPL prints it prefixed with `=>`:

```text
luma> 2 + 3
=> 5
luma> "hello" + " " + "world"
=> "hello world"
luma> Math.sqrt(144.0)
=> 12
luma> true && !false
=> true
```

Statements that do not produce a value (such as `print`) execute silently:

```text
luma> print("hi")
hi
```

---

## 3 — Variables and Functions

Variables and functions defined in one line persist for the entire session:

```text
luma> number x = 10
luma> x * x
=> 100
luma> number pi = 3.14159
luma> pi * x * x
=> 314.159
```

Define functions and call them in subsequent lines:

```text
luma> function integer square(integer n) { return n * n }
luma> square(7)
=> 49
```

Mutable variables work as expected:

```text
luma> mutable integer count = 0
luma> count = count + 1
luma> count
=> 1
```

Records can be defined and instantiated:

```text
luma> record Point { number x, number y }
luma> Point p = Point { x: 3.0, y: 4.0 }
luma> p.x
=> 3
```

---

## 4 — Multi-Line Input

When a line contains an unmatched opening brace `{`, the REPL automatically switches to multi-line mode. A continuation prompt (`...`) appears until all braces are balanced:

```text
luma> function integer factorial(integer n) {
  ...     if n <= 1 {
  ...         return 1
  ...     }
  ...     return n * factorial(n - 1)
  ... }
luma> factorial(5)
=> 120
```

This works for any block construct — `if`/`else`, `for`, `while`, `match`, and function definitions. The brace tracking correctly handles string literals, `${...}` interpolation, and comments.

---

## 5 — Pipe Operator

The pipe operator `|>` works in the REPL just as it does in source files. It passes the left-hand value as the first argument to the right-hand function call:

```text
luma> "Hello, World!" |> String.lowercase()
=> "hello, world!"
luma> "  padded  " |> String.trim() |> String.uppercase()
=> "PADDED"
luma> array<integer> nums = [3, 1, 4, 1, 5]
luma> nums |> Array.sort() |> Array.reverse()
=> [5, 4, 3, 1, 1]
```

Pipes are especially useful for chaining standard library calls interactively.

---

## 6 — Using Standard Library Modules

All standard library modules are available in the REPL. Call module functions directly:

```text
luma> Math.abs(-42.0)
=> 42
luma> String.split("a,b,c", ",")
=> ["a", "b", "c"]
luma> Array.length([10, 20, 30])
=> 3
luma> Random.integer(1, 100)
=> 47
luma> DateTime.now()
=> "2025-01-15T10:30:00Z"
```

Collections can be built and manipulated across multiple lines:

```text
luma> mutable array<string> names = ["Alice", "Bob"]
luma> names = Array.append(names, "Charlie")
luma> names |> Array.map(function(string s) -> string { return String.uppercase(s) })
=> ["ALICE", "BOB", "CHARLIE"]
```

> **Note:** In sandbox mode (`--box`), modules that interact with the operating system (`Console`, `FileSystem`, `Http`, `Process`, `Socket`, etc.) are disabled.

---

## 7 — REPL Commands

Commands start with `:` and are not Luma code. The following commands are available:

| Command        | Short Form | Description                                            |
| -------------- | ---------- | ------------------------------------------------------ |
| `:quit`        | `:q`       | Exit the REPL                                          |
| `:help`        | `:h`       | Print the list of REPL commands                        |
| `:clear`       | `:c`       | Reset the environment — clears all variables/functions |
| `:file <path>` | `:f`       | Load and execute a `.luma` file in the current session |

### Loading Files

The `:file` command loads a `.luma` file and makes all its declarations available in the REPL session:

```text
luma> :file examples/language-features/hello.luma
Loaded: examples/language-features/hello.luma
```

Restrictions on `:file`:

- Only `.luma` files are accepted.
- Directory traversal (`..`) in paths is rejected for security.
- Symbolic links are rejected.

### Resetting the Environment

Use `:clear` to start fresh without restarting the REPL:

```text
luma> number x = 42
luma> x
=> 42
luma> :clear
Environment cleared.
luma> x
Error: undefined variable 'x'
```

---

## 8 — Line Editing and Tab Completion

The REPL includes a built-in line editor with the following features:

### Keyboard Shortcuts

| Key          | Action                     |
| ------------ | -------------------------- |
| Left / Right | Move cursor                |
| Up / Down    | Navigate command history   |
| Home         | Move to beginning of line  |
| End          | Move to end of line        |
| Tab          | Trigger tab completion     |
| Ctrl+A       | Move to beginning of line  |
| Ctrl+E       | Move to end of line        |
| Ctrl+K       | Delete to end of line      |
| Ctrl+U       | Delete to beginning        |
| Ctrl+L       | Clear screen               |
| Ctrl+C       | Cancel current input       |
| Ctrl+D       | Exit (on empty line)       |
| Delete       | Delete character at cursor |
| Backspace    | Delete character before    |

### Tab Completion

Press `Tab` to complete keywords, standard library module names, and REPL commands. If there is exactly one match, it is inserted. If there are multiple matches, they are listed:

```text
luma> Str<Tab>
=> String
luma> :q<Tab>
=> :quit
luma> Ma<Tab>
  Match  Math
```

Completion supports case-insensitive fuzzy matching when no exact prefix match is found.

### Command History

The REPL remembers up to 1,000 previous inputs (excluding commands and empty lines). Use the Up and Down arrow keys to navigate through history. Consecutive duplicate entries are deduplicated.

---

## 9 — Error Handling

The REPL catches errors gracefully and returns to the prompt. It never crashes on bad input.

### Syntax Errors

```text
luma> if true {
  ...
Error: expected expression
luma>
```

### Type Errors

The REPL runs type checking before execution. Type errors are reported but do not end the session:

```text
luma> integer x = "hello"
type: cannot assign 'string' to variable of type 'integer'
luma>
```

### Runtime Errors

```text
luma> array<integer> a = [1, 2, 3]
luma> a[10]
Error: index 10 out of bounds (array length: 3)
luma>
```

### Invalid Control Flow

Using `break` or `continue` outside a loop produces a clear error:

```text
luma> break
Error: 'break' used outside of a loop
```

---

## 10 — Tips and Tricks

- **Quick arithmetic:** Use the REPL as a calculator — `2 ** 10`, `Math.pi`, `0xFF`.
- **Explore modules:** Tab-complete a module name and browse its functions.
- **Test functions:** Define a function and call it with different arguments to verify behaviour before committing it to a file.
- **Load and extend:** Use `:file` to load utility functions from a file, then experiment with them interactively.
- **Reset often:** If the environment gets cluttered, `:clear` gives you a clean slate without restarting.
- **Pipe chains:** Build complex transformations incrementally with `|>` — each step's output is visible immediately.
- **No `@main` needed:** The REPL does not require a `@main` function. Top-level expressions and statements execute directly.
- **Sandbox mode:** Use `luma --box --repl` when experimenting with untrusted code.

---

## See Also

- [Tutorial](Luma_Tutorial.md) — a step-by-step introduction to the language for beginners
- [User Manual](Luma_User_Manual.md) — complete language reference
- [Standard Library Reference](Luma_Standard_Library_Reference.md) — all built-in modules and functions
- [Error Handling](Luma_Error_Handling.md) — error conventions and recovery
- [Contributing](../CONTRIBUTING.md) — installing and building the REPL
