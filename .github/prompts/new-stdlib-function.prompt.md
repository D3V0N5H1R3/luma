---
description: "Add a new built-in function to an existing Luma standard library module"
agent: "agent"
argument-hint: "Module name and function description, e.g. 'String.reverse — reverses a string'"
---

# New Standard Library Function

Add a new built-in function to a Luma standard library module. Follow the existing patterns:

1. Read [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) Section 11 for stdlib architecture.
2. Read [Luma_Standard_Library_Reference.md](../../documents/Luma_Standard_Library_Reference.md) for the existing standard library API.
3. Read [Luma_Coding_Guidelines.md](../../documents/Luma_Coding_Guidelines.md) for Luma coding style conventions.
4. Read [Luma_Error_Handling.md](../../documents/Luma_Error_Handling.md) for error handling conventions — especially §6 (Standard Library Conventions) and §8 (Anti-Patterns).
5. Implement the function across the runtime and metadata layers, following the existing patterns:
    - **Study the existing pattern.** Find the target module's runtime registration function in `core/runtime/stdlib/` (e.g. `register_string_ns()` in `string_module.cpp`) and see how nearby functions are registered and implemented. (See also `core/runtime/stdlib/STDLIB_MODULE_REGISTRATION_GUIDE.md`.)
    - **Register and implement (runtime).** Add the function via the `ModuleBuilder` fluent DSL — `.func("name", arity)` (name and arity only), then `.extract_body(...)` / `.raw_body(...)` for the body. Validate arguments, emit clear error messages, and use the pipe-first calling convention (first argument is the receiver).
    - **Add catalog metadata.** Add the function to the module's catalog file (`shared/stdlib/stdlib_catalog_<module>.cpp`) via `m.fn(name, arity, params, return_type, {param_types})`. This is the single source of truth — the type checker derives arity and return type from it automatically, and the language server derives completions, hover, and signature help. The `params` string is the human-readable parameter list **with names and types** (e.g. `"(value: string, count: integer)"`), so name the parameters clearly.
    - **Add type refinement only if generic.** Most functions need nothing further. Only if the return type depends on the call-site argument types (e.g. `Array.map` returning `array<U>`, which the catalog's `return_type` cannot express) add manual refinement logic to `refine_return_type()` in `core/analysis/types/stdlib_type_signatures.cpp`.
6. Add a C++ unit test in the appropriate `tests/runtime/stdlib_test_*.cpp` file.
7. Add a Luma test in the appropriate `tests/features/stdlib/` file.
8. **When warranted, add fuzz and/or benchmark coverage.** Most pure-logic functions (e.g. `String.reverse`) need neither — skip both by default.
    - **Fuzz** — only if the function decodes or parses *untrusted input* (a new parser/decoder entry point, regex, compression, etc.). Extend the existing module's target in `fuzz/` (e.g. `fuzz_json.cpp`, `fuzz_xml.cpp`, `fuzz_string.cpp`) and its seed corpus — the targets are trust-boundary/codec-scoped, not per-function, so do not add a new target for one function.
    - **Benchmark** — only if the function is performance-sensitive or on a hot path. Add a `time_it` case to the relevant `bench_<topic>.luma` in `benchmarks/` (e.g. `bench_strings.luma`); do not create a new file for a single case. CI fails on a >10% regression against the cached baseline.
9. Document the function in `Luma_Standard_Library_Reference.md` under the module's section. If the module has its own dedicated guide (e.g. `GraphicalUi` → `Luma_GraphicalUi_Guide.md`), update that guide's detailed reference too.
10. Build and verify: `cmake --build --preset default`, then run the relevant tests (`ctest --preset default` for the C++ unit test and `build/Release/luma --test <your-feature-test>.luma` for the Luma test). Confirm everything passes before finishing.

## For a Named Constant

A named constant (e.g. `Math.pi`, `Math.tau`, `GraphicalUi.PRIMARY`) is a nullary, bodyless variation on the steps above — it lives in the same runtime module and catalog files as a function, **not** in the type-arities `storage()` used by [new-stdlib-type.prompt.md](new-stdlib-type.prompt.md). Follow the same workflow with these differences:

1. **Register (runtime).** Instead of `.func(...).extract_body(...)`, bind the value with `ModuleBuilder`'s `.constant("name", Value{...})` (or a direct `env->define("Module.name", Value{...}, false)` for special cases). There is no argument validation and no body.
2. **Add catalog metadata.** Use `m.constant("name", return_type)` (e.g. `m.constant("pi", R::number_type())`) in `shared/stdlib/stdlib_catalog_<module>.cpp` instead of `m.fn(...)`. This records `arity = 0` and `is_constant = true` and remains the single source of truth for the type checker and language server (completions, hover).
3. **Skip arity and type refinement.** Constants are excluded from arity validation automatically (`init_arities()` skips every `is_constant` spec), and a constant's type is fixed, so `refine_return_type()` never applies.
4. **Test, document — skip fuzz/benchmark.** Add a C++ unit test and a Luma feature test asserting the constant's value/type, and document it under the module's section in `Luma_Standard_Library_Reference.md`. (The catalog↔runtime wiring is already guarded — `test_catalog_constants_are_not_callable` in `tests/runtime/stdlib_catalog_conformance_test.cpp` fails if a catalog constant has no runtime binding — so add both entries together.) Fuzz and benchmark coverage do not apply to a constant.
