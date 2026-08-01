---
description: "Use when designing, reviewing, or restructuring software architecture. Covers simplicity, modularity, separation of concerns, naming, encapsulation, robustness, and security principles."
applyTo: "{core,shared,language-server,debugger}/**"
priority: essential
---

# Working with Software Architecture

Instructions for creating simple, understandable, maintainable, modern, modular, fail-safe, and secure software. These are language-agnostic design principles. For C++-specific application of each principle (with examples, references to the C++ Core Guidelines, and implementation idioms), see [cpp.instructions.md](cpp.instructions.md).

---

## Table of Contents

1. [Core Philosophy](#1--core-philosophy)
2. [Foundational Principles](#2--foundational-principles)
3. [Structural Principles](#3--structural-principles)
4. [Robustness & Safety Principles](#4--robustness--safety-principles)
5. [Summary Checklist](#5--summary-checklist)

---

## 1 — Core Philosophy

Every design decision should favour clarity over cleverness. Code is read far more often than it is written. Optimise for the reader — your future self, your teammates, or anyone who inherits the codebase.

---

## 2 — Foundational Principles

These principles apply to all code regardless of language. Each is covered in detail with C++-specific guidance in [cpp.instructions.md](cpp.instructions.md).

| Principle                 | One-Line Summary                                                                 |
| ------------------------- | -------------------------------------------------------------------------------- |
| **KISS**                  | Choose the simplest solution that solves the problem correctly.                  |
| **Express Intent**        | Say _what_ you want done, not _how_ — prefer algorithms over hand-written loops. |
| **DRY**                   | Every piece of knowledge lives in exactly one place.                             |
| **Single Responsibility** | Every entity does one thing and does it well.                                    |
| **Occam's Razor**         | Given two equivalent designs, prefer the one with fewer moving parts.            |
| **Meaningful Naming**     | Names are the primary documentation — reveal purpose, not type.                  |
| **Self-Documenting Code** | Code explains _what_; comments explain _why_.                                    |
| **Consistent Style**      | A codebase should look like it was written by one person.                        |

---

## 3 — Structural Principles

### Separation of Concerns (SoC)

Divide the system into distinct sections, each addressing a separate concern. A concern is a cohesive area of functionality or a distinct aspect of behaviour.

- Separate business logic from I/O (database, network, filesystem).
- Separate data transformation from data presentation.
- Separate configuration from code.
- Separate policy (what to do) from mechanism (how to do it).
- Use clear boundaries: modules, packages, layers, or services, depending on the scale of the system.

### Encapsulation

Hide internal details behind a well-defined interface. Expose only what consumers need. Everything else is private.

- Default to the narrowest visibility: private fields, unexported functions, internal modules.
- Communicate through explicit interfaces and contracts, not shared internal state.
- Never let implementation details leak into public APIs.
- When a module's internals change, its consumers should not need to change.

### Single Source of Truth

Every piece of state or configuration should live in exactly one place. All other parts of the system should reference that single location rather than maintaining their own copies.

- Store each datum in one canonical location.
- Derive computed values rather than storing them redundantly.
- Centralise environment-specific configuration.
- If you find yourself synchronising two stores of the same information, redesign so only one store exists.

### High Cohesion, Low Coupling

Cohesion measures how strongly the elements inside a module belong together. Coupling measures how much modules depend on each other. Aim for high cohesion within modules and low coupling between them.

- Group functions and data that change together and serve the same purpose.
- Minimise the number of things a module needs to know about the outside world.
- Communicate between modules through narrow, well-defined interfaces.
- Prefer passing data over sharing mutable state.
- Depend on abstractions (interfaces, contracts) rather than concrete implementations when the dependency is likely to change.

---

## 4 — Robustness & Safety Principles

### Fail Fast

When something goes wrong, detect it and surface the error immediately. Do not silently swallow failures, return misleading defaults, or let invalid state propagate through the system.

- Validate inputs at the boundary — as soon as data enters a function, module, or system.
- Throw or return clear, specific errors the moment an invariant is violated.
- Prefer strict parsing: reject ambiguous input rather than guessing the intent.
- Crash early in development; use structured error handling and graceful degradation in production, but never hide the root cause.

### Explicit Over Implicit

Make behaviour visible and predictable. Do not rely on hidden side effects, implicit type conversions, framework magic, or convention that is not documented.

- Pass dependencies explicitly (function parameters, constructor injection) instead of relying on global or ambient state.
- Make configuration and feature flags visible, not buried in environment-variable defaults.
- Prefer explicit error returns or exceptions over silent fallbacks.
- State preconditions and postconditions clearly through types, assertions, or documentation.
- If behaviour depends on order, make that order obvious in the code structure.

### Secure by Default

Treat security as a baseline property of the design, not an afterthought.

- Validate and sanitise all external input — user data, API payloads, file contents, environment variables.
- Apply the principle of least privilege: grant only the permissions that are necessary.
- Never store secrets in source code or logs.
- Use parameterised queries; never interpolate user input into commands or queries.
- Default to denying access; require explicit grants.
- Keep dependencies up to date and audit them for known vulnerabilities.
- Enforce resource limits on all unbounded resources (queues, pools, collections); define those limits centrally rather than scattering magic numbers.
- Gate access to OS-level capabilities (file system, network, processes) behind an explicit opt-in, and keep those checks at the boundary of the privileged operation.
- Reject injection vectors (path traversal, CRLF, shell metacharacters) at module boundaries, and surface validation failures through the language's standard error-handling mechanism rather than throwing across layers.

> For the concrete mechanisms this project uses to implement these rules — the central `resource_limits.hpp`, the `--box` sandbox flag, and `result<T>` error propagation — see [cpp.instructions.md](cpp.instructions.md) and [luma.instructions.md](luma.instructions.md).

---

## 5 — Summary Checklist

Before committing any design or code, ask:

1. Is this the simplest approach that meets the requirements? _(KISS)_
2. Is every piece of knowledge represented exactly once? _(DRY, Single Source of Truth)_
3. Does every unit have exactly one responsibility? _(Single Responsibility)_
4. Can I remove any part without losing required functionality? _(Occam's Razor)_
5. Do the names make the purpose obvious? _(Meaningful Naming)_
6. Can the code be understood without comments? _(Self-Documenting Code)_
7. Is the style consistent throughout? _(Consistent Style)_
8. Are concerns cleanly separated? _(SoC)_
9. Are internals hidden behind clear interfaces? _(Encapsulation)_
10. Does the system fail loudly and early on bad input? _(Fail Fast)_
11. Is all behaviour visible and predictable? _(Explicit Over Implicit)_
12. Are modules internally cohesive and externally independent? _(High Cohesion, Low Coupling)_
13. Is the design secure by default? _(Secure by Default)_

If any answer is "no," revise before proceeding.
