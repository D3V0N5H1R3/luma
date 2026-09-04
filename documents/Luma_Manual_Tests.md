# Luma — Manual Test Plan

A comprehensive guide for manually testing the Luma Interpreter, Language Server, Debugger, VS Code Extension, and Zed Extension.

## Prerequisites

| Prerequisite | How to verify |
|---|---|
| Luma interpreter built | `build/Release/luma --version` |
| Language server built | `build/Release/luma_lsp --help` |
| Debug adapter built | `build/Release/luma_dap --help` |
| VS Code extension installed | Extensions panel → search "Luma" → shows installed |
| Zed extension installed | `zed: extensions` → search "Luma" → shows installed |

### Test Workspace Setup

Create a folder `luma-manual-tests/` with these files:

**`hello.luma`**

```luma
@main
function void main() {
    string greeting = "Hello, world!"
    Console.print(greeting)
}
```

**`math.luma`**

```luma
function number add(number a, number b) {
    return a + b
}

function string format_money(number amount) {
    return "$${String.format_number(amount, 2)}"
}

@main
function void main() {
    number result = add(3.0, 4.5)
    Console.print(format_money(result))
}
```

**`types.luma`**

```luma
record Point {
    number x
    number y
}

choice Shape {
    Circle(number radius)
    Rectangle(number width, number height)
}

function number area(Shape shape) {
    return match shape {
        case Shape.Circle(r) => 3.14159 * r * r
        case Shape.Rectangle(w, h) => w * h
    }
}

@main
function void main() {
    Shape circle = Shape.Circle(5.0)
    Console.print("Area: ${area(circle)}")
}
```

**`errors.luma`**

```luma
function result<integer> safe_divide(integer a, integer b) {
    if b == 0 {
        return failure("division by zero")
    }
    return success(a / b)
}

@main
function void main() {
    result<integer> r = safe_divide(10, 0)
    match r {
        case success(value) => Console.print("Result: ${value}")
        case failure(err) => Console.print("Error: ${err}")
    }
}
```

**`concurrency.luma`**

```luma
@main
function void main() {
    task_scope {
        task<string> t1 = spawn {
            return "hello from task"
        }
        string msg = await t1
        Console.print(msg)
    }
}
```

**`tests.luma`**

```luma
function integer factorial(integer n) {
    if n <= 1 {
        return 1
    }
    return n * factorial(n - 1)
}

@test
function void test_factorial_base() {
    assert(factorial(0) == 1)
    assert(factorial(1) == 1)
}

@test
function void test_factorial_recursive() {
    assert(factorial(5) == 120)
    assert(factorial(10) == 3628800)
}

@main
function void main() {
    Console.print("5! = ${factorial(5)}")
}
```

**`include_helper.luma`**

```luma
function string greet(string name) {
    return "Hello, ${name}!"
}
```

**`include_main.luma`**

```luma
include "include_helper.luma"

@main
function void main() {
    Console.print(greet("Luma"))
}
```

---

## Part 1 — Interpreter

### Test File Setup (Interpreter)

Most tests below use the files from the workspace setup above or small inline commands. Where a dedicated file is needed, create it in the `luma-manual-tests/` folder.

**`loops.luma`**

```luma
@main
function void main() {
    # For loop with range
    mutable integer sum = 0
    for i in Array.range(1, 11) {
        sum = sum + i
    }
    Console.print("Sum 1..10: ${sum}")

    # While loop
    mutable integer n = 10
    while n > 0 {
        if n == 5 {
            n = n - 1
            continue
        }
        Console.print("n = ${n}")
        n = n - 1
    }
}
```

**`collections.luma`**

```luma
@main
function void main() {
    # Arrays
    array<integer> nums = [3, 1, 4, 1, 5, 9]
    array<integer> sorted = nums |> Array.sort()
    array<integer> unique = sorted |> Array.unique()
    Console.print("Sorted unique: ${unique}")

    # Dictionary
    dictionary<integer> ages = {"alice": 30, "bob": 25}
    Console.print("Alice is ${Dictionary.get(ages, "alice")}")

    # Tuples
    (string, integer) pair = ("hello", 42)
    Console.print("${pair.0} ${pair.1}")

    # Queue and Stack
    mutable queue<string> q = Queue.new()
    q = Queue.enqueue(q, "first")
    q = Queue.enqueue(q, "second")
    Console.print("Dequeued: ${Queue.peek(q)}")

    mutable stack<integer> s = Stack.new()
    s = Stack.push(s, 10)
    s = Stack.push(s, 20)
    Console.print("Top: ${Stack.peek(s)}")
}
```

