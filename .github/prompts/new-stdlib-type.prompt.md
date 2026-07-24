---
description: "Add a new record or choice type to a Luma standard library module"
agent: "agent"
argument-hint: "Module name and type description, e.g. 'Http.Response — record with status, reason, body, headers'"
---

# New Standard Library Type

Add a new record or choice type to a Luma standard library module. Follow the existing patterns:

1. Read [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) Section 11 for stdlib architecture.
2. Read [Luma_Standard_Library_Reference.md](../../documents/Luma_Standard_Library_Reference.md) for the existing standard library API.
3. Read [Luma_Coding_Guidelines.md](../../documents/Luma_Coding_Guidelines.md) for Luma coding style conventions.
4. Read [Luma_Error_Handling.md](../../documents/Luma_Error_Handling.md) for error handling conventions — especially §6 (Standard Library Conventions) and §8 (Anti-Patterns) — which apply to the constructor/accessor functions you add alongside the type.
5. Study the existing stdlib types in `core/analysis/types/stdlib_type_arities.cpp` (e.g. the `Http.Response` record and the `Log.Level` choice) to see how records and choice types are declared and registered in `storage()`.
6. Implement the type by following the section below for its kind — [For a Record Type](#for-a-record-type) or [For a Choice Type](#for-a-choice-type) — then complete the shared [Common Steps](#common-steps).

> **Adding a plain constant, not a type?** A named constant such as `Math.pi` is registered like a nullary function (runtime `.constant(...)` / `env->define(...)` plus a catalog `m.constant(...)` entry) and never touches the type-arities `storage()` below. Follow the [For a Named Constant](new-stdlib-function.prompt.md#for-a-named-constant) section of `new-stdlib-function.prompt.md` instead.

## For a Record Type

1. Add a `RecordDeclaration` in the `storage()` function in `core/analysis/types/stdlib_type_arities.cpp`.
2. Populate the fields using `RecordField{ type_annotation, field_name, default_value }`.
3. Register the declaration in `record_map` with its qualified name (e.g. `"Http.Response"`).

## For a Choice Type

1. Add a `ChoiceDeclaration` in the `storage()` function in `core/analysis/types/stdlib_type_arities.cpp`.
2. Define variants using `ChoiceVariant{ variant_name, fields }`.
3. Register the declaration in `choice_map` with its qualified name (e.g. `"Log.Level"`).
4. Verify that `core/runtime/stdlib/common/stdlib_registry.hpp` automatically instantiates and registers variants at runtime (it iterates all choice types).

## Common Steps

1. Add type checking support in `core/analysis/types/stdlib_type_signatures.cpp` only if the type requires special resolution logic beyond the automatic map lookup.
2. Add the constructor/accessor functions that build or operate on the type, following [new-stdlib-function.prompt.md](new-stdlib-function.prompt.md). In each function's catalog entry (`shared/stdlib/stdlib_catalog_<topic>.cpp`), express the return type as `R::named("Module.TypeName")` (e.g. `R::named("Http.Response")`) so the type checker and language server resolve it.
3. Add a C++ unit test in the appropriate `tests/runtime/stdlib_test_*.cpp` file.
4. Add a Luma test in the appropriate `tests/features/stdlib/` file.
5. Document the type in `Luma_Standard_Library_Reference.md` under the module's section.
6. Build and verify: `cmake --build --preset default`, then run the relevant tests (`ctest --preset default` for the C++ unit test and `build/Release/luma --test <your-feature-test>.luma` for the Luma test). Confirm everything passes before finishing.
