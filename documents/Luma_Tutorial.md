# Luma — Tutorial

> A step-by-step introduction to programming with Luma — for people who have never written a line of code before. No prior experience needed.

---

## Table of Contents

1. [What Programming Is, and What Luma Is](#1--what-programming-is-and-what-luma-is)
2. [Installing and Running Luma](#2--installing-and-running-luma)
3. [Your First Program](#3--your-first-program)
4. [Printing and Comments](#4--printing-and-comments)
5. [Values and Types](#5--values-and-types)
6. [Variables and Mutability](#6--variables-and-mutability)
7. [Arithmetic and Operators](#7--arithmetic-and-operators)
8. [Working with Text](#8--working-with-text)
9. [Making Decisions](#9--making-decisions)
10. [Repeating Work](#10--repeating-work)
11. [Arrays](#11--arrays)
12. [Dictionaries](#12--dictionaries)
13. [Functions](#13--functions)
14. [Records](#14--records)
15. [Tuples](#15--tuples)
16. [Choice Types](#16--choice-types)
17. [Pattern Matching](#17--pattern-matching)
18. [Handling Absence and Failure](#18--handling-absence-and-failure)
19. [Lambdas, Pipes, and Higher-Order Functions](#19--lambdas-pipes-and-higher-order-functions)
20. [A Tour of the Standard Library](#20--a-tour-of-the-standard-library)
21. [Reading Input from the User](#21--reading-input-from-the-user)
22. [Testing Your Code](#22--testing-your-code)
23. [Organising Code with Namespaces](#23--organising-code-with-namespaces)
24. [Splitting a Program Across Files](#24--splitting-a-program-across-files)
25. [Project — A Number-Guessing Game](#25--project--a-number-guessing-game)
26. [Where to Go Next](#26--where-to-go-next)
27. [Glossary](#27--glossary)

---

## 1 — What Programming Is, and What Luma Is

Welcome. If you have never programmed before, you are in exactly the right place. This tutorial assumes no prior knowledge and builds up one small idea at a time.

### What Is a Program?

A **computer** is a machine that follows instructions very quickly and very literally. A **program** is a list of those instructions, written down in a language the computer can understand. **Programming** is the craft of writing that list so the machine does something useful — add up a shopping bill, play a game, draw a chart, or send a message.

The catch is that computers do *exactly* what you say, not what you *meant*. Learning to program is mostly learning to express your intentions precisely and to break big goals into tiny, unambiguous steps.

### What Is Luma?

**Luma** is a programming language designed for beginners. Its goal is to be *as easy to read as Python, and as safe as Rust*. In practice that means two things:

- **Readable.** The words on the page say what they do. You will be surprised how much of a Luma program you can guess just by reading it aloud.
- **Safe.** Luma catches many common mistakes *before* your program even runs, and it forces you to think about the cases that go wrong (empty lists, missing values, bad input) instead of ignoring them until they crash.

Luma is **statically typed**, which means every value has a known *type* (a number, a piece of text, a list, and so on), and the language checks that you use those types consistently. Don't worry if that sounds abstract — you will feel how it helps within the first few chapters.

### How to Use This Tutorial

Read it in order. Each chapter builds on the last. Most importantly: **type the examples yourself and run them.** Reading about programming is like reading about swimming — useful, but no substitute for getting in the water. When you see a **Try it** box, pause and experiment before moving on.

Let's get you set up.

---

## 2 — Installing and Running Luma

To follow along you need the `luma` program installed on your computer. Building it from source is covered in detail in the [Contributing guide](../CONTRIBUTING.md); follow that guide once, then come back here.

Once it is installed, open a **terminal** (also called a command line or shell) — a text window where you type commands. Check that Luma is available:

```bash
luma --version
```

You should see a version number, such as `Luma 0.10.0`. If you do, you are ready.

### Running a Program from a File

A Luma program lives in a plain text file whose name ends in `.luma`. To run one, you type `luma` followed by the file's name:

```bash
luma hello.luma
```

Luma first checks your program for mistakes, and if all is well, runs it and shows the output.

### The Interactive REPL

Luma also has an **REPL** (Read–Eval–Print Loop) — an interactive playground where you type one line at a time and see the result immediately. Start it by running `luma` with no file name:

```bash
luma
```

You will get a prompt where you can try small expressions. The REPL is perfect for experimenting while you learn. Type `:quit` (or press `Ctrl+D`) to leave. See the [REPL Guide](Luma_REPL_Guide.md) for everything it can do.

> **Tip:** Use a code editor with Luma support (there are extensions for VS Code and Zed) so you get colours, error underlines, and hints as you type. It makes learning much smoother.

---

## 3 — Your First Program

By tradition, the first program in any language simply greets the world. Create a file called `hello.luma` with exactly this content:

```luma
@main
function void main() {
    print("Hello, Luma!")
}
```

Run it:

```bash
luma hello.luma
```

The output is:

```text
Hello, Luma!
```

Congratulations — you are a programmer. Now let's understand every piece of that program, because these pieces appear in *every* Luma program you will write.

### Anatomy of the Program

- **`@main`** is an **annotation** — a note attached to what follows. `@main` marks the function that Luma should run first. Think of it as the front door: execution starts here. Every complete Luma program has exactly one `@main`.
- **`function void main()`** declares a **function** named `main`. A function is a named block of instructions. We will cover functions properly in [chapter 13](#13--functions); for now, just know this is the standard starting shape. The word `void` means "this function produces no value" — it just *does* things.
- **`{ ... }`** — the curly braces hold the function's **body**: the instructions to carry out. Everything the program does goes between them.
- **`print("Hello, Luma!")`** is the one instruction here. `print` is a built-in tool that displays text on the screen. The text to display, `"Hello, Luma!"`, sits inside the parentheses.

### Two Rules You'll Notice

- **Braces are always required**, even around a single instruction. This keeps programs consistent and unambiguous.
- **Semicolons are optional.** Some languages end every line with `;`. In Luma you may, but you needn't — a new line is enough. This tutorial leaves them out.

> **Try it:** Change the greeting to your own name and run it again. Then add a second `print` line below the first and see what happens.

From here on, when a snippet is short, assume it lives *inside* a function body (usually `main`). When we show a full program, you will see the `@main` wrapper.

---

## 4 — Printing and Comments

### Printing Output

`print` is how your program talks to you. You can print text, numbers, or several values at once, separated by commas:

```luma
print("The answer is", 42)
```

```text
The answer is 42
```

Each `print` starts a new line. Printing is your most valuable tool while learning: when you are unsure what a program is doing, add a `print` to peek inside.

### Comments

A **comment** is a note for humans that the computer ignores. In Luma, anything after a `#` on a line is a comment:

```luma
# This whole line is a comment.
print("Hi")   # This part, after the code, is too.
```

Use comments to explain *why* you did something, not to restate the obvious. Good code says *what* it does by itself; comments add the reasoning a reader can't see.

> **Try it:** Add a comment above your greeting explaining what the program does. Confirm the output is unchanged — the computer skipped your note.

---

## 5 — Values and Types

Programs work with **values**: pieces of data such as the number `7`, the text `"hello"`, or the idea of `true`. Every value in Luma has a **type**, which tells the language (and you) what kind of thing it is and what you can do with it. Luma has four fundamental types you will use constantly.

### Boolean

A **boolean** is a truth value: either `true` or `false`. Booleans are the backbone of decision-making — "is the score high enough?", "did the user quit?".

```luma
boolean is_raining = true
boolean is_sunny = false
```

### Integer

An **integer** is a whole number with no fractional part: `0`, `42`, `-7`. In Luma, integers have a specific job: **counting positions and repetitions** — list indexes, loop counters, and the bounds of a range.

```luma
integer index = 0
integer year = 2025
```

### Number

A **number** holds values that may have a fractional part: `3.14`, `-0.5`, `100.0`. Use `number` for **quantities and measurements** — prices, scores, temperatures, distances, averages.

```luma
number price = 19.99
number temperature = 21.5
```

> **Watch out — `integer` vs `number`:** This is the one distinction beginners trip over most in Luma, so let's be clear. Reach for `integer` when the value counts *positions or steps* (the 3rd item, loop number 5, the range `0..10`). Reach for `number` for *how much* of something there is (a score of `95`, a price of `4.50`, a weight of `70`). When in doubt for a real-world quantity, use `number`.
>
> Two consequences follow. First, dividing two integers throws away the remainder: `7 / 2` is `3`, not `3.5`. Second, a whole `number` always prints with a trailing `.0` — the value `95` of type `number` shows as `95.0`. That `.0` is Luma reminding you "this is a measurement, not a position."

### String

A **string** is a piece of text, written between double quotes: `"hello"`, `"Luma 0.10"`, `""` (the empty string). Strings hold names, messages, file contents — anything textual.

```luma
string greeting = "Hello"
string name = "Ada"
```

### None

There is one more special value, `none`, which represents *the absence of a value* — "nothing here". It has its own careful handling, which we cover in [chapter 18](#18--handling-absence-and-failure). Mentioning it now just so the word isn't a surprise later.

### Checking a Type

If you are ever unsure what type a value has, `type_of` tells you:

```luma
print(type_of(42))       # integer
print(type_of(3.14))     # number
print(type_of("hi"))     # string
print(type_of(true))     # boolean
```

---

## 6 — Variables and Mutability

A **variable** is a name that refers to a value. Think of it as a labelled box: you put a value in, and later refer to it by its label instead of repeating the value.

### Declaring a Variable

In Luma you declare a variable by writing its **type**, then its **name**, then `=`, then the value:

```luma
integer score = 10
string player = "Ada"
number pi_ish = 3.14
boolean finished = false
```

Read that first line as "an integer called `score` equal to 10". This *type-first* shape is a hallmark of Luma — the type is always right there, so both you and the language know exactly what the box holds.

> **Note:** Some languages use keywords like `let` or `var` to introduce a variable. Luma does not — you name the type instead. If you write `let`, Luma will gently remind you it isn't a keyword.

### Immutable by Default

Here is a safety feature that may surprise you: once you set a variable, you **cannot change it** by default. It is *immutable*.

```luma
integer age = 30
age = 31        # Error! 'age' cannot be reassigned.
```

This sounds restrictive, but it prevents a huge class of bugs — values that change unexpectedly behind your back. Most of the data in a well-written program never needs to change, and marking it immutable makes your intent clear.

### Opting In to Change with `mutable`

When you genuinely need a value to change over time — a running total, a counter, a game score — mark it `mutable`:

```luma
mutable integer counter = 0
counter = 1
counter = counter + 1
print(counter)      # 2
```

Now reassignment is allowed. The rule of thumb: **start immutable, and add `mutable` only when you must.** Your future self will thank you.

### Naming Variables

Luma variables use `snake_case`: all lowercase, with underscores between words — `player_name`, `total_score`, `is_ready`. Choose names that describe the value's *meaning*, not its type: `remaining_lives` beats `n`. For booleans, a question-like name reads well: `is_valid`, `has_next`, `game_over`.

> **Try it:** Declare a `mutable number` called `balance` starting at `100.0`, then subtract `25.5` from it and print the result. What do you get, and why the `.0`?

---

## 7 — Arithmetic and Operators

**Operators** are symbols that combine values to produce new ones. You already know most of them from school maths.

### Arithmetic

```luma
print(2 + 3)     # 5   addition
print(10 - 4)    # 6   subtraction
print(6 * 7)     # 42  multiplication
print(9 / 2)     # 4   division (integers throw away the remainder!)
print(9 % 2)     # 1   remainder (modulo): what's left after dividing
print(9.0 / 2.0) # 4.5 division of numbers keeps the fraction
```

Two things worth remembering:

- **Integer division truncates.** `9 / 2` is `4`, because both sides are integers and Luma discards the `.5`. If you want `4.5`, make at least one side a `number`: `9 / 2.0`.
- **The modulo operator `%`** gives the remainder. It is wonderfully useful: `n % 2 == 0` tests whether `n` is even, and `hour % 12` wraps a clock.

When you mix an integer and a number, the integer is promoted and the result is a `number`:

```luma
print(3 + 0.5)   # 3.5
```

For powers, roots, and other maths, use the `Math` module (see [chapter 20](#20--a-tour-of-the-standard-library)); there is no `**` operator.

### Comparison

Comparison operators ask a yes/no question and produce a **boolean**:

| Operator | Meaning                    | Example    | Result  |
| -------- | -------------------------- | ---------- | ------- |
| `==`     | equal to                   | `3 == 3`   | `true`  |
| `!=`     | not equal to               | `3 != 4`   | `true`  |
| `<`      | less than                  | `2 < 5`    | `true`  |
| `>`      | greater than               | `2 > 5`    | `false` |
| `<=`     | less than or equal to      | `5 <= 5`   | `true`  |
| `>=`     | greater than or equal to   | `4 >= 9`   | `false` |

### Logical

Logical operators combine booleans:

- **`&&` (and)** is `true` only if *both* sides are true.
- **`||` (or)** is `true` if *at least one* side is true.
- **`!` (not)** flips a boolean: `!true` is `false`.

```luma
boolean can_enter = age >= 18 && has_ticket
boolean day_off = is_weekend || is_holiday
print(!finished)
```

### Combining and Grouping

Operators have a **precedence** (order of evaluation), just like maths does `*` before `+`. When in doubt, use parentheses to make your intent explicit and your code readable:

```luma
number total = (base + bonus) * tax_rate
```

> **Try it:** Predict the output of `print(2 + 3 * 4)` and `print((2 + 3) * 4)`, then run them. Did precedence behave as you expected?

---

## 8 — Working with Text

Text (strings) is everywhere in programs, so Luma gives it special comfort.

### String Interpolation

Rather than gluing strings together by hand, you can drop a value straight into a string using `${...}`. This is called **interpolation**:

```luma
string name = "Ada"
integer age = 36
print("My name is ${name} and I am ${age} years old.")
```

```text
My name is Ada and I am 36 years old.
```

Anything inside `${ }` is evaluated and its result inserted. You can put whole expressions in there:

```luma
number price = 4.50
integer quantity = 3
print("Total: ${price * quantity}")   # Total: 13.5
```

Interpolation is almost always clearer than joining pieces with `+`, so prefer it.

### Escape Sequences

Some characters need a backslash `\` to write. The common ones:

- `\n` — a new line
- `\t` — a tab
- `\"` — a literal double quote inside a string
- `\\` — a literal backslash
- `\$` — a literal dollar sign (so it isn't treated as interpolation)

```luma
print("Line one\nLine two")
print("She said \"hi\".")
print("Price: \$5")
```

### Multi-Line Strings

For longer text, triple quotes `"""` let a string span many lines, and Luma tidily removes the common leading indentation for you:

```luma
string poem = """
    Roses are red,
    Violets are blue,
    Luma is typed,
    And safe for you.
    """
print(poem)
```

### Common String Operations

The `String` module is a toolbox of text operations. A few you'll reach for immediately:

```luma
string raw = "  Hello, World  "
print(String.trim(raw))                 # "Hello, World" (no surrounding spaces)
print(String.length("hello"))           # 5
print(String.uppercase("hello"))        # "HELLO"
print(String.lowercase("HELLO"))        # "hello"
print(String.contains("hello", "ell"))  # true
```

You can split a string into a list of pieces, and join a list back into a string:

```luma
array<string> words = String.split("a,b,c", ",")   # ["a", "b", "c"]
string joined = String.join(words, " - ")          # "a - b - c"
```

We will meet the `array` type properly in [chapter 11](#11--arrays). For now, notice how readable these operations are — the names say what they do.

> **Try it:** Ask yourself how you'd turn `"jane doe"` into `"Jane Doe"`. Peek at the `String` functions in the [Standard Library Reference](Luma_Standard_Library_Reference.md) and see if there's one that helps.

## 9 — Making Decisions

Programs become interesting when they can choose between paths. That's the job of `if`.

### `if` and `else`

An `if` runs a block of code *only when* a condition is `true`. An optional `else` block runs when it is `false`:

```luma
integer temperature = 28

if temperature > 25 {
    print("It's warm out.")
} else {
    print("Bring a jacket.")
}
```

The condition after `if` must be a boolean — exactly the kind of value the comparison operators from [chapter 7](#7--arithmetic-and-operators) produce.

### `else if` Chains

To test several conditions in order, chain them with `else if`. Luma checks each in turn and runs the first block whose condition is true:

```luma
number score = 82

if score >= 90 {
    print("Grade: A")
} else if score >= 80 {
    print("Grade: B")
} else if score >= 70 {
    print("Grade: C")
} else {
    print("Grade: F")
}
```

### `if` as an Expression

Here is something powerful that many languages lack: in Luma, `if` can *produce a value*. The last expression in each branch becomes the branch's result, and you can assign the whole thing to a variable:

```luma
number score = 82
string outcome = if score >= 60 { "pass" } else { "fail" }
print(outcome)      # pass
```

This is often cleaner than declaring a `mutable` variable and assigning to it inside each branch. Notice there is no `mutable` here — `outcome` is set once, to whichever value the `if` chose.

> **Try it:** Write an `if`/`else if`/`else` that prints whether a `number` called `balance` is `"positive"`, `"zero"`, or `"negative"`. Then rewrite it as a single `if`-expression assigned to a `string`.

---

## 10 — Repeating Work

Computers shine at doing the same thing many times without getting bored. **Loops** express repetition.

### Looping Over a Range

A **range** like `0..5` describes the integers from `0` up to *but not including* `5` — that is, `0, 1, 2, 3, 4`. A `for` loop walks through them:

```luma
for i in 0..5 {
    print("Step ${i}")
}
```

```text
Step 0
Step 1
Step 2
Step 3
Step 4
```

If you want the end value *included*, use `..=`:

```luma
for i in 1..=3 {
    print(i)        # 1, then 2, then 3
}
```

The loop variable `i` is an `integer` — ranges are exactly the "counting positions" job integers are for.

> **Note:** If you don't actually use the loop variable, name it `_` (a single underscore) to say "I'm counting, but I don't care about the number." Luma warns about unused names, and `_` tells it the omission is deliberate:
>
> ```luma
> for _ in 0..3 {
>     print("tick")
> }
> ```

### Looping Over a Collection

More often you loop over the items *in* a collection directly:

```luma
array<string> names = ["Ada", "Alan", "Grace"]

for name in names {
    print("Hello, ${name}!")
}
```

If you need the position as well as the item, ask for both:

```luma
for index, name in names {
    print("${index}: ${name}")
}
```

```text
0: Ada
1: Alan
2: Grace
```

### `while` Loops

A `while` loop repeats *as long as* a condition stays true. Use it when you don't know in advance how many times you'll loop:

```luma
mutable integer countdown = 3

while countdown > 0 {
    print(countdown)
    countdown = countdown - 1
}
print("Lift off!")
```

Because `countdown` changes each time, it must be `mutable`. **Make sure the condition eventually becomes false**, or the loop runs forever.

### `break` and `continue`

Two keywords give you finer control inside any loop:

- **`break`** stops the loop immediately.
- **`continue`** skips the rest of the current turn and moves to the next.

```luma
for n in 0..10 {
    if n == 5 {
        break           # stop entirely once we reach 5
    }
    if n % 2 == 1 {
        continue        # skip odd numbers
    }
    print(n)            # prints 0, 2, 4
}
```

> **Try it:** Use a `for` loop to add up the numbers `1` through `10` into a `mutable integer total`, then print it. (You should get `55`.)

---

## 11 — Arrays

An **array** is an ordered list of values, *all of the same type*. Arrays are the workhorse collection of programming — todo items, scores, names, rows of data.

### Creating and Reading

Write an array with square brackets. Its type is `array<T>`, where `T` is the type of its elements:

```luma
array<integer> primes = [2, 3, 5, 7, 11]
array<string> pets = ["cat", "dog", "fish"]
array<number> empty = []
```

Access an element by its **index** — its position, counting from `0`. So the first element is at index `0`, the second at `1`, and so on:

```luma
print(pets[0])      # "cat"
print(pets[1])      # "dog"
```

> **Watch out:** Indexes start at `0`, not `1`. The last index of a list with 3 items is `2`. Asking for `pets[3]` here is out of bounds and stops the program with an error. When you're not certain an index is valid, use the safe `Array.get` described below.

### How Many? Looping

`Array.length` tells you how many elements there are (an `integer`, naturally):

```luma
print(Array.length(pets))    # 3
```

And you already know how to visit each element with a `for` loop:

```luma
for pet in pets {
    print(pet)
}
```

### Arrays Don't Change — They Copy

Here's a Luma habit worth understanding early: collections are **immutable**. Operations that seem to "modify" an array actually return a *brand-new* array, leaving the original untouched. For example, `Array.push` gives you a new array with one more element:

```luma
array<integer> a = [1, 2, 3]
array<integer> b = Array.push(a, 4)
print(a)     # [1, 2, 3]  — unchanged
print(b)     # [1, 2, 3, 4] — the new one
```

To "grow" an array in a loop, keep a `mutable` variable and reassign it to each new version:

```luma
mutable array<integer> squares = []
for i in 1..=5 {
    squares = Array.push(squares, i * i)
}
print(squares)      # [1, 4, 9, 16, 25]
```

### Safe Access with `Array.get`

Instead of risking an out-of-bounds crash with `pets[99]`, `Array.get` returns a *result* you can handle gracefully. We cover results fully in [chapter 18](#18--handling-absence-and-failure); the short version is that `?? fallback` supplies a value to use if the index is missing:

```luma
string third = Array.get(pets, 2) ?? "unknown"     # "fish"
string tenth = Array.get(pets, 10) ?? "unknown"    # "unknown" — no crash
```

### A Few Handy Array Tools

| Operation                       | What it does                          | Returns          |
| ------------------------------- | ------------------------------------- | ---------------- |
| `Array.length(a)`               | number of elements                    | `integer`        |
| `Array.push(a, v)`              | new array with `v` added at the end   | `array<T>`       |
| `Array.contains(a, v)`          | is `v` in the array?                  | `boolean`        |
| `Array.reverse(a)`              | new array, reversed                   | `array<T>`       |
| `Array.get(a, i)`               | safe element access                   | `result<T>`      |
| `Array.sum(a)`                  | total of a numeric array              | `result<number>` |
| `Array.join(a, sep)`            | join elements into a string           | `string`         |

We'll meet the transforming tools — `Array.map`, `Array.filter`, `Array.reduce` — in [chapter 19](#19--lambdas-pipes-and-higher-order-functions), once we have functions under our belt.

> **Try it:** Build an array of your three favourite foods, print how many there are, then print them one per line with their position number.

---

## 12 — Dictionaries

An array looks things up by *position*. A **dictionary** looks things up by *name* — a **key**. Each entry pairs a text key with a value. Dictionaries are perfect for "look up X and get Y": a phone book, word counts, settings.

### Creating and Reading

A dictionary's type is `dictionary<V>`, where `V` is the type of the *values*. Keys are always strings. Write one with `{ "key": value, ... }`:

```luma
dictionary<number> ages = {
    "Ada": 36.0,
    "Alan": 41.0,
    "Grace": 45.0
}
```

Look up a value by key. Since a key might not exist, use `Dictionary.get_or` to supply a fallback:

```luma
number ada = Dictionary.get_or(ages, "Ada", 0.0)      # 36.0
number nobody = Dictionary.get_or(ages, "Zed", 0.0)   # 0.0 (missing → fallback)
```

### Adding and Updating

Like arrays, dictionaries are immutable — `Dictionary.set` returns a *new* dictionary with the change:

```luma
dictionary<number> updated = Dictionary.set(ages, "Zed", 29.0)
```

To build one up in a loop, reassign a `mutable` variable each time.

### Looping Over a Dictionary

You can loop over the keys and values together:

```luma
for name, age in ages {
    print("${name} is ${age} years old")
}
```

Or get the keys or values as arrays:

```luma
array<string> everyone = Dictionary.keys(ages)
array<number> all_ages = Dictionary.values(ages)
```

### Handy Dictionary Tools

| Operation                    | What it does                            | Returns         |
| ---------------------------- | --------------------------------------- | --------------- |
| `Dictionary.get_or(d, k, x)` | value for key `k`, or `x` if absent     | `V`             |
| `Dictionary.set(d, k, v)`    | new dictionary with `k` set to `v`      | `dictionary<V>` |
| `Dictionary.keys(d)`         | all keys                                | `array<string>` |
| `Dictionary.values(d)`       | all values                              | `array<V>`      |
| `Dictionary.length(d)`       | number of entries                       | `integer`       |
| `Dictionary.remove(d, k)`    | new dictionary without key `k`          | `dictionary<V>` |

> **Try it:** Make a `dictionary<number>` mapping three fruits to their prices. Print each fruit and price on its own line, then print the total of all prices (hint: `Dictionary.values` gives you an array you can total).

---

## 13 — Functions

As programs grow, you'll want to name a useful chunk of work and reuse it. That's a **function**: a named recipe that takes some inputs, does something, and usually gives back a result. You've been *using* functions (`print`, `String.trim`); now you'll *write* your own.

### Declaring a Function

The shape is: `function` , then the **return type** (the type of value it gives back), then the **name**, then the **parameters** in parentheses, then the body:

```luma
function integer square(integer n) {
    return n * n
}
```

Read it as "a function returning an `integer`, called `square`, taking an `integer` named `n`." The `return` keyword hands a value back to whoever called the function. Now you can *call* it as many times as you like:

```luma
print(square(5))       # 25
print(square(9))       # 81
```

> **Important:** In Luma the return type comes **before** the function name, never after. There's no `-> ReturnType` on a function declaration. (You *will* see `->` later, but only for lambdas and type annotations.)

### Parameters

A function can take several **parameters** — the inputs it needs — each written type-first, separated by commas:

```luma
function number rectangle_area(number width, number height) {
    return width * height
}

print(rectangle_area(3.0, 4.0))    # 12.0
```

### Functions That Return Nothing

Some functions just *do* something and return no value — like `print` does. Their return type is `void`, and they need no `return`:

```luma
function void greet(string name) {
    print("Hello, ${name}!")
}

greet("Ada")       # Hello, Ada!
```

That's exactly why `main` is declared `function void main()` — it orchestrates work but hands back no value.

### Why Functions?

Functions pay off in three ways:

- **Reuse** — write it once, call it everywhere.
- **Naming** — `rectangle_area(w, h)` says what it means; the bare `w * h` doesn't.
- **Focus** — each function does one job, so you can reason about it in isolation.

### Calling Functions from Functions

Functions can call other functions, including themselves. A function that calls itself is **recursive** — useful for problems that break into smaller copies of the same problem, like factorial (`5! = 5 × 4 × 3 × 2 × 1`):

```luma
function integer factorial(integer n) {
    if n <= 1 {
        return 1
    }
    return n * factorial(n - 1)
}

print(factorial(5))     # 120
```

Every recursive function needs a **base case** — here, `n <= 1` — that stops the recursion, or it would call itself forever.

Here is a complete program that ties functions together:

```luma
function integer square(integer n) {
    return n * n
}

function integer sum_of_squares(integer a, integer b) {
    return square(a) + square(b)
}

@main
function void main() {
    print("3² + 4² = ${sum_of_squares(3, 4)}")   # 25
}
```

> **Try it:** Write a function `function boolean is_even(integer n)` that returns whether `n` is even (hint: `n % 2 == 0`). Call it inside `main` with a few numbers.

## 14 — Records

So far each variable holds a single value. Real things have *several* attributes at once: a person has a name *and* an age; a point has an *x* *and* a *y*. A **record** groups related values into one named type.

### Defining and Creating

Define a record with the `record` keyword, listing each **field** type-first:

```luma
record Point {
    number x,
    number y
}
```

Record names use `PascalCase` (each word capitalised). Create a value by naming the fields:

```luma
Point origin = Point { x = 0.0, y = 0.0 }
Point corner = Point { x = 3.0, y = 4.0 }
```

Read a field with a dot:

```luma
print(corner.x)     # 3.0
print("(${corner.x}, ${corner.y})")   # (3.0, 4.0)
```

### Records in Functions

Records make functions expressive — you pass one meaningful thing instead of a loose bundle of values:

```luma
record Point {
    number x,
    number y
}

function number distance_from_origin(Point p) {
    return Math.square_root(p.x * p.x + p.y * p.y) ?? 0.0
}

@main
function void main() {
    Point p = Point { x = 3.0, y = 4.0 }
    print(distance_from_origin(p))     # 5.0
}
```

### Updating with `with`

Records are values: assigning one copies it, and comparing two compares their contents. Like other data in Luma, a record's fields don't change in place. To get a *modified copy*, use a `with` expression — it produces a new record with some fields replaced:

```luma
Point start = Point { x = 0.0, y = 0.0 }
Point moved = start with { x = 5.0 }

print(start.x)      # 0.0 — original untouched
print(moved.x)      # 5.0 — new copy
```

### Default Field Values

A field can declare a default, which is used when you leave it out:

```luma
record Config {
    string host = "localhost",
    integer port = 8080,
    boolean verbose = false
}

Config defaults = Config {}                    # all defaults
Config custom = Config { port = 3000 }         # override just the port
```

> **Try it:** Define a `record Book { string title, string author, number price }`. Create one, print a nice one-line summary using interpolation, then make a discounted copy with `with` (say, price minus `2.0`) and print both prices.

---

## 15 — Tuples

Sometimes you want to bundle a few values together *without* going to the trouble of defining a whole record — most commonly to return more than one value from a function. A **tuple** is a lightweight, fixed-size group of values that may have different types.

Its type is written as the types in parentheses, and you create one the same way:

```luma
(string, number) person = ("Ada", 36.0)
```

Access the pieces by position with `.0`, `.1`, and so on:

```luma
print(person.0)     # "Ada"
print(person.1)     # 36.0
```

Often it's tidier to **destructure** a tuple — pull its pieces into named variables in one step:

```luma
(string name, number age) = person
print("${name} is ${age}")
```

Tuples are ideal for returning two related results at once:

```luma
function (integer, integer) divide_with_remainder(integer a, integer b) {
    return (a / b, a % b)
}

@main
function void main() {
    (integer quotient, integer remainder) = divide_with_remainder(17, 5)
    print("17 ÷ 5 = ${quotient} remainder ${remainder}")   # 3 remainder 2
}
```

Reach for a **record** when the group is meaningful and reused (give the fields names); reach for a **tuple** for a quick, local pairing.

---

## 16 — Choice Types

Often a value should be *exactly one of several possibilities*: a traffic light is red, amber, or green; a card suit is one of four. A **choice type** models "one of these options" precisely, so impossible combinations simply can't occur.

### Simple Choices (Like an Enum)

List the options, called **variants**, inside `choice`:

```luma
choice Direction {
    North
    South
    East
    West
}
```

Refer to a variant with the type name and a dot. A variable of that type can only ever hold one of the listed variants — nothing else is possible:

```luma
Direction heading = Direction.North
```

This is far safer than using strings like `"north"`, where a typo (`"nrth"`) would slip through. With a choice type, `Direction.Nrth` isn't a valid value, so the mistake is caught immediately.

### Choices That Carry Data

Variants can also carry extra information. Each variant may hold its own values, so a single type can describe shapes of different "shapes":

```luma
choice Shape {
    Circle(number radius)
    Rectangle(number width, number height)
    Point
}
```

Create them by supplying the data (always qualified with the type name):

```luma
Shape a = Shape.Circle(2.0)
Shape b = Shape.Rectangle(3.0, 4.0)
Shape c = Shape.Point
```

Now `Shape` neatly captures "a circle *with a radius*, or a rectangle *with width and height*, or a dimensionless point" — and every `Shape` is exactly one of them. To *use* the data inside, we need one more tool: pattern matching, next.

---

## 17 — Pattern Matching

`match` is Luma's most elegant feature. It compares a value against a series of **patterns** and runs the branch that fits — a bit like a supercharged `if`/`else if`. Its real power is with choice types, where it also *unpacks* the data inside a variant.

### Matching Choice Variants

```luma
choice Shape {
    Circle(number radius)
    Rectangle(number width, number height)
    Point
}

function number area(Shape s) {
    return match s {
        case Shape.Circle(r) { Math.pi * r * r }
        case Shape.Rectangle(w, h) { w * h }
        case Shape.Point { 0.0 }
    }
}

@main
function void main() {
    print(area(Shape.Circle(2.0)))          # 12.566...
    print(area(Shape.Rectangle(3.0, 4.0)))  # 12.0
}
```

Look at `case Shape.Circle(r)`: it both *tests* whether `s` is a circle *and*, if so, binds its radius to a new variable `r` you can use in that branch. Each branch's last expression is its value, and `match` as a whole produces one — so we can `return` it directly.

### Exhaustiveness — a Safety Net

Luma checks that your `match` covers **every** possibility. If you forget the `Point` case above, the program won't even compile — Luma tells you a case is missing. This means you can *never* accidentally forget a variant, even months later when you add a new one. This guarantee is a big part of what "safe as Rust" means.

### Matching Values with `else`

`match` also works on plain values like numbers and strings. Because there are infinitely many numbers, these need an `else` branch to catch everything not listed. You can match ranges with comparison patterns:

```luma
function string grade(number score) {
    return match score {
        case >= 90.0 { "A" }
        case >= 80.0 { "B" }
        case >= 70.0 { "C" }
        else         { "F" }
    }
}
```

Or match specific literal values:

```luma
function string describe(integer n) {
    return match n {
        case 0 { "zero" }
        case 1 { "one" }
        case 2 { "two" }
        else   { "many" }
    }
}
```

A `match` expression is frequently clearer than a long `if`/`else if` chain — and the exhaustiveness check keeps you honest.

> **Try it:** Define `choice TrafficLight { Red Amber Green }` and a function returning the advice for each (`"stop"`, `"ready"`, `"go"`). Leave one case out on purpose and watch Luma catch it.

---

## 18 — Handling Absence and Failure

Two situations arise constantly in real programs: a value might be **missing**, and an operation might **fail**. Many languages handle these with special "null" values or crashes that surface far from the cause. Luma makes both explicit in the type system, so you can't forget to deal with them. This is where the "safe" in Luma really earns its keep.

### `optional` — a Value That Might Be Absent

An `optional<T>` holds either a value (`some(x)`) or nothing (`none`):

```luma
optional<string> found = some("Ada")
optional<string> missing = none
```

Because the type says "maybe," you're reminded to handle the empty case. The quickest way is `??`, which supplies a fallback when the value is absent:

```luma
string name = missing ?? "Anonymous"     # "Anonymous"
```

Or handle both cases explicitly with `match`:

```luma
match found {
    case some(value) { print("Found ${value}") }
    case none { print("Nothing here") }
}
```

### `result` — an Operation That Might Fail

A `result<T>` represents an operation that either **succeeded** with a value (`success(x)`) or **failed** with a message (`failure("...")`). This is Luma's primary way to report errors that can reasonably happen — bad input, a missing file, an empty list. Many standard-library functions return a `result` for exactly this reason.

For instance, turning text into a number can fail (what number is `"hello"`?), so `String.parse_integer` returns a `result<integer>`:

```luma
match String.parse_integer("42") {
    success(n) { print("Parsed ${n}") }        # Parsed 42
    failure(message) { print("Oops: ${message}") }
}
```

### Three Ways to Handle a `result`

You'll use these constantly, so let's see all three:

**1. `??` — supply a default.** The simplest: if it failed, use this value instead.

```luma
integer count = String.parse_integer("oops") ?? 0     # 0
```

This is why earlier chapters wrote things like `Array.sum(scores) ?? 0.0` and `Array.get(pets, 2) ?? "unknown"` — those functions return results, and `??` unwraps them with a fallback.

**2. `match` — handle success and failure separately.** Best when the two cases need different logic (as shown above).

**3. `?` — pass the failure up.** Inside a function that *itself* returns a `result`, the `?` operator says "if this failed, stop and return that failure; otherwise give me the value." It lets you chain fallible steps without a pyramid of checks:

```luma
function result<integer> add_parsed(string a, string b) {
    integer x = String.parse_integer(a)?      # bail out if this fails
    integer y = String.parse_integer(b)?      # ...or this
    return success(x + y)
}
```

### Errors vs. Crashes

`result` and `optional` are for *expected* problems you can recover from — the everyday stuff. Truly *unexpected* situations (a bug, running out of memory) are a different category, handled by `try`/`catch`, which you can read about in the [Error Handling](Luma_Error_Handling.md) guide once you're comfortable. As a beginner, prefer `result` and `optional`; they cover almost everything and keep your programs honest.

> **Try it:** Write `function result<number> safe_divide(number a, number b)` that returns `failure("divide by zero")` when `b` is `0.0`, and `success(a / b)` otherwise. Call it both ways and handle the outcome with `match`.

---

## 19 — Lambdas, Pipes, and Higher-Order Functions

Now that you can write functions and handle results, let's combine them into Luma's most expressive style for working with data.

### Lambdas — Functions Without a Name

A **lambda** is a small, unnamed function you write inline, right where it's needed. The syntax is the parameters, then `->`, then the expression it returns:

```luma
(integer x) -> x * 2
```

That's a function that doubles its input. You can store one in a variable — and *here* is where the `->` arrow lives (in lambdas and type annotations, never on a `function` declaration):

```luma
function(integer) -> integer double = (integer x) -> x * 2
print(double(10))      # 20
```

### Higher-Order Functions on Arrays

Lambdas shine when passed to **higher-order functions** — functions that take other functions as arguments. The big three transform whole arrays at once. Each returns a `result` (a lambda *could* misbehave), so unwrap with `?? []`:

**`Array.map`** applies a function to every element, producing a new array:

```luma
array<integer> nums = [1, 2, 3, 4]
array<integer> doubled = Array.map(nums, (integer n) -> n * 2) ?? []
print(doubled)     # [2, 4, 6, 8]
```

**`Array.filter`** keeps only the elements for which the function returns `true`:

```luma
array<integer> evens = Array.filter(nums, (integer n) -> n % 2 == 0) ?? []
print(evens)       # [2, 4]
```

**`Array.reduce`** combines all elements into a single value, starting from an initial accumulator:

```luma
integer total = Array.reduce(nums, 0, (integer acc, integer n) -> acc + n) ?? 0
print(total)       # 10
```

These three replace a lot of manual loops, and they read like a description of *what* you want rather than *how* to compute it.

### The Pipe Operator

When you chain several transformations, nesting the calls gets hard to read inside-out. The **pipe operator** `|>` flips it around: it takes the value on its left and feeds it in as the first argument of the call on its right. So `x |> f()` means `f(x)`.

Compare these two ways of writing "take these words, and join them with commas":

```luma
array<string> words = ["a", "b", "c"]

# Nested — read inside-out:
string a = String.join(words, ", ")

# Piped — read left-to-right:
string b = words |> String.join(", ")
```

For a single step the difference is small, but pipelines make a *sequence* of steps read like a recipe, top to bottom. When every step returns a plain value, they chain cleanly:

```luma
string tidy = "  Hello, World  "
    |> String.trim()          # "Hello, World"
    |> String.uppercase()     # "HELLO, WORLD"

print(tidy)     # HELLO, WORLD
```

### Threading Results Through a Pipe

There's one wrinkle. The pipe always calls a *function* on its right, so you can't drop the `?? default` operator between stages — you need a *function* that unwraps a `result`. That function is `Result.unwrap_or(default)`, the pipe-friendly twin of `??`. Since `Array.map`, `Array.filter`, and `Array.sum` each return a `result`, unwrap after each one:

```luma
array<number> prices = [10.0, 20.0, 30.0]

number total = prices
    |> Array.map((number p) -> p * 1.2)   # add 20% tax → result<array<number>>
    |> Result.unwrap_or([])               # unwrap to a plain array
    |> Array.sum()                        # total it → result<number>
    |> Result.unwrap_or(0.0)              # unwrap to a plain number

print(total)        # 72.0
```

Prefer pipes when you have three or more chained steps; they turn tangled nesting into a clear flow.

> **Try it:** Starting from `[1, 2, 3, 4, 5, 6]`, use `Array.filter` to keep the even numbers, `Array.map` to square them, and `Array.sum` to add them up — chained with `|>`, unwrapping each step with `|> Result.unwrap_or(...)`. (You should get `4 + 16 + 36 = 56`.)

## 20 — A Tour of the Standard Library

You don't have to build everything from scratch. Luma ships with a large **standard library**: ready-made functions grouped into **modules** such as `String`, `Math`, and `Array`. You call a function by naming its module, a dot, and the function: `Math.maximum(3.0, 9.0)`. You've been doing this all along.

A recurring pattern to remember: **many library functions return a `result`** (because they can fail), so you unwrap them with `?? fallback`, exactly as in [chapter 18](#18--handling-absence-and-failure). The tables below note which functions do.

### `Console` — Talking to the Terminal

Beyond `print`, the `Console` module reads what the user types. `Console.prompt` shows a message and hands back what they enter. It returns a `result` (the terminal could be closed), so unwrap it with `??`:

```luma
string line = Console.prompt("What's your name? ") ?? ""
print("Hello, ${line}!")
```

We devote the whole of [chapter 21](#21--reading-input-from-the-user) to reading input, since it's how interactive programs come alive.

### `String` — Text

```luma
print(String.trim("  hi  "))               # "hi"
print(String.uppercase("hello"))           # "HELLO"
print(String.length("hello"))              # 5
print(String.contains("hello", "ell"))     # true
print(String.replace("a-b-c", "-", "+"))     # "a+b-c"  (first match only)
print(String.replace_all("a-b-c", "-", "+")) # "a+b+c"  (every match)
print(String.split("a,b,c", ","))          # ["a", "b", "c"]
```

### `Math` — Numbers

The `Math` module offers constants and functions. **Constants** are accessed *without* parentheses; several functions return a `result` (they can fail — a square root of a negative number, say), so unwrap with `??`:

```luma
print(Math.pi)                          # 3.141592653589793  (a constant)
print(Math.maximum(3.0, 9.0))               # 9.0                (plain number)
print(Math.minimum(3.0, 9.0))               # 3.0
print(Math.square_root(144.0) ?? 0.0)   # 12.0               (result → unwrap)
print(Math.power(2.0, 10.0) ?? 0.0)     # 1024.0
print(Math.absolute(-7.0) ?? 0.0)       # 7.0
```

### `Random` — Chance

Great for games. `Random.generate_integer` picks a whole number in an *inclusive* range and returns a `result`:

```luma
integer die = Random.generate_integer(1, 6) ?? 1     # 1..6 inclusive
number chance = Random.generate_number()             # a number in [0, 1)
boolean coin = Random.generate_boolean()             # true or false
```

### `Converter` — Changing Types

Turn one type of value into another. Conversions that could fail (text that isn't a number) return a `result`:

```luma
string s = Converter.to_string(255)          # "255"      (always works)
integer n = Converter.to_integer("100") ?? 0 # 100        (result → unwrap)
number x = Converter.to_number("3.14") ?? 0.0# 3.14
```

### `Array` and `Dictionary`

You met these in chapters [11](#11--arrays), [12](#12--dictionaries), and [19](#19--lambdas-pipes-and-higher-order-functions). They are among the richest modules — `Array.map`, `Array.filter`, `Array.reduce`, `Array.sort`, `Array.contains`, and many more.

### There's Much More

These are just the beginner essentials. Luma has dozens of modules; here are some you'll enjoy exploring as you grow:

| Module      | For working with…                                    |
| ----------- | ---------------------------------------------------- |
| `DateTime`  | dates, times, and durations                          |
| `FileSystem`| reading and writing files                            |
| `Json`      | JSON data (reading and producing it)                 |
| `Set`       | collections of unique values                         |
| `Queue`     | first-in-first-out sequences                         |
| `Stack`     | last-in-first-out sequences                          |
| `Log`       | structured logging                                   |
| `Terminal`  | rich, colourful terminal interfaces                  |
| `GraphicalUi` and `Solaris` | graphical desktop applications       |

The [Standard Library Reference](Luma_Standard_Library_Reference.md) documents every module and every function, with the exact types. Get comfortable skimming it — knowing *where to look* is a real programming skill.

> **Try it:** Roll two dice with `Random.generate_integer(1, 6)` and print their sum. Run it a few times and watch the total change.

---

## 21 — Reading Input from the User

A program that only prints is a monologue. To hold a conversation, it must *read* what the user types. `Console.prompt` shows a message and waits for a line of input.

Because input can fail (for instance, the input stream ends), `Console.prompt` returns a `result<string>`. Unwrap it with `??` to supply a fallback:

```luma
string name = Console.prompt("What is your name? ") ?? "friend"
print("Hello, ${name}!")
```

### Reading Numbers

Input always arrives as *text*. To do arithmetic, convert it to a number with `String.parse_integer` (or `String.parse_number`). That, too, can fail — the user might type `"banana"` — so handle both outcomes with `match`:

```luma
string raw = Console.prompt("Enter your age: ") ?? ""

match String.parse_integer(String.trim(raw)) {
    success(age) { print("Next year you'll be ${age + 1}.") }
    failure(_message) { print("That wasn't a whole number.") }
}
```

Note `String.trim` — it removes stray spaces and the trailing newline so parsing is reliable. Trimming input before parsing is a good habit.

Here is a small but complete interactive program:

```luma
@main
function void main() {
    string name = Console.prompt("Your name: ") ?? "friend"
    string raw = Console.prompt("Your lucky number: ") ?? ""

    match String.parse_integer(String.trim(raw)) {
        success(n) {
            print("${name}, your number doubled is ${n * 2}.")
        }
        failure(_message) {
            print("${name}, that wasn't a number — but nice to meet you!")
        }
    }
}
```

> **Try it:** Ask the user for two numbers on separate prompts and print their sum. Handle the case where either isn't a valid number.

---

## 22 — Testing Your Code

How do you know your code actually works — and *keeps* working after you change it? You **test** it. Luma has testing built right in, so you don't need any extra tools.

### Writing a Test

Mark a function with the `@test` annotation and use `assert` to state something that *must* be true. If the assertion holds, the test passes; if not, it fails and tells you where:

```luma
function integer add(integer a, integer b) {
    return a + b
}

@test
function void test_add() {
    assert(add(2, 3) == 5)
    assert(add(-1, 1) == 0)
}
```

Test files use `@test` functions and don't need a `@main`. Run them with the `--test` flag:

```bash
luma --test my_tests.luma
```

```text
  pass  test_add

1 test(s): 1 passed
```

### Why Bother?

Tests are a safety net. When you later change `add` — or any code it relies on — rerunning the tests instantly tells you whether you broke anything. Good tests let you improve code fearlessly. Start small: whenever you write a function with clear inputs and outputs, add a `@test` that checks a couple of cases.

```luma
function boolean is_even(integer n) {
    return n % 2 == 0
}

@test
function void test_is_even() {
    assert(is_even(4))          # should be true
    assert(!is_even(7))         # should be false
}
```

> **Try it:** Write a `function integer clamp(integer value, integer low, integer high)` that keeps `value` within `[low, high]`, then write a `@test` checking values below, inside, and above the range.

---

## 23 — Organising Code with Namespaces

As a program grows, it collects more and more functions and types, and their names start to compete for space. A **namespace** groups related declarations together under a shared name — the way a folder groups related files. You have been using namespaces since [chapter 4](#4--printing-and-comments): every standard-library module you have called, from `String` to `Math` to `Array`, *is* a namespace. That's why you write `String.trim` and `Math.maximum`. Now you'll build your own.

### Declaring a Namespace

Wrap related declarations in a `namespace` block. Namespace names use `PascalCase`, like records and choice types:

```luma
namespace Temperature {
    function number to_fahrenheit(number celsius) {
        return celsius * 9.0 / 5.0 + 32.0
    }

    function number to_celsius(number fahrenheit) {
        return (fahrenheit - 32.0) * 5.0 / 9.0
    }
}
```

You call a member by naming the namespace, a dot, and the member — exactly the pattern you already know from the standard library:

```luma
print(Temperature.to_fahrenheit(100.0))   # 212.0
print(Temperature.to_celsius(32.0))        # 0.0
```

Grouping like this does two useful things. It makes your intent obvious — `Temperature.to_celsius` reads better than a lone `to_celsius` adrift among dozens of unrelated functions. And it prevents **name clashes**: two different namespaces can each define a `describe` or a `format` without colliding.

### Namespaces Hold Types Too

A namespace isn't limited to functions. It can also contain records, choice types, and type aliases. You refer to them with the same dotted syntax:

```luma
namespace Inventory {
    record Item {
        string name,
        number price
    }

    function string describe(Item it) {
        return "${it.name} costs ${it.price}"
    }
}

@main
function void main() {
    Inventory.Item gadget = Inventory.Item { name = "Gadget", price = 9.99 }
    print(Inventory.describe(gadget))     # Gadget costs 9.99
}
```

From outside the namespace you always use the qualified name: the type is `Inventory.Item`, and you build a value with `Inventory.Item { ... }`. *Inside* the namespace, members may refer to a sibling type by its short name — which is why the `describe` parameter is written simply as `Item`.

### Importing Names with `use`

Writing the namespace prefix every time can get repetitive. The `use` declaration imports names so you can call them **bare** (unqualified). Import an entire namespace at once:

```luma
use Temperature

@main
function void main() {
    print(to_fahrenheit(20.0))              # 68.0  — no prefix needed
    print(Temperature.to_fahrenheit(100.0)) # 212.0 — the qualified form still works
}
```

Or import just a single member with `use Namespace.member`:

```luma
use Temperature.to_celsius

@main
function void main() {
    print(to_celsius(212.0))                # 100.0
}
```

Use `use` sparingly. The qualified `Temperature.to_fahrenheit` is often clearer to a reader, because it says *where* the function comes from. Reach for `use` when a single namespace dominates a file and the repeated prefix becomes noise.

> **Note:** If a bare name you import clashes with one you have already defined yourself, Luma reports an error rather than quietly guessing which you meant. The fully qualified `Namespace.member` form always works, no matter what you have imported.

### Hiding Details with `internal`

Some members of a namespace are its *public* face; others are private helpers the outside world shouldn't touch. Mark those helpers `internal`. An internal member is fully usable *inside* its namespace but invisible from outside:

```luma
namespace Account {
    function string summary(number balance) {
        return "Balance: ${Account.format(balance)} credits"
    }

    internal function string format(number amount) {
        return Converter.to_string(amount)
    }
}

@main
function void main() {
    print(Account.summary(42.5))     # Balance: 42.5 credits
}
```

Here `summary` is public and calls its `internal` helper `format` freely. But reaching in from outside is rejected before the program even runs:

```luma
print(Account.format(42.5))          # Error!
```

```text
'format' is internal to namespace 'Account' and cannot be accessed from outside
```

This is the same instinct as any well-made tool: present a small, deliberate set of buttons on the outside and keep the wiring hidden. It lets you change *how* `format` works later without any risk of breaking code elsewhere — because nothing outside the namespace was ever allowed to depend on it.

> **Try it:** Build a `namespace Circle` with a public `function number area(number radius)` and an `internal` helper `function number squared(number x)` that returns `x * x`. Have `area` call `Circle.squared(radius)` and multiply by `Math.pi`. Call `Circle.area(2.0)` from `main` and check you get about `12.57`.

---

## 24 — Splitting a Program Across Files

So far every program has lived in a single `.luma` file. That's fine for something small, but real projects spread their code across many files — one per feature, or a shared file of helpers reused by several programs. Luma joins files together with the **`include`** directive.

### The `include` Directive

`include` pulls another file's declarations into the current one, as if you had pasted them in. Suppose you keep some shared shapes in a file called `shapes.luma`:

```luma
# shapes.luma — shared declarations, no @main of its own

record Circle {
    number radius
}

function number circle_area(Circle c) {
    return Math.pi * c.radius * c.radius
}
```

Another file can then `include` it and use everything it defines:

```luma
# main.luma

include "shapes.luma"

@main
function void main() {
    Circle small = Circle { radius = 2.0 }
    print("Area: ${circle_area(small)}")   # Area: 12.566370614359172
}
```

Run it the usual way — `luma main.luma` — and Luma stitches the two files together before your program starts. The `record` and the `function` defined in `shapes.luma` are available in `main.luma` exactly as if they had been written there.

### Where Files Are Found

A path in `include` is resolved **relative to the file doing the including** — not to wherever you happen to run the command from. So `include "shapes.luma"` looks for `shapes.luma` in the same folder as `main.luma`. You can reach into subfolders too:

```luma
include "geometry/shapes.luma"
include "helpers/text.luma"
```

For safety, an include path may not climb *out* of its folder with `..`, and it may not be a symbolic link — Luma rejects both. (There is also an advanced `LUMA_PATH` environment variable for shared libraries, described in the [User Manual](Luma_User_Manual.md#25--including-files); you won't need it to start out.)

### Keep Included Files Declaration-Only

A file you include should contain only **declarations** — functions, records, choice types, type aliases, interfaces, and namespaces. It shouldn't define its own `@main` (the program that includes it provides that), and it's best to avoid loose top-level statements such as a bare `print`. Here's why: any top-level statement in an included file runs *before* your `@main`, which is surprising and hard to follow — so Luma warns you when it spots one. Think of included files as toolboxes: they *define* tools; the program that includes them decides *when* to use them.

Each file is included **at most once**, even if several files all ask for it. If `main.luma` includes both `shapes.luma` and `render.luma`, and `render.luma` *also* includes `shapes.luma`, the shapes are merged in just once — no duplication, no error. You are free to include a shared helper from everywhere that needs it.

### Namespaces and Files Together

The two ideas from these last chapters combine naturally. A common pattern is to put a namespace in its own file and include it wherever it's needed:

```luma
# temperature.luma

namespace Temperature {
    function number to_fahrenheit(number celsius) {
        return celsius * 9.0 / 5.0 + 32.0
    }
}
```

```luma
# report.luma

include "temperature.luma"

@main
function void main() {
    print(Temperature.to_fahrenheit(37.0))   # 98.6
}
```

Now the `Temperature` namespace can be reused by any program that includes `temperature.luma`, and its name keeps its members tidy wherever it lands. This is how larger Luma programs stay organised: small, focused files, each holding a namespace or a set of related helpers, included where they're wanted.

> **Try it:** Create two files in the same folder. In `mathutils.luma`, define `function integer double_it(integer n) { return n * 2 }`. In `main.luma`, write `include "mathutils.luma"`, then call `double_it(21)` from `@main` and print the result. Run `luma main.luma` and confirm you see `42`.

---

## 25 — Project — A Number-Guessing Game

Time to put it all together. We'll build a complete game that picks a secret number and lets the player guess, giving "too high" / "too low" hints. This one program uses variables, `mutable` state, a `while` loop, `if`/`else if`, `match` on a `result`, string interpolation, and three standard-library modules — everything from the previous chapters working as a team.

### The Complete Program

```luma
@main
function void main() {
    integer secret = Random.generate_integer(1, 100) ?? 50
    integer max_attempts = 7
    mutable integer attempts = 0
    mutable boolean won = false

    print("I'm thinking of a number between 1 and 100.")
    print("You have ${max_attempts} guesses. Type 'quit' to give up.")

    while attempts < max_attempts && !won {
        string raw = String.trim(Console.prompt("Your guess: ") ?? "quit")

        if raw == "quit" {
            break
        }

        match String.parse_integer(raw) {
            success(guess) {
                attempts = attempts + 1
                integer remaining = max_attempts - attempts

                if guess == secret {
                    won = true
                    print("Correct! You got it in ${attempts} guesses.")
                } else if guess < secret {
                    print("Too low. ${remaining} guesses left.")
                } else {
                    print("Too high. ${remaining} guesses left.")
                }
            }
            failure(_message) {
                print("That's not a whole number — try again.")
            }
        }
    }

    if !won && attempts >= max_attempts {
        print("Out of guesses! The number was ${secret}.")
    }
}
```

### Reading It Top to Bottom

- We pick a `secret` with `Random.generate_integer(1, 100)`, unwrapping the result with `?? 50`.
- `attempts` and `won` are `mutable` because they change as the game unfolds; `secret` and `max_attempts` never change, so they stay immutable.
- The `while` condition `attempts < max_attempts && !won` keeps the game going until the player runs out of guesses *or* wins.
- Each turn we read a line, `String.trim` it, and allow `"quit"` to `break` out early. Reading input as `?? "quit"` also means the game ends cleanly if the input stream runs dry.
- We `match` on `String.parse_integer` so that non-numeric input is handled kindly (the `failure` arm) instead of crashing — and note it doesn't cost the player an attempt.
- The `if`/`else if`/`else` gives the classic high/low hint.
- After the loop, we reveal the number only if the player neither won nor quit.

### Playing It

```text
I'm thinking of a number between 1 and 100.
You have 7 guesses. Type 'quit' to give up.
Your guess: 50
Too high. 6 guesses left.
Your guess: 25
Too low. 5 guesses left.
Your guess: 37
Correct! You got it in 3 guesses.
```

### Make It Your Own

The best way to cement what you've learned is to *extend* this program. A few ideas, from easy to ambitious:

- Congratulate the player differently if they win in three guesses or fewer.
- Track and print how many games they've won across multiple rounds (wrap the game in an outer loop).
- Let the player choose the difficulty (the upper bound) at the start.
- Keep a running list of their past guesses and show it each turn (use an `array`).

---

## 26 — Where to Go Next

You now know the core of Luma: values and types, variables, control flow, functions, collections, your own data types, pattern matching, safe error handling, the standard library, and how to organise a larger program with namespaces and multiple files. That's genuinely most of the language — enough to write real, useful programs.

When you're ready for more, here's the path onward:

- **Read other people's code.** The `examples/` folder in the Luma repository is full of complete, runnable programs — a calculator, a quiz game, a bank ledger, a maze solver — ranging from beginner to advanced. Reading and tweaking them is the fastest way to grow.
- **Deepen the language.** The [User Manual](Luma_User_Manual.md) covers everything this tutorial simplified or skipped: named arguments, type aliases, **interfaces** (shared shapes across types), **generics** (functions and types that work for *any* element type), and **ownership** for fine-grained safety.
- **Handle errors like a pro.** The [Error Handling](Luma_Error_Handling.md) guide goes deep on `result`, `optional`, and when to reach for `try`/`catch`.
- **Do more at once.** Luma has first-class **concurrency** — `task`s and `channel`s for doing several things in parallel. See the [Concurrent Debugging Guide](Luma_Concurrent_Debugging_Guide.md) and the User Manual.
- **Build something graphical.** The [Solaris Guide](Luma_Solaris_Guide.md) teaches a beginner-friendly way to build desktop apps with buttons, text, and charts, atop the lower-level [GraphicalUi](Luma_GraphicalUi_Guide.md) engine.
- **Learn the tools.** Explore the [REPL Guide](Luma_REPL_Guide.md) for interactive experimentation, and set up the editor extensions for live error-checking as you type.

Above all: **keep writing programs.** Pick something small you'd find useful — a tip calculator, a to-do list, a dice roller — and build it. Every program teaches you something no tutorial can.

Welcome to programming. Have fun.

---

## 27 — Glossary

A quick reference for the terms introduced in this tutorial.

| Term                  | Meaning                                                                                     |
| --------------------- | ------------------------------------------------------------------------------------------- |
| **annotation**        | A marker attached to code, like `@main` or `@test`, that gives it special meaning.          |
| **argument**          | A value you pass into a function when calling it.                                            |
| **array**             | An ordered list of values of the same type.                                                 |
| **assertion**         | A statement in a test (`assert(...)`) that must be true, or the test fails.                  |
| **boolean**           | A value that is either `true` or `false`.                                                    |
| **choice type**       | A type whose value is exactly one of several named variants.                                |
| **comment**           | A note in the code (after `#`) that the computer ignores.                                    |
| **condition**         | A boolean expression that decides which branch of code runs.                                |
| **dictionary**        | A collection that maps string keys to values.                                               |
| **expression**        | A piece of code that produces a value (e.g. `2 + 3` or an `if`-expression).                  |
| **function**          | A named, reusable block of instructions that may take inputs and return a value.            |
| **immutable**         | Unable to be changed after it's set (the default for Luma variables).                        |
| **include**           | Pulls another file's declarations into this one via `include "file.luma"`.                  |
| **index**             | The position of an element in an array, counting from `0`.                                   |
| **integer**           | A whole number, used for positions, counts of steps, and range bounds.                      |
| **interpolation**     | Inserting a value into a string with `${...}`.                                               |
| **lambda**            | A small, unnamed function written inline, using `->`.                                        |
| **loop**              | Code that repeats (`for`, `while`).                                                          |
| **mutable**           | Able to be changed after it's set; opted into with the `mutable` keyword.                    |
| **namespace**         | A named group of related functions and types, used as `Namespace.member`.                   |
| **number**            | A value that may have a fractional part, used for quantities and measurements.              |
| **optional**          | A type (`optional<T>`) holding either a value (`some`) or nothing (`none`).                  |
| **parameter**         | A named input listed in a function's declaration.                                           |
| **pattern matching**  | Choosing a branch by comparing a value against patterns with `match`.                       |
| **pipe**              | The `\|>` operator, which feeds a value as the first argument of the next function.          |
| **record**            | A type that groups several named fields into one value.                                     |
| **result**            | A type (`result<T>`) representing success (`success`) or failure (`failure`).               |
| **return**            | Handing a value back from a function to its caller.                                          |
| **statement**         | An instruction that performs an action.                                                     |
| **string**            | A piece of text, written in double quotes.                                                  |
| **tuple**             | A fixed-size group of values that may have different types.                                  |
| **type**              | The kind of a value, which determines what you can do with it.                               |
| **use**               | Imports a namespace's names so you can call them without the prefix.                        |
| **variable**          | A named reference to a value.                                                                |

---

## See Also

- [User Manual](Luma_User_Manual.md) — the complete, precise reference for every language feature introduced here
- [Standard Library Reference](Luma_Standard_Library_Reference.md) — the full catalogue of built-in modules and functions
- [REPL Guide](Luma_REPL_Guide.md) — experiment with the language interactively as you learn
- [Solaris Tutorial](Luma_Solaris_Tutorial.md) — put these language basics to work building a graphical desktop app
- [Contributing](../CONTRIBUTING.md) — install and build Luma, and set up an editor
- [Coding Guidelines](Luma_Coding_Guidelines.md) — idiomatic style and conventions to grow into
- [Error Handling](Luma_Error_Handling.md) — a deeper look at `result`, `optional`, and recovery
- [Documentation Index](DIRECTORY.md) — index of all Luma documentation