**`closures.luma`**

```luma
function function(integer) -> integer make_adder(integer x) {
    return (integer y) -> x + y
}

@main
function void main() {
    function(integer) -> integer add5 = make_adder(5)
    Console.print("add5(3) = ${add5(3)}")
    Console.print("add5(10) = ${add5(10)}")
}
```

**`generics.luma`**

```luma
function T identity<T>(T value) {
    return value
}

record Pair<A, B> {
    A first
    B second
}

@main
function void main() {
    integer x = identity<integer>(42)
    string s = identity<string>("hello")
    Pair<string, integer> p = Pair { first: "age", second: 30 }
    Console.print("${p.first} = ${p.second}")
}
```

**`interfaces.luma`**

```luma
interface Printable {
    function string to_display(self)
}

record Dog {
    string name
    integer age
}

function string to_display(Dog d) {
    return "${d.name} (${d.age} years)"
}

function void print_item(Printable item) {
    Console.print(to_display(item))
}

@main
function void main() {
    Dog rex = Dog { name: "Rex", age: 5 }
    print_item(rex)
}
```

**`sandbox_test.luma`**

```luma
@main
function void main() {
    # This should fail in sandbox mode
    Console.print("before file access")
    result<string> content = FileSystem.read("somefile.txt")
    Console.print("after: ${content}")
}
```

### 1.1 CLI Modes & Flags

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 1.1.1 | Run a file | `luma hello.luma` | Prints "Hello, world!", exits 0 |
| 1.1.2 | Run with arguments | `luma hello.luma --arg1 value` (if program uses `Process.arguments()`) | Arguments passed to program |
| 1.1.3 | Test mode | `luma --test tests.luma` | Runs all `@test` functions, shows pass/fail summary |
| 1.1.4 | Strict mode | `luma --strict hello.luma` (with an unused variable) | Lint warning promoted to error, non-zero exit |
| 1.1.5 | Sandbox mode | `luma --box sandbox_test.luma` | FileSystem call fails with "not available in sandbox mode" |
| 1.1.6 | Type-check only | `luma --check math.luma` | Reports type errors without running; exits 0 if clean |
| 1.1.7 | Version | `luma --version` | Prints version string (e.g., "luma 0.7.x") |
| 1.1.8 | Help | `luma --help` | Shows usage with all flags listed |
| 1.1.9 | Invalid file | `luma nonexistent.luma` | Error message, exits with non-zero code |
| 1.1.10 | Syntax error exit code | `luma` a file with syntax error | Exits with code 3 (SyntaxError) |
| 1.1.11 | Type error exit code | `luma` a file with type error | Exits with code 2 (TypeError) |
| 1.1.12 | Runtime error exit code | `luma` a file that divides by zero | Exits with code 1 (RuntimeError) |

