# Module Development Kit

Step-by-step guide for adding a new standard library module to Luma.  By the end you will have a fully working module that is lazily loaded, type-checked, and tested.

> **See also:** [Stdlib Module Registration Guide](STDLIB_MODULE_REGISTRATION_GUIDE.md) — when to use each registration pattern (`ModuleBuilder`, `ContainerModuleBuilder`, or `define_native`), the `register_<module_lower>_ns` naming convention, and the error-handling patterns.  This kit links to those references rather than repeating them.

## Contents

1. [Step-by-step walkthrough](#step-by-step-walkthrough)
2. [Minimal working module skeleton](#minimal-working-module-skeleton)
3. [Implementation checklist](#implementation-checklist)
4. [Testing](#testing)

---

## Step-by-step walkthrough

### Step 1 — Create the public header

Create `core/runtime/stdlib/<category>/<module_lower>_module.hpp`.  Standard-library sources are organised into category subdirectories — `common`, `types`, `collections`, `text`, `math`, `io`, `system`, and `concurrency` — so choose the one that matches your module's domain.  The header declares only the registration function `register_<module_lower>_ns` — see the [Module Naming Convention](STDLIB_MODULE_REGISTRATION_GUIDE.md#module-naming-convention) — while all implementation details stay in `.cpp` files.

The complete header (include guard, `Environment` forward declaration, and registration function) is shown once in the [Minimal working module skeleton](#minimal-working-module-skeleton) below.

### Step 2 — Create the implementation file

Create `core/runtime/stdlib/<category>/<module_lower>_module.cpp` (the same category subdirectory as the header).  Include the header and any stdlib infrastructure you need, then implement `register_<module_lower>_ns`.

First choose a registration pattern — `ModuleBuilder` for a standard `Module.function` namespace, `ContainerModuleBuilder` for a collection type, or `define_native` for special cases.  See [Registration Patterns](STDLIB_MODULE_REGISTRATION_GUIDE.md#registration-patterns) for how to choose; the skeleton below uses `ModuleBuilder`, the most common choice.

See [Minimal working module skeleton](#minimal-working-module-skeleton) below.

### Step 3 — Add to the CMake build

Open `core/runtime/CMakeLists.txt`.  Standard-library sources are compiled as a
small set of grouped `OBJECT` libraries declared with `luma_add_stdlib_library`
(`collections`, `io`, `data`, `math`, `concurrency`, `utility`).  Add the new
`.cpp` — with its category path — to the `SOURCES` list of the group that best
fits the module:

```cmake
luma_add_stdlib_library(collections
    SOURCES
        ...
        stdlib/collections/widgets_module.cpp
        ...
)
```

The build library is grouped for faster incremental builds; the path prefix is
the category subdirectory the file physically lives in (Steps 1–2).

### Step 4 — Register in `stdlib_registry.hpp`

Open `core/runtime/stdlib/common/stdlib_registry.hpp` and add an entry to `kModules` in alphabetical order within the appropriate group:

```cpp
// ── Always-available modules ──
{"Widgets", register_widgets_ns, nullptr, false, false},
```

Groups (in order):

- Always-available — pure computation, no OS access.
- Sandbox-aware — behaviour adapts based on the `sandbox` flag; use `sandbox_register_fn`.
- OS-only — file system, network, process; set `os_only = true`.

Include the new header at the top of `stdlib_registry.hpp` alongside the others.

### Step 5 — Add type signatures (optional but recommended)

If the module is exposed to the type checker, add its function signatures to `shared/stdlib/stdlib_catalog.cpp` so that the type checker and language server know about it.  Follow the pattern of an existing module in that file.

### Step 6 — Write feature tests

Create `tests/features/stdlib/<module_lower>_functions.luma`.  See [Testing](#testing) for the file structure.

---

## Minimal working module skeleton

The skeleton below is a complete, buildable module.  Replace `Widgets` / `widgets` with your module name.

**`widgets_module.hpp`**

```cpp
#ifndef LUMA_STDLIB_WIDGETS_MODULE_HPP
#define LUMA_STDLIB_WIDGETS_MODULE_HPP

#include <memory>

namespace luma {

class Environment;
using EnvPtr = std::shared_ptr<Environment>;

void register_widgets_ns(const EnvPtr& env);

} // namespace luma

#endif // LUMA_STDLIB_WIDGETS_MODULE_HPP
```

**`widgets_module.cpp`**

```cpp
#include "runtime/stdlib/<category>/widgets_module.hpp"

#include <cstdint>

#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

void register_widgets_ns(const EnvPtr& env) {
    ModuleBuilder{"Widgets", env}
        // Widgets.count(widgets_array) → integer
        .func("count", 1)
            .extract_body(expect_array,
                [](const auto& arr, const Args&, SourceLocation) -> Value {
                    return Value{static_cast<std::int64_t>(arr->elements->size())};
                })
        // Widgets.make(label) → string (stub: returns the label)
        .func("make", 1)
            .raw_unary([](const Value& v, SourceLocation loc) -> Value {
                return Value{expect_string(v, "Widgets.make", loc)};
            });
}

} // namespace luma
```

**Key points in the skeleton:**

- `ModuleBuilder{"Widgets", env}` auto-qualifies names and validates arity — see [ModuleBuilder — standard modules](STDLIB_MODULE_REGISTRATION_GUIDE.md#modulebuilder--standard-modules) for exactly what the builder handles.
- `extract_body(expect_array, ...)` validates the type of `args[0]` before the lambda runs.  `self` (the first lambda parameter) is the extracted, const shared pointer — never mutate it directly; clone it first (see COW contract in `function_builder.hpp`).
- `raw_unary` is shorthand when you only need the single argument as a `Value` without pre-extraction.
- Return `NullValue{}` for void-like operations; `Value{...}` for typed results.

---

## Implementation checklist

Work through this checklist before marking a module as ready for review.

### Argument validation

- [ ] Every function calls `expect_args` (exact arity) or `expect_min_args` (variadic) — or uses `extract_body` / `raw_unary` / `raw_binary` which do this automatically.
- [ ] Every argument of a constrained type is validated with an `expect_*` helper (`expect_string`, `expect_integer`, `expect_array`, `expect_boolean`, `expect_numeric`, …).
- [ ] Enum/mode string arguments throw `RuntimeError` with a clear message listing the accepted values.

### Error messages

The registration guide's [Error Handling Patterns](STDLIB_MODULE_REGISTRATION_GUIDE.md#error-handling-patterns) are authoritative for the `"Module.function: reason"` format and the `RuntimeError` / `result<T>` / `optional<T>` choice.  Confirm each function:

- [ ] Reports failures with the mechanism from the guide that matches the failure kind (programming error, expected failure, or absent value).
- [ ] Uses helpers from `error_messages.hpp` for common cases (`ErrorMessages::index_out_of_bounds`, `check_bounds`, `check_not_empty`, …).

### Edge cases

- [ ] **Empty input**: containers (arrays, queues, strings) — what happens on length 0?  Return a `result<T>` failure or an empty result, never a crash.
- [ ] **Null / optional values**: if the function can receive `optional<T>`, validate with `expect_optional` or document that `null` is rejected.
- [ ] **Zero and negative numbers**: integer indices, counts, and sizes must be range-checked.  Use `check_bounds` or `validate_container_size`.
- [ ] **Maximum sizes**: containers must be checked against `ResourceLimits::max_array_size` (or the type-appropriate limit) before inserting elements.
- [ ] **NaN and Infinity**: floating-point results must be checked with `stdlib::is_valid_numeric` for functions that return `result<number>`.

### COW (copy-on-write) correctness

- [ ] The `self` / `src` parameter in every `extract_body` lambda is treated as read-only.
- [ ] Any mutation creates a copy first: `clone_array(src)`, `clone_container<T>(src)`, or `std::make_shared<T>(*src)`.

### Module registration

Follow the [Module Naming Convention](STDLIB_MODULE_REGISTRATION_GUIDE.md#module-naming-convention) for the function name, then confirm:

- [ ] `register_<module_lower>_ns` declared in `<category>/<module_lower>_module.hpp`, defined in `<category>/<module_lower>_module.cpp`.
- [ ] Entry added to `kModules` in `common/stdlib_registry.hpp` (correct group, alphabetical order).
- [ ] Header included in `common/stdlib_registry.hpp`.
- [ ] Source file (with its `stdlib/<category>/` path) added to `core/runtime/CMakeLists.txt`.

---

## Testing

For the assertion macros, `@test` conventions, and the wider test framework, see [testing.instructions.md](../../../instructions/testing.instructions.md).

### Where to add tests

Feature tests live in `tests/features/stdlib/`.  Create one file per module:

```text
tests/features/stdlib/<module_lower>_functions.luma
```

### Test file structure

```luma
# ═══════════════════════════════════════════════════════════
# Luma v1.0 — Feature Tests — Widgets Functions
#
# Feature Tests are based on Luma_Initial_Concept.md and
# Luma_Standard_Library_Reference.md.
# ═══════════════════════════════════════════════════════════

@test
function void test_widgets_count_empty() {
    assert(Widgets.count([]) == 0)
}

@test
function void test_widgets_count_nonempty() {
    assert(Widgets.count(["a", "b", "c"]) == 3)
}

@test
function void test_widgets_make_returns_label() {
    string w = Widgets.make("button")
    assert(w == "button")
}
```

Rules:

- One `@test` function per logical scenario — keep them focused.
- Name tests `test_<module_lower>_<what_is_tested>`.
- Cover the happy path, empty/zero inputs, and at least one error path (use `Result.is_failure` to assert failure results).
- Do not import any module other than the one under test unless the test genuinely requires it.

### Running the tests

```bash
cd build && ctest --output-on-failure -R stdlib
```

To run a single test file directly:

```bash
build/Release/luma tests/features/stdlib/widgets_functions.luma
```
