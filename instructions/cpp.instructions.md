---
description: "Use when writing, reviewing, or modifying C++ source code (.cpp, .hpp, .h). Covers naming, style, const-correctness, error handling, resource management, and modern C++ idioms for the Luma interpreter."
applyTo: "**/*.{cpp,hpp,h}"
priority: essential
---

# Working with C++

These instructions govern how you write C++ source code. Every function, class, and file you produce must follow these principles. They are aligned with the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) (Stroustrup & Sutter). When principles conflict, favour the one that yields simpler, safer, more readable code.

---

## Table of Contents

1. [Simplicity First (KISS)](#1--simplicity-first-kiss)
2. [Express Intent — Prefer Algorithms and Abstractions](#2--express-intent--prefer-algorithms-and-abstractions)
3. [Don't Repeat Yourself (DRY)](#3--dont-repeat-yourself-dry)
4. [Single Responsibility Principle](#4--single-responsibility-principle)
5. [Occam's Razor](#5--occams-razor)
6. [Meaningful Naming](#6--meaningful-naming)
7. [Dependency Management](#7--dependency-management)
8. [Consistent Style](#8--consistent-style)
9. [Whitespace as Structure](#9--whitespace-as-structure)
10. [Separation of Concerns (SoC)](#10--separation-of-concerns-soc)
11. [Encapsulation](#11--encapsulation)
12. [Single Source of Truth](#12--single-source-of-truth)
13. [Const Correctness and Immutability](#13--const-correctness-and-immutability)
14. [Fail Fast](#14--fail-fast)
15. [Explicit over Implicit](#15--explicit-over-implicit)
16. [High Cohesion, Low Coupling](#16--high-cohesion-low-coupling)
17. [Initialization and Declarations](#17--initialization-and-declarations)
18. [Resource Management and Special Member Functions](#18--resource-management-and-special-member-functions)
19. [Modern C++ Practices](#19--modern-c-practices)
20. [Security Essentials](#20--security-essentials)
21. [Modularity and File Organisation](#21--modularity-and-file-organisation)
22. [Anti-Patterns](#22--anti-patterns)
23. [Checklist](#23--checklist)

---

## 1 — Simplicity First (KISS)

Write the simplest code that solves the problem correctly.

- Prefer straightforward control flow over clever tricks.
- Avoid template metaprogramming, SFINAE gymnastics, or macro sorcery unless the task genuinely demands it.
- If a standard-library facility does what you need, use it instead of rolling your own.
- A junior developer with reasonable C++ knowledge should be able to read your code and understand what it does within minutes.

**Test:** Before committing to an approach, ask yourself — _is there a simpler way?_

_Ref: P.1 — Express ideas directly in code. P.11 — Encapsulate messy constructs, rather than spreading through the code._

---

## 2 — Express Intent — Prefer Algorithms and Abstractions

Say _what_ you want done, not _how_ to do it. Choose constructs that make the program's intent visible to both humans and tools.

- Prefer range-based `for` loops over index-based loops when you simply iterate.
- Prefer standard algorithms (`std::find`, `std::transform`, `std::any_of`, …) over hand-written loops when they express the operation more clearly.
- Prefer strong types over primitive types to encode meaning. For example, pass a `Speed` or `Milliseconds` rather than a bare `double` or `int`.

```cpp
// Intent hidden — how, not what.
int index = -1;

for (int i = 0; i < static_cast<int>(v.size()); ++i) {
    if (v[i] == target) {
        index = i;

        break;
    }
}

// Intent clear — what, not how.
auto it = std::find(v.begin(), v.end(), target);
```

_Ref: P.1 — Express ideas directly in code. P.3 — Express intent._

---

## 3 — Don't Repeat Yourself (DRY)

Every piece of knowledge or logic must live in exactly one place.

- Extract repeated code into a named function or template.
- Extract repeated literal values into named constants (`constexpr` or `const`).
- If two classes share identical behaviour, factor it into a common base or a free utility function — but only when the duplication is _genuine_, not coincidental.

Do **not** over-abstract. Two fragments that happen to look alike today but serve different purposes are not duplication — they are independent concerns that may diverge later. Premature unification violates KISS.

---

## 4 — Single Responsibility Principle

Every entity — function, class, file, module — does **one thing** and does it well.

- A function performs one logical operation. If you struggle to name it without using "and", it does too much. _(Ref: F.2)_
- A class models one coherent concept. If it accumulates unrelated state or mixed-level logic, split it.
- A source file groups closely related declarations. If it grows beyond a few hundred lines, consider whether it contains more than one responsibility.

---

## 5 — Occam's Razor

Given two designs that satisfy the requirements equally well, choose the one with fewer moving parts.

- Fewer classes, fewer indirections, fewer allocations, fewer template parameters.
- Add complexity only when a concrete requirement forces you to — never speculatively.
- Prefer composition over deep inheritance hierarchies. _(Ref: C.120 — Use class hierarchies to represent concepts with inherent hierarchical structure only.)_

---

## 6 — Meaningful Naming

Names are the primary documentation of your code. Choose them with care.

- **Variables and parameters:** Describe _what the value represents_, not its type. `connection_timeout` — good. `t` — bad. `int_val` — bad.
- **Functions:** Use a verb or verb-phrase that states the action. `parse_header()`, `compute_checksum()`, `is_valid()`.
- **Classes and structs:** Use a noun or noun-phrase that states the concept. `HttpConnection`, `SensorReading`, `TokenStream`.
- **Booleans:** Phrase as a question. `is_empty`, `has_children`, `should_retry`.
- **Constants and enumerators:** Use `PascalCase` or `snake_case` — be consistent within a project. Do not use `ALL_CAPS` for non-macro symbolic constants. _(Ref: NL.9 — Use ALL_CAPS for macro names only.)_
- **Template parameters:** Single uppercase letter only for trivially obvious cases (`T`, `U`). Otherwise, use a descriptive name: `Container`, `Allocator`, `Predicate`.
- **Namespaces:** Short, lowercase, project-scoped. `net`, `io`, `math`.

Avoid abbreviations unless they are universally understood in the domain (`tcp`, `url`, `id`). Never sacrifice clarity for brevity. Avoid Hungarian notation — the type system already encodes type information. _(Ref: NL.5 — Avoid encoding type information in names.)_

---

## 7 — Dependency Management

Luma is designed to be self-contained. Third-party runtime dependencies are permitted only as exceptions, when the required functionality genuinely cannot be implemented with the C++20 standard library and OS APIs alone. This keeps the build process simple, enhances portability, and reduces the attack surface.

### 7.1 Third-Party Dependencies — Exceptions Only

The interpreter executable (`luma`) and related tools (`luma_dap`, `luma_lsp`) should primarily depend on:

1. The C++20 Standard Library.
2. Operating system APIs (e.g., POSIX, Win32) for platform-specific functionality like file I/O or networking.

**Do not** add external libraries for tasks that can be accomplished with the standard library (e.g., Boost, Abseil, fmt).

A third-party library may be added when **all** of the following criteria are met:

1. The functionality cannot be reasonably implemented with the standard library or OS APIs.
2. The library is small, well-maintained, and has a permissive license.
3. The library is vendored in `external/` (no dynamic fetching at build time).
4. The dependency is documented in this section.

**Current exceptions:**

| Library  | Purpose                                    | Justification                                |
| -------- | ------------------------------------------ | -------------------------------------------- |
| miniz    | Deflate/inflate/gzip compression           | Compression algorithms beyond stdlib scope   |
| Mbed TLS | TLS/HTTPS and cryptographic hashing        | Cryptographic primitives beyond stdlib scope |

### 7.2 Standard Library Usage

Prefer modern C++20 features and idioms.

- **Containers**: Use `std::vector` by default. Use `std::string` for text, `std::string_view` for non-owning string slices. Use `std::unordered_map` for hash maps.
- **Smart Pointers**: Use `std::unique_ptr` for exclusive ownership and `std::shared_ptr` for shared ownership. Avoid raw `new` and `delete`.
- **Concurrency**: Use `std::jthread`, `std::mutex`, and `std::condition_variable` for threading and synchronization.
- **File System**: Use `std::filesystem` for path manipulation and I/O.
- **Optional and Variant**: Use `std::optional` for values that may be absent and `std::variant` for sum types.

---

## 8 — Consistent Style

Consistency removes cognitive friction. Adopt one style and apply it everywhere.

- **Braces:** Always use braces for `if`, `else`, `for`, `while`, and `do` — even for single-statement bodies. _(Note: The C++ Core Guidelines' "Stroustrup" layout permits brace-less single statements. This document opts for the stricter always-braces rule to prevent errors during maintenance.)_
- **Indentation:** Use 4 spaces. No tabs.
- **Naming convention:** `snake_case` for variables, functions, namespaces, and file names. `PascalCase` for types (classes, structs, enums, concepts, type aliases). `UPPER_SNAKE_CASE` for macros only (avoid macros when possible). _(Ref: NL.8, NL.9, NL.10.)_
- **Line length:** Aim for a maximum of 100 characters. Break long expressions at logical boundaries.
- **Include order:** Standard library → third-party libraries → project headers, each group separated by a blank line and sorted alphabetically within the group. _(Ref: SF.4.)_
- **File naming:** `snake_case.hpp` for headers, `snake_case.cpp` for sources.
- **Class member declaration order:** `public` → `protected` → `private`. Within each section: constructors, assignments, destructor, then other functions, then data members. _(Ref: NL.16.)_
- **Declarator layout:** Place `*` and `&` with the type: `int* p`, `const std::string& name`. _(Ref: NL.18.)_
- **`const` placement:** Use conventional west-const notation: `const int` rather than `int const`. _(Ref: NL.26.)_

---

## 9 — Whitespace as Structure

Use spaces and blank lines to reveal logical structure.

### Horizontal Spacing

- Place a space after keywords: `if (`, `for (`, `while (`, `return`.
- Place a space around binary operators: `a + b`, `x == y`, `i < n`.
- No space after unary operators: `!flag`, `++i`, `*ptr`.
- No space inside parentheses: `func(a, b)`, not `func( a, b )`.
- Align nothing by adding extra spaces. Let consistent indentation handle visual grouping.

### Vertical Spacing (Blank Lines)

Use blank lines to reveal logical structure — like paragraphs in prose.

- **One blank line** between functions and method definitions.
- **One blank line** between logical blocks _within_ a function (setup, processing, teardown; guard clauses vs. main logic).
- **One blank line** after `#include` groups and before/after namespace blocks.
- **No** multiple consecutive blank lines. A single blank line is the unit of separation.
- **No** trailing blank lines at end of file; **one** trailing newline character.

```cpp
#include <string>
#include <vector>

#include "project/config.hpp"
#include "project/logger.hpp"

namespace project {

std::vector<std::string> load_names(const std::string& path) {
    auto file = open_file(path);

    std::vector<std::string> names;

    for (const auto& line : read_lines(file)) {
        if (auto name = parse_name(line); !name.empty()) {
            names.push_back(std::move(name));
        }
    }

    return names;
}

} // namespace project
```

---

## 10 — Separation of Concerns (SoC)

Divide a system into distinct sections, each addressing a separate concern.

- **I/O vs. logic:** Functions that read input or write output should not contain business logic. Parse input into a clean data structure, pass it to a pure-logic function, then format the result for output.
- **Interface vs. implementation:** Expose the minimal necessary surface in headers. Hide implementation details in source files, anonymous namespaces, or the `detail` / `impl` namespace. _(Ref: I.25 — Prefer empty abstract classes as interfaces to class hierarchies.)_
- **Policy vs. mechanism:** Separate _what to do_ (policy) from _how to do it_ (mechanism). For example, a retry policy (max attempts, backoff) should be independent of the HTTP client that executes the request.

---

## 11 — Encapsulation

Hide internal state and expose behaviour through a controlled interface.

- Make data members `private` by default. Provide access only through member functions with clear semantics. Use `struct` with public members only when the type is a plain data aggregate with no invariant. _(Ref: C.2 — Use class if the class has an invariant; use struct if the data members can vary independently.)_
- Prefer non-member non-friend functions where possible — they reduce coupling and keep the class interface narrow. _(Ref: C.4 — Make a function a member only if it needs direct access to the representation of a class.)_
- Use the `pImpl` idiom or forward declarations to break compile-time dependencies when appropriate.
- Avoid `friend` unless the friendship is part of a cohesive design (e.g., an iterator for a container).

---

## 12 — Single Source of Truth

Every fact — a configuration value, a business rule, a format string — must be defined in exactly one place.

- Derive dependent values rather than storing them redundantly.
- Do not hardcode the same magic number or string in multiple locations. Define it once as a named constant, then reference it everywhere.
- If a truth must live in two systems (e.g., a C++ enum and a database schema), generate one from the other when feasible.

---

## 13 — Const Correctness and Immutability

Prefer immutable data to mutable data. Immutable objects are easier to reason about, enable compiler optimisations, and eliminate data races.

- Declare variables `const` by default. Remove `const` only when mutation is required. _(Ref: P.10 — Prefer immutable data to mutable data. Con.1 — By default, make objects immutable.)_
- Mark member functions `const` when they do not modify observable state. _(Ref: Con.2 — By default, make member functions const.)_
- Prefer `constexpr` for values that can be computed at compile time. _(Ref: Con.5 — Use constexpr for values that can be computed at compile time.)_
- Use `const` references (`const T&`) and `std::string_view` / `std::span<const T>` for read-only parameters rather than copying. _(Ref: F.16 — For "in" parameters, pass cheaply-copied types by value and others by reference to const.)_

```cpp
// Prefer const by default.
const auto max_retries = 5;
const auto& config = load_config();

for (const auto& entry : config.entries()) {
    process(entry);
}
```

---

## 14 — Fail Fast

Detect errors as early as possible and surface them immediately.

- **Compile time over run time:** Use `static_assert`, `concepts`, `constexpr if`, strong types, and `= delete` to catch misuse before the program runs. _(Ref: P.5 — Prefer compile-time checking to run-time checking.)_
- **Precondition checks:** Validate function inputs at the entry point. Use assertions (`assert`, or a project-level contract macro) for conditions that indicate programmer errors. _(Ref: I.6 — Prefer `Expects()` for expressing preconditions.)_
- **Exceptions are the preferred error-handling mechanism for run-time errors.** Use exceptions to signal failures that cannot be handled locally, especially constructor failures and resource-acquisition failures. _(Ref: E.1 — Develop an error-handling strategy early in a design. E.2 — Throw an exception to signal that a function can't perform its assigned task. E.6 — Use RAII to prevent leaks.)_
- Reserve `std::optional` and `std::expected` for cases where absence or failure is a **normal, expected outcome**, not an error (e.g., a lookup that may return nothing).
- **No silent failures:** Never swallow an error. Every error path must either handle the problem, propagate it to the caller (via an exception), or terminate.
- **RAII everywhere:** Acquire resources in constructors, release in destructors. Never rely on manual cleanup. This eliminates entire classes of leaks and double-free bugs. _(Ref: R.1 — Manage resources automatically using resource handles and RAII. P.8 — Don't leak any resources.)_
- Mark functions `noexcept` when they are guaranteed not to throw. Destructors, move operations, and swap should always be `noexcept`. _(Ref: E.12 — Use noexcept when exiting a function because of a throw is impossible or unacceptable. C.37 — Make destructors noexcept. C.66 — Make move operations noexcept.)_

```cpp
// Use exceptions for errors, optional for expected absence.

Config load_config(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error{"Config file not found: " + path.string()};
    }

    auto stream = std::ifstream{path};

    if (!stream.is_open()) {
        throw std::runtime_error{"Failed to open config file: " + path.string()};
    }

    return parse_config(stream);  // May also throw on malformed input.
}

// Optional for "not found" — a normal, expected outcome.
std::optional<User> find_user_by_email(std::string_view email);
```

---

## 15 — Explicit over Implicit

Make behaviour visible. Do not rely on hidden side effects, implicit conversions, or default arguments that obscure intent.

- Mark single-argument constructors `explicit` unless implicit conversion is intentional and documented. _(Ref: C.46 — By default, declare single-argument constructors explicit.)_
- Prefer named functions over operator overloads for non-obvious operations.
- Use `[[nodiscard]]` on functions whose return value must not be ignored.
- Avoid default function arguments when they hide important decisions. Prefer overloads or named parameter patterns if clarity is at stake.
- Spell out types when `auto` would make the type non-obvious at the point of use. Use `auto` freely when the type is evident from the right-hand side. _(Ref: ES.11 — Use auto to avoid redundant repetition of type names.)_

```cpp
// Implicit — what does 'true' mean?
connect(host, port, true);

// Explicit — the meaning is visible.
connect(host, port, UseTls::yes);
```

---

## 16 — High Cohesion, Low Coupling

### High Cohesion

Everything inside a module should be closely related. A class whose members and methods all serve the same tightly-scoped purpose is highly cohesive.

- If a subset of methods only touches a subset of data members, that subset probably wants to be its own class.
- If a header pulls in a dozen unrelated includes just to compile, the class it declares likely has too many responsibilities.

### Low Coupling

Modules should depend on each other as little as possible, and through the narrowest possible interface.

- Depend on abstractions (interfaces, concepts), not on concrete implementations.
- Pass data, not services. Prefer passing a `std::string_view` over a reference to the object that owns the string.
- Minimise header includes. Use forward declarations where a full definition is not needed.
- Avoid circular dependencies. If module A depends on module B and B on A, extract the shared interface into a third module.

---

## 17 — Initialization and Declarations

Prevent undefined behaviour and accidental misuse through disciplined initialization.

- **Always initialize variables.** Never use an uninitialized variable. _(Ref: ES.20 — Always initialize an object.)_
- **Declare variables close to first use** and in the smallest possible scope. _(Ref: ES.21 — Don't introduce a variable (or constant) before you need to use it. ES.5 — Keep scopes small.)_
- **Prefer `{}` initialisation.** Brace initialisation prevents narrowing conversions, works consistently in all contexts, and avoids the most-vexing-parse. _(Ref: ES.23 — Prefer the `{}`-initializer syntax.)_
- **Prefer in-class member initialisers** for default values, so that every constructor starts from a valid state.

```cpp
int x{7};                         // Good: brace init.
std::string name{"default"};      // Good: brace init.
std::vector<int> counts{1, 2, 3}; // Good: initializer list.

auto ratio = 0.0;                 // OK when type is obvious from the literal.

// In-class member initialisers.
class Timer {
public:
    explicit Timer(int interval_ms) : interval_ms_{interval_ms} {}

private:
    int interval_ms_;
    bool running_{false};          // Default via in-class initialiser.
    int elapsed_ms_{0};
};
```

---

## 18 — Resource Management and Special Member Functions

Follow the **Rule of Zero** and the **Rule of Five** to keep resource management safe and predictable.

- **Rule of Zero:** If a class does not directly manage a resource (memory, file handle, socket, …), do not declare any special member functions (destructor, copy/move constructors, copy/move assignment operators). Let compiler-generated defaults and RAII members handle everything. _(Ref: C.20 — If you can avoid defining default operations, do.)_
- **Rule of Five:** If a class _does_ manage a resource, define or `= delete` all five special member functions: destructor, copy constructor, copy assignment operator, move constructor, move assignment operator. _(Ref: C.21 — If you define or =delete any copy, move, or destructor function, define or =delete them all.)_
- **Base-class destructors:** A base class destructor should be either `public` and `virtual`, or `protected` and non-`virtual`. _(Ref: C.35.)_
- **Smart pointers:** `std::unique_ptr` for exclusive ownership, `std::shared_ptr` only when shared ownership is genuinely required. Raw owning pointers are banned. _(Ref: R.20 — Use unique_ptr to represent unique ownership. R.21 — Prefer unique_ptr over shared_ptr unless you need to share ownership.)_

```cpp
// Rule of Zero — no special members needed.
class UserProfile {
public:
    explicit UserProfile(std::string name) : name_{std::move(name)} {}

private:
    std::string name_;
    std::vector<std::string> permissions_;
};

// Rule of Five — class directly manages a resource.
class SocketConnection {
public:
    explicit SocketConnection(int fd) noexcept : fd_{fd} {}
    ~SocketConnection() { if (fd_ >= 0) ::close(fd_); }

    SocketConnection(const SocketConnection&) = delete;
    SocketConnection& operator=(const SocketConnection&) = delete;

    SocketConnection(SocketConnection&& other) noexcept : fd_{other.fd_} { other.fd_ = -1; }
    SocketConnection& operator=(SocketConnection&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);

            fd_ = other.fd_;
            other.fd_ = -1;
        }

        return *this;
    }

private:
    int fd_{-1};
};
```

---

## 19 — Modern C++ Practices

Use the facilities of modern C++ (C++20) to write safer, clearer code.

- **`std::optional`** for values that may or may not be present.
- **`std::variant`** for type-safe unions. _(Ref: P.4 — use `variant` instead of `union`.)_
- **`std::string_view`** for non-owning read access to strings.
- **`std::span`** for non-owning access to contiguous ranges. _(Ref: P.4, P.7 — use `span` instead of `(pointer, count)` interfaces.)_
- **Structured bindings** for unpacking pairs, tuples, and simple structs.
- **`constexpr` and `consteval`** to push computation to compile time.
- **Range-based `for` loops** over iterator-pair loops.
- **`enum class`** over unscoped `enum`. _(Ref: Enum.1 — Prefer enumerations over macros. Enum.3 — Prefer enum class over "plain" enum.)_
- **`nullptr`** — never `NULL` or `0` for pointers. _(Ref: ES.47.)_
- **`using` aliases** over `typedef`.
- **Concepts** (C++20) to constrain templates and produce clear error messages. _(Ref: T.10 — Specify concepts for all template arguments.)_
- **`noexcept`** on destructors, move operations, and swap. _(Ref: C.37, C.66, C.85.)_

---

## 20 — Security Essentials

Write defensively. Assume all external input is hostile.

- Validate and sanitise all input from files, networks, users, and environment variables before use.
- Bounds-check array and buffer accesses. Prefer `std::vector`, `std::array`, and `std::span` with `.at()` or range-checked access over raw C arrays and pointer arithmetic. _(Ref: P.4, P.6, P.7.)_
- Avoid `reinterpret_cast` and C-style casts. Use `static_cast`, `dynamic_cast`, or `std::bit_cast` (C++20). _(Ref: ES.49 — If you must use a cast, use a named cast.)_
- Never store passwords or secrets in plain text. Zero sensitive buffers after use.
- Avoid `system()`, `popen()`, and string-composed shell commands. Use direct API calls to prevent command injection.
- Prefer established, audited libraries for cryptography, serialisation, and network protocols. Do not invent your own.
- **Resource limits:** define upper bounds for unbounded resources (queues, collections, open handles) as `static constexpr` values in `resource_limits.hpp`. Enforce limits at the point of allocation and return clear error messages.
- **Sandbox gating:** standard library functions that access the filesystem or network must be gated behind the `sandbox` flag. Use `if (!sandbox)` guards around `define_native` calls for individual functions, or skip entire module registration for fully OS-dependent modules.
- **Injection prevention:** reject CRLF characters in HTTP headers, path traversal sequences in file paths, and shell metacharacters in process commands at the module boundary.

---

## 21 — Modularity and File Organisation

- **One class per header/source pair** as a default. Small, tightly related helpers may share a file.
- **Split large source files** when a single `.cpp` exceeds approximately 1 000 lines and contains multiple distinct responsibilities. Each resulting file should contain a coherent group of related functions. Share types through an internal header in a `detail` namespace.
- **Header guards:** Use `#ifndef` / `#define` / `#endif` include guards. _(Ref: SF.8 — Use #include guards for all header files.)_ `#pragma once` is acceptable as a supplementary guard on compilers that support it, but do not rely on it as the sole mechanism in portable code.
- **Forward-declare** in headers wherever possible to reduce transitive includes.
- **Namespace structure mirrors directory structure.** `project/net/socket.hpp` → `namespace project::net`.
- Keep `main()` thin. Parse arguments, wire up dependencies, delegate to application logic, and return.

---

## 22 — Anti-Patterns

Concrete C++ practices to avoid. Each restates a principle from the sections above in the negative — the form that most often slips into code under time pressure.

- **Owning raw `new` / `delete`.** Manage lifetime with values, containers, or smart pointers (`std::make_unique`, `std::make_shared`); a raw owning pointer leaks on any early return or thrown exception. _(Ref: R.11 — Avoid calling new and delete explicitly.)_
- **C-style or function-style casts.** Use a named cast (`static_cast`, `const_cast`, `reinterpret_cast`) so the intent and the risk stay explicit and greppable. _(Ref: ES.49 — If you must use a cast, use a named cast.)_
- **Casting away `const` to mutate.** Redesign the interface instead; mutating an originally-`const` object through `const_cast` is undefined behaviour. _(Ref: ES.50 — Don't cast away const.)_
- **`using namespace` at file or header scope.** Qualify names, or use a narrow function-local `using` declaration; a header-scope directive leaks into every translation unit that includes it. _(Ref: SF.7 — Don't write using namespace at global scope in a header file.)_
- **Output parameters for results.** Return by value (the compiler elides or moves the result), or return a small struct or `std::tuple`; reserve non-`const` reference parameters for genuine in/out mutation. _(Ref: F.20 — For "out" output values, prefer return values to output parameters.)_
- **`#define` for constants or "functions".** Prefer `constexpr`, `enum class`, and `inline` functions — macros ignore scope and types and confuse the debugger. _(Ref: ES.31 — Don't use macros for constants or "functions".)_
- **Passing large objects by value.** Take `const T&`, or a `std::string_view` / `std::span` for a non-owning view, when you do not need your own copy. _(Ref: F.16 — For "in" parameters, pass cheaply-copied types by value and others by reference to const.)_
- **Hand-written index loops where an algorithm fits.** Reach for `<algorithm>` or `<ranges>` and range-based `for`; say _what_, not _how_. _(Express Intent.)_
- **Swallowing exceptions with an empty `catch` or `catch (...)`.** Catch by `const&`, then handle or rethrow; a silent catch hides the very failure you need to surface. _(Fail Fast.)_

---

## 23 — Checklist

Before delivering any C++ code, verify:

1. Is this the simplest correct solution? _(KISS)_
2. Does the code express intent through algorithms, strong types, and clear structure? _(Express Intent)_
3. Is every piece of logic and every fact defined in exactly one place? _(DRY, Single Source of Truth)_
4. Does every entity have a single, clear responsibility? _(SRP, SoC)_
5. Are names descriptive, consistent, and free of type encoding? _(Meaningful Naming, Consistent Style)_
6. Does the code explain itself, with comments reserved for _why_? _(Self-Documenting Code)_
7. Is whitespace used deliberately to reveal structure? _(Whitespace as Structure)_
8. Is internal state hidden behind a controlled interface? _(Encapsulation)_
9. Are variables and parameters `const` by default? _(Const Correctness)_
10. Are errors detected and surfaced as early as possible, using exceptions for errors and RAII for resource safety? _(Fail Fast)_
11. Is behaviour visible — no hidden conversions, no silent defaults? _(Explicit over Implicit)_
12. Are modules focused internally and loosely connected externally? _(High Cohesion, Low Coupling)_
13. Are variables always initialised, preferably with `{}`? _(Initialization)_
14. Does the class follow the Rule of Zero, or the Rule of Five if it manages resources? _(Resource Management)_
15. Does the code use modern C++ facilities for safety and clarity? _(Modern C++)_
16. Is all external input validated and all resource management automatic? _(Security, RAII)_