### 1.2 REPL

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 1.2.1 | Enter REPL | Run `luma` with no arguments | Shows prompt, ready for input |
| 1.2.2 | Evaluate expression | Type `1 + 2` and press Enter | Prints `3` |
| 1.2.3 | Variable persistence | `integer x = 10` then on next line `x * 2` | First line silently defines; second prints `20` |
| 1.2.4 | Function definition | Define `function integer sq(integer n) { return n * n }` then call `sq(7)` | Returns `49` |
| 1.2.5 | Multi-line input | Type `if true {` and press Enter | Continues prompt (waits for `}`) |
| 1.2.6 | `:help` command | Type `:help` | Prints available REPL commands |
| 1.2.7 | `:clear` command | Define a variable, then `:clear`, then reference it | Error — variable no longer defined |
| 1.2.8 | `:file` command | `:file include_helper.luma` then call `greet("world")` | Loads file; function available |
| 1.2.9 | `:quit` command | Type `:quit` | REPL exits cleanly |
| 1.2.10 | History | Type something, press Up arrow | Previous input recalled |
| 1.2.11 | Tab completion | Type `Con` then Tab | Completes to `Console` (or shows options) |
| 1.2.12 | Runtime error recovery | Type `1 / 0` | Error message printed, REPL continues (doesn't crash) |

### 1.3 Language Basics

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 1.3.1 | Immutable variable | Assign to an immutable variable twice in source | Type error at compile time |
| 1.3.2 | Mutable variable | `mutable integer x = 1` then `x = 2` | Compiles and runs successfully |
| 1.3.3 | String interpolation | `"1 + 2 = ${1 + 2}"` | Produces `"1 + 2 = 3"` |
| 1.3.4 | Triple-quoted string | Use `"""` multiline string | Auto-dedents common whitespace |
| 1.3.5 | Boolean operations | `true and false`, `not true`, `true or false` | `false`, `false`, `true` |
| 1.3.6 | Integer arithmetic | `10 / 3`, `10 % 3` | `3`, `1` (integer division) |
| 1.3.7 | Number precision | `0.1 + 0.2` | Standard IEEE-754 result |
| 1.3.8 | String concatenation | `"hello" + " " + "world"` | `"hello world"` |
| 1.3.9 | Type promotion | `integer` + `number` operation | Promotes to `number` result |
| 1.3.10 | None type | Use `none` with `optional<T>` | Compiles; evaluates to absent value |

### 1.4 Control Flow

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 1.4.1 | If/else expression | `integer x = if true { 1 } else { 2 }` | `x` equals `1` |
| 1.4.2 | If/else if/else | Chain of conditions with distinct branches | Correct branch executes |
| 1.4.3 | While loop | Run `loops.luma` | Sum computed correctly (55) |
| 1.4.4 | For loop with range | Run `loops.luma` | Iterates 1 through 10 |
| 1.4.5 | Break in loop | `while true { break }` | Loop exits immediately |
| 1.4.6 | Continue in loop | Loop with `continue` on `n == 5` | Skips iteration, prints other values |
| 1.4.7 | Match expression | Run `types.luma` (match on Shape) | Correct branch for `Circle` |
| 1.4.8 | Match exhaustiveness | Remove a `case` from a `match` on a choice type | Compile error: non-exhaustive match |

### 1.5 Functions

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 1.5.1 | Basic function call | Run `math.luma` | `add(3.0, 4.5)` returns `7.5` |
| 1.5.2 | Recursion | Run `tests.luma` | `factorial(5)` returns `120` |
| 1.5.3 | Closures | Run `closures.luma` | `add5(3)` returns `8`, `add5(10)` returns `15` |
| 1.5.4 | Higher-order functions | `Array.map([1,2,3], (integer x) -> x * 2)` | Returns `[2, 4, 6]` |
| 1.5.5 | Named arguments | `add(a: 1.0, b: 2.0)` | Works the same as positional |
| 1.5.6 | Optional parameters | Function with default value, call without that arg | Default applied |
| 1.5.7 | Pipe operator | `"hello" \|> String.to_upper()` | Returns `"HELLO"` |
| 1.5.8 | Pipe chain | `[3,1,2] \|> Array.sort() \|> Array.reverse()` | Returns `[3, 2, 1]` |
| 1.5.9 | Namespace function | `Math.sqrt(16.0)` | Returns `4.0` |

### 1.6 Types & Data Structures

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 1.6.1 | Record creation | Run `types.luma` | `Point` created with `x`, `y` fields |
| 1.6.2 | Choice type | `Shape.Circle(5.0)` | Creates Circle variant |
| 1.6.3 | Match on choice | Run `types.luma` | `area()` dispatches correctly |
| 1.6.4 | Generic function | Run `generics.luma` | `identity` works for integer and string |
| 1.6.5 | Generic record | `Pair<string, integer>` | Fields typed correctly |
| 1.6.6 | Interface (structural) | Run `interfaces.luma` | `print_item(rex)` works via structural match |
| 1.6.7 | Downcast | `downcast<Dog>(some_printable)` | Returns `result<Dog>` |
| 1.6.8 | Type alias | `type Name = string` then use `Name` | Transparent alias works |
| 1.6.9 | Record with block | `Point { x: 1.0, y: 2.0 }` literal syntax | Creates record correctly |
| 1.6.10 | Private fields | Record with `private` field, access from outside | Compile error unless `trusted` |

### 1.7 Collections

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 1.7.1 | Array operations | Run `collections.luma` | Sort, unique, print work |
| 1.7.2 | Array indexing | `[10, 20, 30][1]` | Returns `20` |
| 1.7.3 | Array slicing | `[1,2,3,4,5][1:3]` | Returns `[2, 3]` |
| 1.7.4 | Array.map/filter/reduce | `[1,2,3,4] \|> Array.filter((integer x) -> x > 2)` | Returns `[3, 4]` |
| 1.7.5 | Dictionary | `Dictionary.get(ages, "alice")` | Returns the value for key |
| 1.7.6 | Queue FIFO | Enqueue A, B; dequeue | Returns A first |
| 1.7.7 | Stack LIFO | Push 10, 20; peek | Returns 20 |
| 1.7.8 | Set operations | `Set.union`, `Set.intersection`, `Set.difference` | Correct set results |
| 1.7.9 | Out of bounds | Access `[1,2,3][10]` | Runtime error with clear message |

### 1.8 Error Handling

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 1.8.1 | Result success | Run `errors.luma` with `safe_divide(10, 2)` | Prints "Result: 5" |
| 1.8.2 | Result failure | Run `errors.luma` with `safe_divide(10, 0)` | Prints "Error: division by zero" |
| 1.8.3 | Default operator `??` | `safe_divide(1, 0) ?? -1` | Returns `-1` |
| 1.8.4 | Propagation `?` | Function using `?` to propagate errors | Error bubbles to caller |
| 1.8.5 | Optional some/none | `optional<integer> x = some(42)` / `none` | Correct value or absence |
| 1.8.6 | Try/catch | `try { 1 / 0 } catch err { Console.print(err) }` | Catches runtime error |
| 1.8.7 | Assert | `assert(false)` | Runtime error: "Assertion failed" |
| 1.8.8 | Assert with message | `assert(false, "custom msg")` | Shows "custom msg" |

### 1.9 Concurrency

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 1.9.1 | Spawn and await | Run `concurrency.luma` | Prints "hello from task" |
| 1.9.2 | Task scope | `task_scope` ensures all children complete | Program doesn't exit before tasks finish |
| 1.9.3 | Channel send/receive | Create channel, send value, receive it | Value received correctly |
| 1.9.4 | Multiple tasks | Spawn 5 tasks, await all | All results collected |
| 1.9.5 | Task cancellation | `Task.cancel(t)` on a running task | Task stops; `Task.is_cancelled(t)` returns true |
| 1.9.6 | Channel close | Close a channel, try to send | Returns failure result |

### 1.10 Standard Library Highlights

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 1.10.1 | String functions | `String.to_upper("hello")`, `String.split("a,b", ",")` | `"HELLO"`, `["a", "b"]` |
| 1.10.2 | Math functions | `Math.sqrt(9.0)`, `Math.pi` | `3.0`, `3.14159...` |
| 1.10.3 | DateTime | `DateTime.now()` | Returns current timestamp |
| 1.10.4 | JSON parse | `Json.parse("{\"key\": 1}")` | Parsed dictionary |
| 1.10.5 | RegularExpression | `RegularExpression.is_match("hello123", "[a-z]+\\d+")` | Returns `true` |
| 1.10.6 | Random | `Random.integer(1, 100)` | Returns integer in range |
| 1.10.7 | FileSystem (non-sandbox) | `FileSystem.write("test.txt", "content")` then `FileSystem.read("test.txt")` | Write succeeds; read returns "content" |
| 1.10.8 | Hash | `Hash.sha256("hello")` | Returns hex digest string |
| 1.10.9 | Converter | `Converter.integer_to_string(42)` | Returns `"42"` |
| 1.10.10 | Array.sort_by | `Array.sort_by(items, (T x) -> x.key)` | Sorted by key |

### 1.11 Include & Multi-File

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 1.11.1 | Basic include | Run `include_main.luma` | Uses function from `include_helper.luma` |
| 1.11.2 | Circular include | File A includes B, B includes A | Compile error: circular include detected |
| 1.11.3 | Missing include | `include "nonexistent.luma"` | Compile error with clear message |
| 1.11.4 | Path traversal | `include "../../../etc/passwd"` | Security error: path traversal rejected |

### 1.12 Testing Framework

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 1.12.1 | All tests pass | `luma --test tests.luma` | "2/2 tests passed" (green output) |
| 1.12.2 | Test failure | Add `assert(false)` to a test, run `--test` | Shows which test failed with location |
| 1.12.3 | Strict test mode | `luma --strict --test tests.luma` | Lint warnings become errors |
| 1.12.4 | Test with main | File has both `@test` and `@main`; `--test` only runs tests | `@main` not executed |

---

## Part 2 — Language Server (VS Code)

### 2.1 Diagnostics

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.1.1 | Syntax error detected | Open `hello.luma`, delete the closing `}` | Red underline appears on the error location within ~1 second |
| 2.1.2 | Type error detected | In `math.luma`, change `return a + b` to `return a + "text"` | Red underline with "type mismatch" diagnostic |
| 2.1.3 | Lint warning | Add `integer unused = 42` inside `main()` of `hello.luma` | Yellow underline on `unused` with "unused variable" warning |
| 2.1.4 | Errors clear on fix | Undo the changes from 1.1.1 | Red underlines disappear |
| 2.1.5 | Diagnostics on save mode | Set `luma.diagnostics.onSave: true` in settings, introduce a lint warning → no squiggles until Ctrl+S | Warning appears only after save |

### 2.2 Hover

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.2.1 | Variable hover | Hover over `greeting` in `hello.luma` | Tooltip shows type `string` |
| 2.2.2 | Function hover | Hover over `add` in `math.luma` at its call site | Tooltip shows `function number add(number a, number b)` |
| 2.2.3 | Stdlib module hover | Hover over `Console` in any file | Tooltip shows module description |
| 2.2.4 | Stdlib function hover | Hover over `print` in `Console.print(...)` | Tooltip shows signature and description |
| 2.2.5 | Type keyword hover | Hover over `integer` in a type annotation | Tooltip shows type description |
| 2.2.6 | Record type hover | Hover over `Point` in `types.luma` | Tooltip shows record fields |

### 2.3 Completions

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.3.1 | Stdlib module completion | Type `Console.` (with dot) in a function body | Dropdown shows `print`, `read_line`, etc. |
| 2.3.2 | Pipe completion | Type `"hello" \|> String.` | Dropdown shows String module members |
| 2.3.3 | Local variable completion | In a function with local `greeting`, type `gre` | `greeting` appears in completions |
| 2.3.4 | Keyword completion | Start a new line and type `fun` | `function` keyword appears |
| 2.3.5 | Completion detail | Select a stdlib completion item | Detail panel shows type signature and docs |

### 2.4 Signature Help

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.4.1 | Function call signature | Type `add(` in `math.luma` body | Signature help popup shows `(number a, number b)` with first param bold |
| 2.4.2 | Parameter advance | Type `3.0,` after the `(` | Signature help highlights second parameter |
| 2.4.3 | Stdlib signature | Type `String.contains(` | Shows signature with parameter names |

### 2.5 Navigation

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.5.1 | Go to Definition | Right-click `add` call in `math.luma` → Go to Definition (F12) | Cursor jumps to `function number add(...)` |
| 2.5.2 | Go to Definition (include) | In `include_main.luma`, F12 on `greet` | Opens `include_helper.luma` at the function |
| 2.5.3 | Find References | Right-click `factorial` → Find All References | Panel shows all usages (definition + calls + test) |
| 2.5.4 | Go to Type Definition | F12-type-def on a variable of type `Shape` | Jumps to the `choice Shape` declaration |
| 2.5.5 | Document Highlight | Click on `amount` in `format_money` | All occurrences of `amount` in that function are highlighted |

### 2.6 Symbols & Outline

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.6.1 | Document Symbols | Open `types.luma`, open Outline panel (Ctrl+Shift+O) | Shows `Point`, `Shape`, `area`, `main` hierarchically |
| 2.6.2 | Workspace Symbols | Ctrl+T, type `factorial` | Shows `factorial` from `tests.luma` |
| 2.6.3 | Breadcrumbs | Click in `area` function body | Breadcrumb bar shows `types.luma > area` |

### 2.7 Code Actions & Refactoring

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.7.1 | Quick Fix | Introduce a fixable error (e.g., missing `mutable`), click lightbulb | Offers to add `mutable` |
| 2.7.2 | Rename symbol | F2 on `greeting` in `hello.luma`, type `message`, Enter | All occurrences renamed |
| 2.7.3 | Add type annotation | On a variable without explicit type (`x = 5`), invoke code action | Offers "Add type annotation" |

### 2.8 Formatting

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.8.1 | Format Document | Mess up indentation, then Shift+Alt+F | Indentation corrected to 4 spaces |
| 2.8.2 | Format Selection | Select a block, right-click → Format Selection | Only selected block reformatted |

### 2.9 Folding

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.9.1 | Function folding | Click fold icon on function `area` in `types.luma` | Function body collapses |
| 2.9.2 | Block folding | Fold a `match` block | Block collapses showing first line |

### 2.10 Inlay Hints

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.10.1 | Variable type hint | In `math.luma`, `result` should show inferred type | Ghost text `: number` after variable name |
| 2.10.2 | Parameter name hint | At call `add(3.0, 4.5)` | Ghost text `a:` before `3.0`, `b:` before `4.5` |
| 2.10.3 | No hints on declarations | Function parameters `(number a, number b)` | No parameter name hints on the declaration itself |
| 2.10.4 | Disable inlay hints | Set `luma.inlayHints.enabled: false` | All inlay hints disappear |

### 2.11 Semantic Highlighting

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.11.1 | Consistent minimap | Open a long `.luma` file, check minimap | Colors are consistent — no "scrambled" sections with different font sizes |
| 2.11.2 | Type coloring | Record/choice names colored differently from variables | Distinct semantic token colors for types vs variables |
| 2.11.3 | Function call coloring | `add(...)` at call site colored as function | Distinct color from variable names |

### 2.12 Code Lens

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.12.1 | Reference count | Ensure `luma.codeLens.enabled: true`, open `tests.luma` | "N references" appears above `factorial` |
| 2.12.2 | Click lens | Click the "N references" lens | Shows references panel |

### 2.13 Call & Type Hierarchy

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.13.1 | Call Hierarchy | Right-click `factorial` → Show Call Hierarchy | Shows incoming calls (from tests and main) |
| 2.13.2 | Type Hierarchy | On a record that implements an interface, Show Type Hierarchy | Shows supertypes/subtypes |

### 2.14 Selection & Linked Editing

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 2.14.1 | Expand selection | Place cursor inside `"Hello, world!"`, press Shift+Alt+→ repeatedly | Selection grows: word → string content → full string → expression → statement → function body → function → file |
| 2.14.2 | Linked editing | Enable linked editing, double-click a variable name | All same-name occurrences in scope edit simultaneously |

---

## Part 3 — Debugger (VS Code)

### 3.1 Basic Launch

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 3.1.1 | Launch and run | Create `launch.json` with program: `${file}`, open `hello.luma`, F5 | Program runs, "Hello, world!" in Debug Console output |
| 3.1.2 | Stop on entry | Set `"stopOnEntry": true`, F5 | Execution pauses on the first line of `main()` |
| 3.1.3 | Program arguments | Add `"args": ["--verbose"]` to launch config (for a program that reads args) | Args are passed correctly |
| 3.1.4 | Working directory | Set `"cwd"` to a different folder | Program runs with that as its CWD |

**Sample `launch.json`:**

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "type": "luma",
            "request": "launch",
            "name": "Debug Luma",
            "program": "${file}",
            "stopOnEntry": false
        }
    ]
}
```

### 3.2 Breakpoints

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 3.2.1 | Line breakpoint | Set breakpoint on `Console.print(greeting)` in `hello.luma`, F5 | Execution pauses at that line |
| 3.2.2 | Conditional breakpoint | Right-click gutter → Conditional Breakpoint → `n > 3` on `return n * factorial(n-1)` in `tests.luma`, F5 | Only pauses when `n > 3` |
| 3.2.3 | Hit count breakpoint | Right-click gutter → Hit Count → enter `3` on recursive `factorial` call | Pauses on 3rd hit |
| 3.2.4 | Log point | Right-click gutter → Log Point → enter `n = {n}` on factorial line | Messages appear in Debug Console without pausing |
| 3.2.5 | Function breakpoint | Debug panel → Breakpoints → "+" → type `add` | Pauses when `add()` is called |
| 3.2.6 | Disable/enable breakpoint | Uncheck a breakpoint in the Breakpoints panel | Breakpoint is skipped during execution |
| 3.2.7 | Remove breakpoint | Click a red dot in the gutter | Breakpoint removed |

### 3.3 Execution Control

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 3.3.1 | Continue | Pause at breakpoint, press F5 | Runs until next breakpoint or program end |
| 3.3.2 | Step Over | Pause at `add(3.0, 4.5)` call, press F10 | Executes `add` without entering it, stops at next line |
| 3.3.3 | Step Into | Pause at `add(3.0, 4.5)` call, press F11 | Enters the `add` function, pauses at `return a + b` |
| 3.3.4 | Step Out | While inside `add`, press Shift+F11 | Returns to caller, pauses after the call |
| 3.3.5 | Pause | F5 on a long-running program (e.g., loop), press Pause button | Execution interrupts at current location |
| 3.3.6 | Restart | During a debug session, press Ctrl+Shift+F5 | Session restarts from the beginning |
| 3.3.7 | Stop | Press Shift+F5 | Session terminates immediately |

### 3.4 State Inspection

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 3.4.1 | Local variables | Pause inside `add`, check Variables panel | Shows `a = 3.0`, `b = 4.5` in Local scope |
| 3.4.2 | Structured types | Pause where a `Point` record exists, expand it | Shows `x` and `y` fields |
| 3.4.3 | Array expansion | Pause where an array variable exists, expand it | Shows indexed elements |
| 3.4.4 | Hover evaluation | While paused, hover over `result` in editor | Tooltip shows current value |
| 3.4.5 | Debug Console eval | While paused, type `a + b` in Debug Console | Shows computed result |
| 3.4.6 | Set variable | In Variables panel, double-click a number value, change it | Value updates; continued execution uses new value |
| 3.4.7 | Call stack | Pause inside nested calls (e.g., recursive `factorial`) | Call Stack panel shows full chain of frames |
| 3.4.8 | Click stack frame | Click a different frame in the Call Stack | Variables panel updates to show that frame's locals |

### 3.5 Exception Handling

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 3.5.1 | Break on uncaught exception | In Breakpoints panel, check "Uncaught Exceptions", run a program that throws | Pauses at the throw site with exception info |
| 3.5.2 | Break on caught exception | Check "Caught Exceptions", run `errors.luma` modified to use try/catch | Pauses before handler runs |
| 3.5.3 | Exception info | When paused on exception, hover or check Variables | Shows exception message |

### 3.6 Concurrency Debugging

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 3.6.1 | Task threads | Debug `concurrency.luma` with breakpoint inside `spawn` | Threads panel shows main + spawned task |
| 3.6.2 | Switch thread | Click different thread in Threads panel | Stack and Variables update to show that thread's state |

---

## Part 4 — VS Code Extension Features

### 4.1 Commands

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 4.1.1 | Run File | Open `hello.luma`, press Ctrl+Alt+R (or click ▶ in title bar) | Terminal opens, runs program, shows "Hello, world!" |
| 4.1.2 | Run Tests | Open `tests.luma`, press Ctrl+Alt+T (or click test icon) | Terminal runs `luma --test tests.luma`, shows pass/fail |
| 4.1.3 | Restart Server | Command Palette → "Luma: Restart Language Server" | LSP restarts, status bar updates, features resume |
| 4.1.4 | Show Output | Command Palette → "Luma: Show Language Server Output" | Output channel opens with LSP trace/logs |

### 4.2 Themes

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 4.2.1 | Luma Dark theme | Command Palette → Preferences: Color Theme → "Luma Dark" | Theme applies with good Luma syntax colors |
| 4.2.2 | Luma Light theme | Switch to "Luma Light" | Light theme with appropriate contrast |
| 4.2.3 | Third-party theme compat | Switch to a popular theme (e.g., "One Dark Pro") | Luma semantic tokens still colored reasonably |

### 4.3 Snippets

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 4.3.1 | Function snippet | In a `.luma` file, type `fn` and trigger completion | Inserts function template with tabstops |
| 4.3.2 | Main snippet | Type `main` and trigger completion | Inserts `@main` annotated function |
| 4.3.3 | Test snippet | Type `test` and trigger completion | Inserts `@test` annotated function |

### 4.4 Tasks

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 4.4.1 | Run task | Terminal → Run Task → select "luma: run" for current file | Program executes in terminal |
| 4.4.2 | Test task | Terminal → Run Task → select "luma: test" | Tests run, results shown |
| 4.4.3 | Problem matcher | Run a file with errors via task | Problems panel shows parsed diagnostics |

### 4.5 Playground

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 4.5.1 | Open Playground | Command Palette → "Luma: Open Playground" | Playground panel opens |
| 4.5.2 | Execute code | Type a simple expression in the playground, execute | Output appears in result panel |
| 4.5.3 | Timeout | Write an infinite loop, execute | Stops after configured timeout with error message |

### 4.6 Walkthrough

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 4.6.1 | Getting Started | Command Palette → "Welcome: Open Walkthrough" → "Luma" | 7-step walkthrough loads with markdown content |

### 4.7 File Association & Icons

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 4.7.1 | File icon | Create a `.luma` file in Explorer | Shows Luma file icon |
| 4.7.2 | Language detection | Open a `.luma` file | Status bar shows "Luma" as language mode |
| 4.7.3 | Markdown code blocks | Open a `.md` file with ` ```luma ` fence | Code block has Luma syntax highlighting |

