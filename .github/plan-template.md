# Implementation Plan: [Feature Title]

| Field              | Value          |
| ------------------ | -------------- |
| Status             | Draft          |
| Date created       | YYYY-MM-DD     |
| Last updated       | YYYY-MM-DD     |
| Related issue / PR | #000 (or link) |

> **Status:** one of `Draft` → `Approved` → `In Progress` → `Done`. Bump _Last updated_ whenever the plan changes.

## Summary

Brief description of the requirements and goals — what this plan delivers, in two or three sentences.

## Motivation

Why this change is needed: the problem it solves and who benefits.

## Scope

- **In scope:** what this plan will deliver.
- **Out of scope (non-goals):** what it deliberately will not address, to keep the work bounded.

## Architecture and Design

High-level design. Which pipeline phases are affected — Lexer → Parser → Include Resolver → Type Checker → Linter → Compiler → VM? What are the key data structures, opcodes, or interfaces involved? Note any design alternatives considered and why the chosen approach won.

## Affected Files

List every file to create or modify, grouped by area. Delete the categories that do not apply.

- **Lexer / Parser / AST** (`core/analysis/lexer/`, `core/analysis/parser/`, `core/analysis/ast/`): ...
- **Type Checker / Linter / Resolver** (`core/analysis/types/`, `core/analysis/linter/`, `core/analysis/resolver/`): ...
- **Compiler / VM** (`core/runtime/compiler/`, `core/runtime/vm/`): ...
- **Runtime / Stdlib** (`core/runtime/interpreter/`, `core/runtime/stdlib/`, `core/runtime/include/`, `core/runtime/concurrency/`): ...
- **Shared / Tooling** (`core/common/`, `shared/`, `language-server/`, `debugger/`, `extensions/`): ...
- **Tests** (`tests/`): ...
- **Documentation** (`documents/`, `DIRECTORY.md`): ...

## Tasks

Break the implementation into ordered, checkable steps that follow the pipeline. Give each task a clear acceptance criterion so completion is unambiguous.

- [ ] Task 1 — what it delivers and how to know it is done.
- [ ] Task 2 — ...
- [ ] Task 3 — ...

## Testing Strategy

Which tests to add or update:

- C++ unit tests (`tests/analysis/` or `tests/runtime/`).
- C++ integration tests (`tests/integration/`) for changes that span multiple pipeline phases.
- Luma feature tests (`tests/features/language/` or `tests/features/stdlib/`).
- Edge cases and error paths to cover.

## Risks and Regressions

Existing behaviour that could break and trade-offs the design accepts, each with a mitigation or the test that guards it.

- **Risk:** ... — mitigation: ...
- **Trade-off:** ...

## Open Questions

1. ...
2. ...
3. ...

## Definition of Done

- [ ] Builds cleanly with no new warnings (`cmake --build --preset default`).
- [ ] C++ unit and integration tests pass (`ctest --preset default`).
- [ ] Luma feature tests pass (`python scripts/run_luma_tests.py`).
- [ ] Documentation updated (User Manual plus any other affected guides under `documents/`).
- [ ] Open questions resolved and the plan reflects the final implementation.
