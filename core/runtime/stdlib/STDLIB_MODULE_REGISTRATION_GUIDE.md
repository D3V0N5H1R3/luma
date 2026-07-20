# Stdlib Module Registration Guide

This guide explains the three registration patterns used across the Luma standard library, when to choose each one, and the conventions that keep the codebase consistent.

> **See also:** [Module Development Kit](MODULE_DEVELOPMENT_KIT.md) — the end-to-end procedure for adding a module (public header, CMake wiring, `stdlib_registry.hpp` entry, type signatures, and feature tests).  This guide owns the pattern, naming, and error-handling references that the kit links to.

## Contents

1. [Registration Patterns](#registration-patterns)
   - [ModuleBuilder — standard modules](#modulebuilder--standard-modules)
   - [ContainerModuleBuilder — collection types](#containermodulebuilder--collection-types)
   - [define\_native directly — special cases](#define_native-directly--special-cases)
2. [Module Naming Convention](#module-naming-convention)
3. [Splitting a Module across Multiple Files](#splitting-a-module-across-multiple-files)
   - [File naming conventions](#file-naming-conventions)
   - [Shared internal helpers](#shared-internal-helpers)
4. [Error Handling Patterns](#error-handling-patterns)

---

## Registration Patterns

### ModuleBuilder — standard modules

Use `ModuleBuilder` for every module that exposes a stable, qualified namespace (`Module.function`) and has no special lifetime or compilation constraints.

```cpp
#include "runtime/stdlib/common/function_builder.hpp"

void register_math_ns(const EnvPtr& env) {
    ModuleBuilder{"Math", env}
        .func("abs", 1)
            .extract_body(expect_numeric_value,
                [](const auto& x, const Args&, SourceLocation) -> Value {
                    return Value{std::abs(x)};
                })
        .constant("pi", Value{3.14159265358979323846});
}
```

`ModuleBuilder` automatically:

- Qualifies every name as `"<Module>.<func>"`.
- Validates argument count via `expect_args` before the body runs.
- Provides fluent chaining so all functions are registered in one expression.

**Numeric helpers** on `ModuleBuilder` remove further boilerplate:

| Helper | When to use |
|---|---|
| `checked_unary(name, fn)` | `sin`, `cos`, `exp` — may produce NaN/Inf |
| `checked_unary_to_int(name, fn)` | `floor`, `ceil`, `round`, `truncate` |
| `positive_unary(name, fn)` | `log_e`, `log_2`, `log_10` — positive domain only |

Each helper handles arity checking, numeric extraction, domain validation, and result wrapping automatically.

---

### ContainerModuleBuilder — collection types

Use `ContainerModuleBuilder` for modules that wrap a homogeneous container value type (`QueueValue`, `StackValue`, `SetValue`, `LinkedListValue`, `HashSetValue`). It registers the common boilerplate operations — `new`, `from_array`, `length`, `is_empty`, `to_array`, `map`, `filter`, `reduce`, `each`, `partition`, `concat` — in a single call, leaving only the container-specific operations to be written by hand.

```cpp
#include "runtime/stdlib/collections/container_module_builder.hpp"

void register_queue_ns(const EnvPtr& env) {
    ContainerModuleBuilder<QueueValue> cmb{
        "Queue", env, expect_queue, ResourceLimits::max_queue_size};
    cmb.register_all_common();   // new, from_array, length, map, filter, …

    // Container-specific operations via the embedded ModuleBuilder.
    cmb.builder()
        .func("enqueue", 2)
            .extract_body(expect_queue,
                [](const auto& src, const Args& args, SourceLocation loc) -> Value {
                    validate_container_size(src->elements.size(),
                                            ResourceLimits::max_queue_size,
                                            "Queue.enqueue", loc);
                    auto q = clone_container<QueueValue>(src);
                    q->elements.push_back(args[1]);
                    return Value{std::move(q)};
                });
}
```

`ContainerModuleBuilder` template parameters:

- `Container` — the value type; must have `std::vector<Value> elements`.
- `ReverseEach` — if `true`, `each()` iterates in reverse order (default: `false`).

Use `register_all_common()` unless you need finer control; individual methods `register_new()`, `register_from_array()`, and `register_common_ops()` are available when only a subset is needed.

---

### define\_native directly — special cases

Use `define_native` directly (bypassing both builders) only when one or more of the following constraints apply:

| Constraint | Example |
|---|---|
| **Global / unqualified names** | `print`, `assert`, `type_of` — no module prefix |
| **Blocking lifecycle** | `GraphicalUi.app` runs an OS event loop that blocks until the window closes |
| **Conditional compilation** | Module guarded by `#ifdef LUMA_HAS_WEBVIEW`; stub path registers placeholder names from a list |
| **Shared mutable state** | Thread-local webview handle captured across many lambdas |
| **Split implementation** | Module delegates to many helper files; builder's single-chain assumption doesn't apply |

```cpp
// core_builtins_module.cpp — global built-ins have no module prefix
define_native(env, "print", [](std::span<const Value> args, SourceLocation) -> Value {
    for (const auto& arg : args) { std::cout << arg.to_string(); }
    std::cout << "\n";
    return NullValue{};
});
```

When using `define_native` for a non-trivial module, add a comment at the top of the `.hpp` explaining which constraints apply (see `graphicalui_module.hpp` for the reference example).

---

## Module Naming Convention

Every module exposes exactly one top-level registration function:

```text
register_<module_lower>_ns(const EnvPtr& env)
```

Examples: `register_queue_ns`, `register_string_ns`, `register_json_ns`.

- `<module_lower>` is the module name in all lowercase with no separators.
- The suffix `_ns` distinguishes module registration functions from other `register_*` helpers.
- Sandbox-aware modules receive an additional `bool sandbox` parameter:
  `register_compression_ns(const EnvPtr& env, bool sandbox)`.

The function is declared in `<module_lower>_module.hpp` and defined in `<module_lower>_module.cpp` (or the first `.cpp` file when split). After writing the function, add it to `stdlib_registry.hpp`'s `kModules` table in alphabetical order within its group — the [Module Development Kit](MODULE_DEVELOPMENT_KIT.md) walks through that registration step and its module groups in full.

---

## Splitting a Module across Multiple Files

Split a module into multiple `.cpp` files when either:

- The implementation exceeds ~300 lines, **or**
- The module contains two or more distinct functional areas (parsing vs. serialisation, search vs. transform, etc.).

Keep the public header (`<module_lower>_module.hpp`) small: it only declares `register_<module_lower>_ns`. All split files `#include` it.

### File naming conventions

| Suffix | What goes there |
|---|---|
| `_module.cpp` | Module entry point: `register_<module_lower>_ns`, top-level wiring |
| `_search.cpp` | Search and find operations (`find`, `contains`, `index_of`, …) |
| `_transform.cpp` | Map and convert operations (`map`, `to_upper`, `replace`, …) |
| `_parser.cpp` | Deserialisation / parsing (JSON parser, XML parser, CSV reader) |
| `_serializer.cpp` | Serialisation (JSON writer, XML writer, CSV writer) |
| `_internal.hpp` | Shared internal helpers used across split files — not part of the public API |

The `_parser.cpp` / `_serializer.cpp` spellings are preferred, but the equivalent `_parsing.cpp` / `_serialization.cpp` forms are also accepted, as used by `http_module_parsing.cpp` and `graphicalui_serialization.cpp` below.

Real examples in the codebase:

```text
string_module.cpp            — registration + core ops
string_module_search.cpp     — find, contains, starts_with, …
string_module_transform.cpp  — to_upper, replace, split, …

json_module.cpp              — registration
json_module_parser.cpp       — JSON parsing (read path)
json_module_serializer.cpp   — JSON serialisation (write path)

xml_module.cpp               — registration
xml_module_parser.cpp        — XML parsing
xml_module_serializer.cpp    — XML serialisation, search, and navigation

http_module.cpp              — registration + core requests
http_module_parsing.cpp      — response parsing helpers

graphicalui_module.cpp       — registration
graphicalui_internal.hpp     — shared state, widget tree, helpers
graphicalui_widgets.cpp      — core widget management
graphicalui_widgets_basic.cpp
graphicalui_events.cpp
graphicalui_css_properties.cpp
graphicalui_css_sanitiser.cpp
graphicalui_serialization.cpp
graphicalui_commands.cpp
```

### Shared internal helpers

Place shared constants, forward declarations, and helper functions that are needed by more than one split file in `<module_lower>_internal.hpp`. Mark every symbol in it as `static` or put it in an anonymous `namespace` so it has internal linkage and cannot leak across translation units.

```cpp
// terminal_module_internal.hpp
#pragma once
namespace luma::terminal_detail {
    [[nodiscard]] std::string strip_ansi(std::string_view s);
}
```

Never include `_internal.hpp` from the public module header or from any file outside the module's own split files.

---

## Error Handling Patterns

The stdlib uses three error-reporting mechanisms. Choose the right one based on whether the error is a programming mistake or an expected runtime condition. For the language-wide rationale and anti-patterns, see [Luma_Error_Handling.md](../../../documents/Luma_Error_Handling.md).

### 1. Throw `RuntimeError` — programming errors

Use for type mismatches, wrong arity, invalid enum values, and resource-limit violations. The `expect_*` helpers in `native_function_validation.hpp` cover the most common cases:

```cpp
expect_args("Array.get", args, 2, loc);
const auto idx = expect_integer(args[1], "Array.get", loc);
const auto& arr = *expect_array(args[0], "Array.get", loc);
```

These throw `RuntimeError` with a uniform, user-readable message.

### 2. Return `result<T>` — expected failures

Use for I/O errors, parse failures, and domain errors (singular matrix, empty container). Return `make_success_value(v)` or `make_failure_value(msg)`:

```cpp
if (src->elements.empty()) {
    return make_failure_value("Queue.dequeue: queue is empty");
}
return make_success_value(src->elements.front());
```

### 3. Return `optional<T>` — absent values

Use for lookups where absence is a normal outcome (dictionary key lookup, regex first match).

### `wrap_result_operation` — wrapping operations that may throw

Use `wrap_result_operation` (from `stdlib_error_helpers.hpp`) when an operation internally returns a `result<T>` Value but may also throw `std::exception`. This avoids repeating the try-catch boilerplate:

```cpp
#include "runtime/stdlib/common/stdlib_error_helpers.hpp"

return wrap_result_operation("FileSystem", "read_text", [&]() -> Value {
    // may return make_failure_value(...) OR throw std::filesystem::filesystem_error
    if (!std::filesystem::exists(path)) {
        return make_failure_value("FileSystem.read_text: file not found");
    }
    return make_success_value(Value{read_file_contents(path)});
});
```

Exceptions are caught and converted to `make_failure_value("Module.function: <what>")`. Pass an optional fourth argument — a machine-readable `error_codes::…` value — to attach a structured code to those exception-derived failures (for example, `error_codes::parse_error` for a deserialiser); omit it to keep the prefixed message only. Use `wrap_result_operation` whenever the inner lambda might throw; use `make_failure_value` directly when the code path is fully controlled and cannot throw.

Two sibling helpers in `native_function_containers.hpp` cover the case where the inner lambda returns a **raw** value (not a `result<T>`) and you want success wrapped automatically: `safe_call(module, function, op)` prefixes any exception-derived failure with `Module.function:`, while `apply_with_error_handling(op)` does the same without a prefix. Reach for these when the operation cannot itself return a failure value, and for `wrap_result_operation` when it can.

**Choosing between wrappers:**

| Wrapper | The inner operation… |
|---|---|
| `safe_call(mod, fn, op)` | Returns a raw value; wrapper wraps it in `make_success_value` |
| `apply_with_error_handling(op)` | Same as `safe_call` but without the module/function prefix |
| `wrap_result_operation(mod, fn, op)` | Returns a `Value` itself (success or failure); only exceptions are caught |