---

## Part 5 — Zed Extension

### 5.1 Basic Language Support

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 5.1.1 | Syntax highlighting | Open `hello.luma` in Zed | Keywords, strings, numbers, comments colored |
| 5.1.2 | File detection | Open any `.luma` file | Bottom bar shows "Luma" language |
| 5.1.3 | Bracket matching | Place cursor on `{` | Matching `}` is highlighted |
| 5.1.4 | Auto-indentation | Press Enter after `{` | New line is indented by 4 spaces |
| 5.1.5 | Code folding | Click fold icon on a function | Body collapses |

### 5.2 LSP Features in Zed

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 5.2.1 | Diagnostics | Introduce a type error | Diagnostic underline appears |
| 5.2.2 | Hover | Hover over a variable | Type information tooltip appears |
| 5.2.3 | Go to Definition | `gd` (or Cmd+Click) on a function call | Jumps to definition |
| 5.2.4 | Find References | `gr` on a symbol | References panel opens with all usages |
| 5.2.5 | Completions | Type `String.` after a pipe | Completion menu shows String members |
| 5.2.6 | Signature help | Type `add(` | Parameter signature shown |
| 5.2.7 | Rename | `gc r` (or F2) on a variable | Rename prompt; all occurrences updated |
| 5.2.8 | Code Actions | On a diagnostic, invoke code action | Quick fix offered |
| 5.2.9 | Format buffer | `cmd+shift+i` (or `:format`) | Document formatted |
| 5.2.10 | Inlay hints | Toggle inlay hints (`toggle_inlay_hints`) | Type annotations and parameter names appear/disappear |
| 5.2.11 | Document outline | `cmd+shift+o` (symbols) | Shows functions, types, namespaces |

