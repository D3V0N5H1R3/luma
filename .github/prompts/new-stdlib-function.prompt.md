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
