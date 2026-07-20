# Luma — Performance Guide

This guide covers performance characteristics of Luma's runtime and standard library to help you write efficient programs.

## Table of Contents

1. [Immutability and Deep Copies](#1--immutability-and-deep-copies)
2. [Collection Performance Comparison](#2--collection-performance-comparison)
    - [When to Use Each Collection](#when-to-use-each-collection)
3. [String Building](#3--string-building)
4. [Recursion Limits](#4--recursion-limits)
5. [Loop Iteration Limits](#5--loop-iteration-limits)
6. [Resource Limits](#6--resource-limits)
7. [Interpreter Optimisations](#7--interpreter-optimisations)

- [See Also](#see-also)

---

## 1 — Immutability and Deep Copies

Luma values are **immutable by default**. Operations on collections (arrays, dictionaries, sets, linked lists) return new copies rather than modifying in place. This design ensures safety — no accidental shared-state bugs — but has a cost: each mutation creates a new copy of the collection.

**What this means in practice:**

- `Array.push(arr, value)` returns a new array (O(n) copy).
- `LinkedList.prepend(list, value)` returns a new linked list (O(n) deep clone).
- `Dictionary.set(dict, key, value)` returns a new dictionary (O(n) copy).

**Recommendation:** Build collections in a single pass rather than appending one element at a time in a loop. Use `Array.map`, `Array.filter`, or `Array.reduce` to transform collections efficiently.

## 2 — Collection Performance Comparison

| Operation            | Array     | LinkedList | Set       | HashSet   | Queue     | Stack     |
| -------------------- | --------- | ---------- | --------- | --------- | --------- | --------- |
| Access by index      | O(1)      | O(n)       | O(n)      | —         | —         | —         |
| Contains             | O(n)      | O(n)       | O(n)      | O(1) avg  | O(n)      | O(n)      |
| Push / Append        | O(n) copy | O(n) copy  | O(n) copy | O(n) copy | O(n) copy | O(n) copy |
| Map / Filter         | O(n)      | O(n)       | O(n)      | O(n)      | O(n)      | O(n)      |
| Union / Intersection | —         | —          | O(n + m)  | O(n + m)  | —         | —         |
| Length               | O(1)      | O(1)       | O(1)      | O(1)      | O(1)      | O(1)      |

### When to Use Each Collection

- **Array**: Default choice. Best for ordered data with index-based access.
- **LinkedList**: Niche use cases. No performance advantage in Luma due to deep-copy semantics. Prefer Array.
- **Set**: Unordered unique elements. Good for small to medium sets.
- **HashSet**: Unordered unique elements with O(1) membership tests. Prefer over Set when `contains` is called frequently. Only supports hashable primitives (integer, number, string, boolean).
- **Queue**: FIFO semantics. Use for producer-consumer patterns.
- **Stack**: LIFO semantics. Use for depth-first traversal or undo buffers.
- **Dictionary**: Key-value storage with ordered keys. Use for structured data and lookups.

## 3 — String Building

Building strings incrementally in a loop uses repeated concatenation, which creates a new string on each iteration:

```luma
# Inefficient — O(n²) string building
mutable result = ""
Array.each(items, fun(item) do
    result = result + Converter.to_string(item) + ", "
end)
```

**Recommendation:** Use `Array.map` with `String.join` instead:

```luma
# Efficient — single pass
let parts = Array.map(items, fun(item) -> Converter.to_string(item))
let result = String.join(parts, ", ")
```

## 4 — Recursion Limits

Luma enforces a maximum call-stack depth by default to bound recursion (see the [resource-limit table](#6--resource-limits) for the value and its `LUMA_LIMIT_MAX_CALL_DEPTH` override). Deeply recursive algorithms should use iterative alternatives or raise the limit.

## 5 — Loop Iteration Limits

The `while` loop has a default maximum iteration count (see the [resource-limit table](#6--resource-limits) for the value and its `LUMA_LIMIT_MAX_WHILE_ITERATIONS` override). This prevents accidental infinite loops from freezing the interpreter.

## 6 — Resource Limits

This is the canonical reference for Luma's resource limits; the defaults below mirror `core/common/resource_limits.hpp`. All limits can be overridden via environment variables prefixed with `LUMA_LIMIT_`. See [Software Architecture](Luma_Software_Architecture.md) §4.16 for the full `ResourceLimits` design and the additional internal limits it defines.

| Resource               | Default      | Environment Variable                |
| ---------------------- | ------------ | ----------------------------------- |
| Max call depth         | 256          | `LUMA_LIMIT_MAX_CALL_DEPTH`         |
| Max array size         | 10,000,000   | `LUMA_LIMIT_MAX_ARRAY_SIZE`         |
| Max dictionary size    | 10,000,000   | `LUMA_LIMIT_MAX_DICTIONARY_SIZE`    |
| Max string size        | 256 MB       | `LUMA_LIMIT_MAX_STRING_SIZE`        |
| Max regex pattern size | 10,000 bytes | `LUMA_LIMIT_MAX_REGEX_PATTERN_SIZE` |
| Max while iterations   | 10,000,000   | `LUMA_LIMIT_MAX_WHILE_ITERATIONS`   |
| Max open sockets       | 1,000        | `LUMA_LIMIT_MAX_OPEN_SOCKETS`       |
| Max task queue size    | 100,000      | `LUMA_LIMIT_MAX_TASK_QUEUE_SIZE`    |

## 7 — Interpreter Optimisations

Several internal optimisations reduce overhead in the compiler and runtime:

- **Binary operator compilation** — The bytecode compiler uses a `constexpr` sparse array indexed by `TokenType` for O(1) opcode lookup when compiling binary operators (`+`, `-`, `*`, `/`, `==`, `<`, etc.), replacing a switch-case dispatch chain.
- **TypeInfo::to_string_cached()** — The type checker caches string representations of `TypeInfo` values to avoid repeated allocations when the same type appears in multiple diagnostic messages or type comparisons.
- **Thread pool queue limits** — The thread pool enforces a maximum queue size (configurable via `ResourceLimits::max_channel_queue_size`, default 1,000,000) to prevent unbounded memory growth from unchecked channel sends.
- **ValueHash structural hashing** — The `ValueHash` implementation uses depth-limited recursive hashing (max depth 8) to prevent pathological cost on deeply nested structures. Values beyond the depth limit are hashed by type tag only. Cross-type int/double normalisation ensures consistent hashing when values compare equal.

---

## See Also

- [User Manual](Luma_User_Manual.md) — complete language reference
- [Standard Library Reference](Luma_Standard_Library_Reference.md) — per-module operations and their costs
- [Software Architecture](Luma_Software_Architecture.md) — interpreter pipeline and internal resource limits
- [Coding Guidelines](Luma_Coding_Guidelines.md) — writing efficient, idiomatic Luma
- [GraphicalUi Guide](Luma_GraphicalUi_Guide.md) — performance considerations for GUI applications