### 5.3 Task Runner

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 5.3.1 | Run file | Use "Run File" task (or inline ▶ button next to `@main`) | Program executes in terminal panel |
| 5.3.2 | Run tests | Use "Run Tests" task (or inline button next to `@test`) | Tests execute, results shown |
| 5.3.3 | Run nearest test | With cursor inside a `@test` function, run "Run Nearest Test" | Only that test runs |

### 5.4 Debugger in Zed

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 5.4.1 | Toggle breakpoint | Click gutter or use keybinding | Red dot appears |
| 5.4.2 | Start debugging | Start debug task for current file | DAP session starts, breakpoints hit |
| 5.4.3 | Step controls | Use step over / step into / continue | Execution advances as expected |
| 5.4.4 | Variable inspection | While paused, check variables panel | Local variables displayed |

### 5.5 Snippets

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 5.5.1 | Function snippet | Type `fn` and accept completion | Function template inserted |
| 5.5.2 | Main snippet | Type `main` and accept | `@main function void main() { }` inserted |

---

## Part 6 — Cross-Cutting & Regression Tests

### 6.1 Minimap & Visual Consistency

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 6.1.1 | Minimap rendering | Open a long `.luma` file in VS Code, look at minimap | All text rendered in same consistent small-font style with colors — no "scrambled" sections |
| 6.1.2 | Inlay hints in minimap | With inlay hints enabled, check minimap | No visual artifacts or differently-sized text from hints |

