# Luma — Initial Concept

> **Historical document.** This captures the original design vision for Luma and is preserved for context. It may not reflect the current implementation. For the authoritative language reference, see the [User Manual](Luma_User_Manual.md) and the [Standard Library Reference](Luma_Standard_Library_Reference.md).

## Table of Contents

1. [Objective](#1--objective)
2. [Details](#2--details)
    - [General](#general)
    - [Safety Features](#safety-features)
    - [Annotations](#annotations)
    - [Keywords](#keywords)
    - [Data Types](#data-types)
    - [Operators](#operators)
    - [Target Platforms](#target-platforms)
3. [Next](#3--next)

---

## 1 — Objective

I would like to invent, implement and share my own simple programming language.

The target audience are beginners.

It should be as easy as Python, as secure as Rust and follow the advances from C++, Python, Java, Rust, Swift and Carbon.

The interpreter (a command-line application written in modern C++) should be able to read a source code file, find the `@main` annotated function (starting point of the program) and execute the source code.

---

## 2 — Details

### General

- it should be interpreted
- it should be imperative
- it should have a C-like syntax without the need of expressions ending with a semicolon (semicolons should be optional)
- it should have one-line comments, which have to be started by a hash character
- it should have variables (by default, immutable)
- it should provide the data types `boolean`, `integer` (should be used for indices only), `number` (should be used for all numbers except indices) and `string`
- it should have assignment operators
- it should have boolean operators
- it should have arithmetics
- it should have a `print` statement
- it should have arrays (`array`) and dictionaries (`dictionary`)
- it should have records (named type)
- it should have `if` / `else if` / `else` conditionals
- it should have `for` loops
- it should have functions (incl. parameters and a return value)
- it should have a `@main` function annotation that marks the main function (starting point of the program)
- it should provide meaningful error messages

### Safety Features

- type safety via static and strong typing
- memory safety via automatic memory management

### Annotations

- `@main` (marks the main function which is the starting point of the program)

### Keywords

- `mutable` (defines a new variable or parameter as mutable)
- `true` (possible value for boolean variables)
- `false` (possible value for boolean variables)
- `if` (control flow keyword)
- `else` (control flow keyword)
- `in` (control flow keyword used in `for` loops)
- `for` (control flow keyword)
- `record` (defines a new record type)
- `return` (returns a value from a function)
- `function` (defines a new function)

### Data Types

- `boolean` (data type for booleans)
- `integer` (data type for integers)
- `number` (data type for integers and floating point numbers)
- `string` (data type for strings)
- `array` (data type for arrays)
- `dictionary` (data type for dictionaries)

### Operators

#### Assignments

- `=` (assignment operator)
- `+=` (special assignment operator for variables of the data types `integer` and `number`)
- `-=` (special assignment operator for variables of the data types `integer` and `number`)
- `*=` (special assignment operator for variables of the data types `integer` and `number`)
- `/=` (special assignment operator for variables of the data types `integer` and `number`)

#### Comparisons

- `==` (comparison operator)
- `!=` (comparison operator)
- `<` (comparison operator)
- `>` (comparison operator)
- `<=` (comparison operator)
- `>=` (comparison operator)

#### Logic

- `&&` (logical operator for variables of the data type `boolean`)
- `||` (logical operator for variables of the data type `boolean`)
- `!` (logical operator for variables of the data type `boolean`)

#### Arithmetics

- `+` (arithmetic operator for variables of the data types `integer` and `number`)
- `-` (arithmetic operator for variables of the data types `integer` and `number`)
- `*` (arithmetic operator for variables of the data types `integer` and `number`)
- `/` (arithmetic operator for variables of the data types `integer` and `number`)
- `%` (arithmetic operator for variables of the data types `integer` and `number`)
- `++` (arithmetic operator for variables of the data types `integer` and `number`)
- `--` (arithmetic operator for variables of the data types `integer` and `number`)

#### Strings

- `+` (concatenation operator for variables of the data type `string`)
- `in` (containment operator for substrings: `"ell" in "hello"` → `true`)

#### Records

- `.` (field access operator)

#### Functions

- `|>` (pipe operator)

### Target Platforms

- Windows
- Linux (Ubuntu)
- macOS

---

## 3 — Next

- introduce generics to prevent general types like `any`
- introduce optionals to prevent error-prone values like `null`
- do not introduce classes because data structures and logic / behavior should be independent of each other

---

## See Also

- [User Manual](Luma_User_Manual.md) — the language as it exists today
- [Software Architecture](Luma_Software_Architecture.md) — how these design goals are realised in the interpreter
- [Documentation Index](DIRECTORY.md) — index of all Luma documentation
