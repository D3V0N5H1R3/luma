---
description: "Add an entirely new standard library module to Luma"
agent: "agent"
argument-hint: "Module name and purpose, e.g. 'Http — make HTTP requests'"
---

# New Standard Library Module

Add a new standard library module to Luma. Follow the existing module patterns.

This prompt covers creating the module skeleton and wiring it into the runtime, catalog, type checker, build, and docs. For the per-item work inside the module, follow:

- [new-stdlib-function.prompt.md](new-stdlib-function.prompt.md) for each function.
- [new-stdlib-type.prompt.md](new-stdlib-type.prompt.md) for each record or choice type.

## Steps

1. Read [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) Section 11 for stdlib architecture.
2. Read [Luma_Standard_Library_Reference.md](../../documents/Luma_Standard_Library_Reference.md) for existing standard library modules.
3. Read [Luma_Coding_Guidelines.md](../../documents/Luma_Coding_Guidelines.md) for Luma coding style conventions.
4. Read [Luma_Error_Handling.md](../../documents/Luma_Error_Handling.md) for error handling conventions — especially §6 (Standard Library Conventions) and §8 (Anti-Patterns).
5. Implement the module across the runtime, catalog, type checker, and build layers, following the existing patterns:
    - **Study the existing pattern.** Read [STDLIB_MODULE_REGISTRATION_GUIDE.md](../../core/runtime/stdlib/STDLIB_MODULE_REGISTRATION_GUIDE.md) and an existing module in `core/runtime/stdlib/` (e.g. `string_module.cpp`) to learn the registration patterns and file-naming conventions.
    - **Create the module (runtime).** Add `core/runtime/stdlib/<category>/<module_lower>_module.hpp` and `<module_lower>_module.cpp` (in the category subdirectory that matches the module's domain — `common`, `types`, `collections`, `text`, `math`, `io`, `system`, or `concurrency`) with one top-level `register_<module_lower>_ns(const EnvPtr& env)` (sandbox-aware modules take an extra `bool sandbox`). Register functions via the fluent DSL — `ModuleBuilder` for standard modules, `ContainerModuleBuilder` for collection types, or `define_native` for special cases (global names, blocking lifecycle, conditional compilation) — using `.func("name", arity)` then `.extract_body(...)` / `.raw_body(...)` (name and arity only; types live in the catalog). Validate arguments, emit clear error messages, use the pipe-first calling convention (first argument is the receiver), and split large modules across `_module.cpp` + `_search.cpp` / `_transform.cpp` / `_parser.cpp` / `_serializer.cpp` with a `_internal.hpp` for shared helpers.
    - **Register the module at runtime.** Include the new `<module_lower>_module.hpp` in `core/runtime/stdlib/common/stdlib_registry.hpp` and add an entry to its `kModules` table, alphabetically within its group, with the correct `os_only` / `sandbox_aware` flags. The lazy registry and stdlib namespace list derive from this table automatically.
    - **Add catalog metadata (single source of truth).** Create `shared/stdlib/stdlib_catalog_<topic>.cpp` (or extend an existing topic file) with a `register_<module_lower>_functions(specs, m, p)` function that adds each function via `m.fn(name, arity, params, return_type, {param_types})`. Declare it in `shared/stdlib/stdlib_catalog_internal.hpp` and add it to the `k_registrations` table in `shared/stdlib/stdlib_catalog.cpp` with the module name and `Capability`. The catalog drives arity, return types, completions, hover, and signature help automatically.
    - **Add type refinement only if generic.** Most modules need nothing further — the type checker derives signatures from the catalog. Only add manual logic to `refine_return_type()` in `core/analysis/types/stdlib_type_signatures.cpp` if a return type depends on call-site argument types (e.g. `array<U>`).
    - **Wire the build.** Add the runtime source(s) to `core/runtime/CMakeLists.txt` and the catalog source to `shared/CMakeLists.txt` (sources are listed explicitly — no globbing).
6. Add C++ unit tests in a `tests/runtime/stdlib_test_<module_lower>.cpp` file and wire it into CMake.
7. Add a Luma feature test at `tests/features/stdlib/<module_lower>_functions.luma`.
8. **When warranted, add fuzz and/or benchmark coverage.** Add a fuzz target in `fuzz/` only if the module parses or decodes *untrusted input* (a new parser/decoder/codec entry point). Add `time_it` cases to a `bench_<topic>.luma` in `benchmarks/` only if the module is performance-sensitive. Skip both for pure-logic modules.
9. Document the module in `Luma_Standard_Library_Reference.md` under the standard library section. If the module is large enough to warrant its own guide (e.g. `GraphicalUi` → `Luma_GraphicalUi_Guide.md`), add one.
10. Build and verify: `cmake --build --preset default`, then `ctest --preset default` for the C++ tests and `build/Release/luma --test tests/features/stdlib/<module_lower>_functions.luma` for the Luma test. Confirm everything passes before finishing.