### 6.2 Multi-file Workspace

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 6.2.1 | Include resolution | Open `include_main.luma`, F12 on `greet` | Navigates to `include_helper.luma` |
| 6.2.2 | Cross-file references | Find References on `greet` | Shows usage in `include_main.luma` |
| 6.2.3 | Workspace symbols | Ctrl+T, search for a function name from another file | Found and navigable |

### 6.3 Settings Responsiveness

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 6.3.1 | Toggle inlay hints | Change `luma.inlayHints.enabled` from true→false→true | Hints disappear and reappear without restart |
| 6.3.2 | Toggle code lens | Change `luma.codeLens.enabled` | Lenses disappear and reappear |
| 6.3.3 | Custom binary path | Set `luma.path` to a wrong path, then fix it | Error shown, then normal operation resumes |

### 6.4 Error Recovery

| # | Test | Steps | Expected |
|---|------|-------|----------|
| 6.4.1 | Partial file editing | Type code character by character (incomplete syntax) | No crash; diagnostics update progressively |
| 6.4.2 | Large file | Open a 1000+ line Luma file | All features still responsive (< 2 second latency) |
| 6.4.3 | LSP crash recovery | Kill the `luma_lsp` process | VS Code shows notification; restarting via command works |

---

## Execution Checklist

Use this summary to track progress:

| Area | Total Tests | Passed | Failed | Skipped |
|------|-------------|--------|--------|---------|
| 1. Interpreter | 58 | | | |
| 2. Language Server | 40 | | | |
| 3. Debugger | 22 | | | |
| 4. VS Code Extension | 16 | | | |
| 5. Zed Extension | 16 | | | |
| 6. Cross-Cutting | 9 | | | |
| **Total** | **161** | | | |

## Reporting Issues

For each failure, record:

- **Test ID** (e.g., 1.10.3)
- **Observed behavior** (what actually happened)
- **Expected behavior** (from the "Expected" column)
- **Screenshot** (if visual)
- **Log output** (terminal output, LSP/DAP logs from Output Channel)
