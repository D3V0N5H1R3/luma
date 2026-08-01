# Luma Documentation

Index of the design, reference, and guide documents for the Luma programming language. New to programming? Start with the [Tutorial](Luma_Tutorial.md). Looking for a complete reference? See the [User Manual](Luma_User_Manual.md). Want to contribute? Start with the [Contributing guide](../CONTRIBUTING.md). Need a specific section fast? The [Document Index](INDEX.md) lists every document's sections with line ranges for targeted reads.

## Table of Contents

1. [Language Reference](#1--language-reference)
2. [Guides](#2--guides)
3. [Tooling](#3--tooling)
4. [Design and Architecture](#4--design-and-architecture)
5. [Historical](#5--historical)
6. [Related](#6--related)
7. [Document Naming and Cross-References](#7--document-naming-and-cross-references)

---

## 1 — Language Reference

| Document                                                         | Description                                                                |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------- |
| [User Manual](Luma_User_Manual.md)                               | Complete language reference — syntax, types, operators, and semantics.     |
| [Standard Library Reference](Luma_Standard_Library_Reference.md) | API for every built-in module, with parameter and return types.            |
| [Error Handling](Luma_Error_Handling.md)                         | Error categories, `result` / `optional`, and standard library conventions. |
| [Coding Guidelines](Luma_Coding_Guidelines.md)                   | Conventions and best practices for writing clear, idiomatic Luma code.     |

---

## 2 — Guides

| Document                                                         | Description                                                        |
| ---------------------------------------------------------------- | ------------------------------------------------------------------ |
| [Tutorial](Luma_Tutorial.md)                                     | A step-by-step introduction to Luma for absolute beginners.        |
| [Solaris Tutorial](Luma_Solaris_Tutorial.md)                     | A step-by-step introduction to GUI programming with Solaris.       |
| [REPL Guide](Luma_REPL_Guide.md)                                 | Interactive exploration of the language and standard library.      |
| [Performance Guide](Luma_Performance_Guide.md)                   | Runtime performance characteristics and optimisation advice.       |
| [Solaris Guide](Luma_Solaris_Guide.md)                           | Building GUI applications the beginner-first way (the MVU surface).|
| [GraphicalUi Guide](Luma_GraphicalUi_Guide.md)                   | The low-level webview GUI engine beneath Solaris, and its raw API. |
| [Concurrent Debugging Guide](Luma_Concurrent_Debugging_Guide.md) | Debugging tasks, channels, and concurrent execution.               |

---

## 3 — Tooling

| Document                                           | Description                                                           |
| -------------------------------------------------- | --------------------------------------------------------------------- |
| [Debugger](Luma_Debugger.md)                       | Debug Adapter Protocol (DAP) debug adapter design and implementation. |
| [Language Server](Luma_Language_Server.md)         | Language Server Protocol (LSP) server design and implementation.      |
| [Syntax Highlighting](Luma_Syntax_Highlighting.md) | Editor grammars and the Visual Studio Code and Zed extensions.        |

---

## 4 — Design and Architecture

| Document                                               | Description                                                |
| ------------------------------------------------------ | ---------------------------------------------------------- |
| [Software Architecture](Luma_Software_Architecture.md) | Interpreter pipeline, module layout, and design rationale. |
| [Solaris Architecture](Luma_Solaris_Architecture.md)   | Design concept and rationale behind the Solaris surface.   |

---

## 5 — Historical

| Document                                   | Description                                        |
| ------------------------------------------ | -------------------------------------------------- |
| [Initial Concept](Luma_Initial_Concept.md) | The original design vision, preserved for context. |

---

## 6 — Related

Companion documents at the repository root that sit alongside this set.

| Document                           | Description                                                                    |
| ---------------------------------- | ------------------------------------------------------------------------------ |
| [Project README](../README.md)     | Project overview, quick start, build commands, and feature highlights.         |
| [Contributing](../CONTRIBUTING.md) | Build setup, branch naming, commit conventions, and the pull request workflow. |
| [Security Policy](../SECURITY.md)  | Supported versions and how to report vulnerabilities privately.                |

---

## 7 — Document Naming and Cross-References

Luma documentation follows a consistent naming and titling scheme:

- **File names** use the `Luma_<Topic>.md` pattern (`PascalCase` topic words joined by underscores). This index (`README.md`) and the section-level [Document Index](INDEX.md) (`INDEX.md`) are the exceptions, named by convention.
- **Titles** are level-one headings of the form `# Luma — <Title>` using an em-dash.
- **The "Guide" suffix** is reserved for task-oriented, how-to documents aimed at users — for example, the [REPL Guide](Luma_REPL_Guide.md), [Performance Guide](Luma_Performance_Guide.md), [Solaris Guide](Luma_Solaris_Guide.md), and [Concurrent Debugging Guide](Luma_Concurrent_Debugging_Guide.md). Reference and design documents use plain descriptive titles — for example, *User Manual*, *Standard Library Reference*, *Debugger*, and *Software Architecture*.
- **Section headings** within each document are numbered `## N — <Title>` and listed in a table of contents.

### Cross-References

Every document except this index ends with a **See Also** section linking to related documents. These sections follow a shared convention:

- **Reciprocal.** When one document points to another as important related reading, the target points back, so key relationships are navigable from both directions.
- **Whole-document by default.** Link to the whole document (`Luma_<Topic>.md`); use a section anchor (`Luma_<Topic>.md#n--section-title`) only when one specific section is materially more useful than the whole document.
- **Annotated.** Each entry ends with a short em-dash note explaining why the link is relevant.
- **Ordered by relevance.** List the most relevant references first.
