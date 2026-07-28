# Luma — Standard Library Reference

Complete reference for Luma's built-in functions and standard library modules. Each module lists its functions with parameter types, return types, and a short description.

This reference was previously part of the [User Manual](Luma_User_Manual.md). For language syntax and semantics, see the [User Manual](Luma_User_Manual.md); for error-handling conventions, see [Error Handling](Luma_Error_Handling.md).

---

## Table of Contents

1. [Core Built-Ins](#1--core-built-ins)
2. [Array](#2--array)
3. [BinaryTree](#3--binarytree)
4. [Calculus](#4--calculus)
5. [Channel](#5--channel)
6. [Compression](#6--compression)
7. [Converter](#7--converter)
8. [Csv](#8--csv)
9. [DateTime](#9--datetime)
10. [Decimal](#10--decimal)
11. [Dictionary](#11--dictionary)
12. [Encoder](#12--encoder)
13. [FileSystem](#13--filesystem)
14. [Graph](#14--graph)
15. [Solaris and GraphicalUi](#15--solaris-and-graphicalui)
16. [Hash](#16--hash)
17. [HashSet](#17--hashset)
18. [Http](#18--http)
19. [Console](#19--console)
20. [Json](#20--json)
21. [KeyValueStore](#21--keyvaluestore)
22. [LinearAlgebra](#22--linearalgebra)
23. [LinkedList](#23--linkedlist)
24. [Log](#24--log)
25. [Math](#25--math)
26. [Optional](#26--optional)
27. [Order](#27--order)
28. [Process](#28--process)
29. [Queue](#29--queue)
30. [Random](#30--random)
31. [Reference](#31--reference)
32. [RegularExpression](#32--regularexpression)
33. [Resource](#33--resource)
34. [Result](#34--result)
35. [Set](#35--set)
36. [Socket](#36--socket)
37. [Stack](#37--stack)
38. [String](#38--string)
39. [Task](#39--task)
40. [Terminal](#40--terminal)
41. [Xml](#41--xml)
42. [Color](#42--color)

- [See Also](#see-also)

---

## 1 — Core Built-Ins

These require no namespace prefix:

| Function            | Parameter Types     | Return Type | Description                                                     |
| ------------------- | ------------------- | ----------- | --------------------------------------------------------------- |
| `assert(cond)`      | `(boolean)`         | `none`      | Fail with `"assertion failed"` if false                         |
| `assert(cond, msg)` | `(boolean, string)` | `none`      | Fail with custom message                                        |
| `failure(msg)`      | `(T)`               | `result<T>` | Construct failure result; records source location automatically |
| `print(v, ...)`     | `(T, ...)`          | `none`      | Print values separated by spaces, followed by newline           |
| `success(v)`        | `(T)`               | `result<T>` | Construct success result                                        |
| `type_of(v)`        | `(T)`               | `string`    | Runtime type name                                               |

## 2 — Array

| Function                         | Parameter Types                        | Return Type                    | Description                                                                |
| -------------------------------- | -------------------------------------- | ------------------------------ | -------------------------------------------------------------------------- |
| `Array.all(arr, fn)`             | `(array<T>, function(T) -> boolean)`   | `result<boolean>`              | `true` if all elements match; fail if predicate throws                     |
| `Array.any(arr, fn)`             | `(array<T>, function(T) -> boolean)`   | `result<boolean>`              | `true` if any element matches; fail if predicate throws                    |
| `Array.append(arr, v)`           | `(array<T>, T)`                        | `array<T>`                     | New array with `v` appended (alias for `push`)                             |
| `Array.binary_search(arr, v)`    | `(array<T>, T)`                        | `result<integer>`              | Index of `v` via binary search of a sorted array; fail if not found        |
| `Array.chunk(arr, n)`            | `(array<T>, integer)`                  | `result<array<array<T>>>`      | Split into sub-arrays of size `n`; fail if `n` is not > 0                  |
| `Array.compact(arr)`             | `(array<T>)`                           | `array<T>`                     | Remove `none` elements                                                     |
| `Array.contains(arr, v)`         | `(array<T>, T)`                        | `boolean`                      | Whether `v` is present in the array                                        |
| `Array.concat(a, b)`             | `(array<T>, array<T>)`                 | `array<T>`                     | Concatenate two arrays                                                     |
| `Array.count(arr, fn)`           | `(array<T>, function(T) -> boolean)`   | `result<integer>`              | Number of matching elements; fail if predicate throws                      |
| `Array.count_by(arr, key)`       | `(array<T>, function(T) -> string)`    | `dictionary<integer>`          | Per-bucket occurrence counts keyed by `key` (like `group_by` counting)     |
| `Array.drop(arr, n)`             | `(array<T>, integer)`                  | `array<T>`                     | Drop the first `n` elements                                                |
| `Array.drop_while(arr, fn)`      | `(array<T>, function(T) -> boolean)`   | `result<array<T>>`             | Drop elements while predicate is true; fail if predicate throws            |
| `Array.each(arr, fn)`            | `(array<T>, function(T) -> none)`      | `result<none>`                 | Iterate for side effects; fail if callback throws                          |
| `Array.enumerate(arr)`           | `(array<T>)`                           | `array<(integer, T)>`          | Array of `(index, value)` tuples                                           |
| `Array.filter(arr, fn)`          | `(array<T>, function(T) -> boolean)`   | `result<array<T>>`             | Keep elements where `fn` returns `true`; fail if predicate throws          |
| `Array.find(arr, fn)`            | `(array<T>, function(T) -> boolean)`   | `result<T>`                    | First matching element; fail if not found                                  |
| `Array.find_index(arr, fn)`      | `(array<T>, function(T) -> boolean)`   | `result<integer>`              | Index of first matching element; fail if not found                         |
| `Array.find_last(arr, fn)`       | `(array<T>, function(T) -> boolean)`   | `result<T>`                    | Last matching element; fail if not found                                   |
| `Array.find_last_index(arr, fn)` | `(array<T>, function(T) -> boolean)`   | `result<integer>`              | Index of last matching element; fail if not found                          |
| `Array.first(arr)`               | `(array<T>)`                           | `result<T>`                    | First element; fail if empty                                               |
| `Array.flat_map(arr, fn)`        | `(array<T>, function(T) -> array<U>)`  | `result<array<U>>`             | Map then flatten one level; fail if callback throws                        |
| `Array.flatten(arr)`             | `(array<array<T>>)`                    | `array<T>`                     | Flatten one nesting level                                                  |
| `Array.get(arr, i)`              | `(array<T>, integer)`                  | `result<T>`                    | Safe indexed access; fail if out of bounds                                 |
| `Array.group_by(arr, fn)`        | `(array<T>, function(T) -> string)`    | `result<dictionary<array<T>>>` | Group elements by key returned by `fn`; fail if callback throws            |
| `Array.index_of(arr, v)`         | `(array<T>, T)`                        | `result<integer>`              | First index of `v`; fail if not found                                      |
| `Array.insert_at(arr, i, v)`     | `(array<T>, integer, T)`               | `result<array<T>>`             | Insert `v` at index `i`; fail if index out of bounds                       |
| `Array.is_empty(arr)`            | `(array<T>)`                           | `boolean`                      | Whether the array has no elements                                          |
| `Array.is_sorted(arr)`           | `(array<T>)`                           | `boolean`                      | Whether the array is in ascending natural order                            |
| `Array.is_sorted_by(arr, key)`   | `(array<T>, function(T) -> U)`         | `result<boolean>`              | Whether the array is ascending by the projected key; fail if key throws    |
| `Array.join(arr, sep)`           | `(array<T>, string)`                   | `string`                       | Concatenate elements as strings separated by `sep`                         |
| `Array.last(arr)`                | `(array<T>)`                           | `result<T>`                    | Last element; fail if empty                                                |
| `Array.length(arr)`              | `(array<T>)`                           | `integer`                      | Number of elements                                                         |
| `Array.map(arr, fn)`             | `(array<T>, function(T) -> U)`         | `result<array<U>>`             | Transform each element; fail if callback throws                            |
| `Array.max(arr)`                 | `(array<T>)`                           | `result<T>`                    | Maximum value; fail if empty                                               |
| `Array.max_by(arr, key)`         | `(array<T>, function(T) -> number)`    | `optional<T>`                  | Element with the greatest key; `none` if empty; first element wins ties    |
| `Array.min(arr)`                 | `(array<T>)`                           | `result<T>`                    | Minimum value; fail if empty                                               |
| `Array.min_by(arr, key)`         | `(array<T>, function(T) -> number)`    | `optional<T>`                  | Element with the smallest key; `none` if empty; first element wins ties    |
| `Array.none(arr, fn)`            | `(array<T>, function(T) -> boolean)`   | `result<boolean>`              | `true` if no element matches; fail if predicate throws                     |
| `Array.partition(arr, fn)`       | `(array<T>, function(T) -> boolean)`   | `result<(array<T>, array<T>)>` | Split into `(matches, rest)`; fail if predicate throws                     |
| `Array.pop(arr)`                 | `(array<T>)`                           | `result<(array<T>, T)>`        | Remove last element; fail if empty                                         |
| `Array.product(arr)`             | `(array<T>)`                           | `result<integer \| number>`    | Multiply numeric elements; `1` for an empty array; fail if non-numeric element found |
| `Array.push(arr, v)`             | `(array<T>, T)`                        | `array<T>`                     | New array with `v` appended                                                |
| `Array.range(start, end)`        | `(integer, integer)`                   | `result<array<integer>>`       | Generate `[start, start+1, ..., end-1]`; fail if range exceeds limit       |
| `Array.reduce(arr, init, fn)`    | `(array<T>, U, function(U, T) -> U)`   | `result<U>`                    | Fold left with accumulator; fail if `fn` is not callable                   |
| `Array.remove_at(arr, i)`        | `(array<T>, integer)`                  | `result<(array<T>, T)>`        | Remove element at index; fail if out of bounds                             |
| `Array.repeat(v, n)`             | `(T, integer)`                         | `result<array<T>>`             | `n` copies of `v`; fail if `n` is negative or exceeds limit                |
| `Array.reverse(arr)`             | `(array<T>)`                           | `array<T>`                     | Return reversed copy                                                       |
| `Array.set(arr, i, v)`           | `(array<T>, integer, T)`               | `result<array<T>>`             | New array with element at index `i` replaced by `v`; fail if out of bounds |
| `Array.slice(arr, from, to)`     | `(array<T>, integer, integer)`         | `result<array<T>>`             | Subarray `[from, to)`; fail if indices are negative or `from > to`         |
| `Array.sort(arr, fn)`            | `(array<T>, function(T, T) -> number)` | `result<array<T>>`             | Sort by comparator; fail if comparator throws                              |
| `Array.sort_by(arr, fn)`         | `(array<T>, function(T) -> U)`         | `result<array<T>>`             | Sort by key function; fail if key function throws                          |
| `Array.split_at(arr, i)`         | `(array<T>, integer)`                  | `(array<T>, array<T>)`         | Split into `(take i, drop i)`; `i` is clamped to `[0, length]`             |
| `Array.sum(arr)`                 | `(array<T>)`                           | `result<integer \| number>`    | Sum numeric elements; fail if non-numeric element found                    |
| `Array.sum_by(arr, key)`         | `(array<T>, function(T) -> number)`    | `number`                       | Total of the projected key over every element; `0` for an empty array      |
| `Array.take(arr, n)`             | `(array<T>, integer)`                  | `array<T>`                     | Take the first `n` elements                                                |
| `Array.take_while(arr, fn)`      | `(array<T>, function(T) -> boolean)`   | `result<array<T>>`             | Take elements while predicate is true; fail if predicate throws            |
| `Array.unique(arr)`              | `(array<T>)`                           | `array<T>`                     | Deduplicate elements                                                       |
| `Array.unzip(arr)`               | `(array<(T, U)>)`                      | `(array<T>, array<U>)`         | Split an array of pairs into two parallel arrays (inverse of `zip`)        |
| `Array.windows(arr, n)`          | `(array<T>, integer)`                  | `result<array<array<T>>>`      | Overlapping sliding windows of size `n`; fail if `n` is not > 0            |
| `Array.zip(a, b)`                | `(array<T>, array<U>)`                 | `array<(T, U)>`                | Pair elements into tuples; truncates to shorter array                      |
| `Array.scan(arr, init, fn)`      | `(array<T>, U, function(U, T) -> U)`   | `result<array<U>>`             | Like reduce but returns all intermediate values; fail if callback throws   |
| `Array.intersperse(arr, sep)`    | `(array<T>, T)`                        | `array<T>`                     | Insert `sep` between each pair of elements                                 |
| `Array.rotate(arr, n)`           | `(array<T>, integer)`                  | `array<T>`                     | Rotate elements left by `n` positions (negative rotates right)             |
| `Array.transpose(arr)`           | `(array<array<T>>)`                    | `result<array<array<T>>>`      | Transpose a 2D array; fail if rows have unequal length                     |

> **Sorting.** The `Array.sort` comparator must return a number: negative puts `a` before `b`, zero treats them as equal, and positive puts `b` before `a`. `Array.sort_by` accepts any function that returns a comparable string or number key — elements are sorted in ascending order by that key.

## 3 — BinaryTree

A binary search tree (BST) with O(log n) average-case insert, remove, and lookup. Supports comparable types (integer, number, string). All operations are immutable — they return a new tree.

> **Ordering invariant.** For every node, all values in the left subtree are strictly less than the node's value, and all values in the right subtree are strictly greater. Integers and numbers are compared numerically; strings are compared lexicographically. Duplicate values are not stored — inserting an existing value is a no-op.
>
> **Balance.** The tree is **not** self-balancing. Inserting values in sorted order produces a degenerate linear tree with O(n) height. For predictable performance on large inputs, shuffle the data before inserting, or prefer `HashSet` for membership-only queries.

| Function                            | Parameter Types                         | Return Type                          | Description                                                    |
| ----------------------------------- | --------------------------------------- | ------------------------------------ | -------------------------------------------------------------- |
| `BinaryTree.balance(t)`             | `(binary_tree)`                         | `binary_tree`                        | Return a height-balanced copy of the tree                      |
| `BinaryTree.all(t, fn)`             | `(binary_tree, function(T) -> boolean)` | `result<boolean>`                    | `true` if every value matches; fail if predicate throws        |
| `BinaryTree.any(t, fn)`             | `(binary_tree, function(T) -> boolean)` | `result<boolean>`                    | `true` if some value matches; fail if predicate throws         |
| `BinaryTree.ceiling_value(t, v)`    | `(binary_tree, T)`                      | `result<T>`                          | Smallest value ≥ `v`; fail if none exists                      |
| `BinaryTree.contains(t, v)`         | `(binary_tree, T)`                      | `boolean`                            | Whether `v` is in the tree                                     |
| `BinaryTree.count(t, fn)`           | `(binary_tree, function(T) -> boolean)` | `result<integer>`                    | Number of values matching `fn`; fail if predicate throws       |
| `BinaryTree.each(t, fn)`            | `(binary_tree, function(T) -> void)`    | `result<binary_tree>`                | Apply `fn` to each value in order; returns `t` unchanged       |
| `BinaryTree.equals(a, b)`           | `(binary_tree, binary_tree)`            | `boolean`                            | Whether both hold the same set of values (shape-independent)   |
| `BinaryTree.filter(t, fn)`          | `(binary_tree, function(T) -> boolean)` | `result<binary_tree>`                | Elements for which `fn` returns true; fail if predicate throws |
| `BinaryTree.find(t, fn)`            | `(binary_tree, function(T) -> boolean)` | `result<optional<T>>`                | First in-order value matching `fn`; `none` if no match         |
| `BinaryTree.floor_value(t, v)`      | `(binary_tree, T)`                      | `result<T>`                          | Largest value ≤ `v`; fail if none exists                       |
| `BinaryTree.from_array(arr)`        | `(array<T>)`                            | `binary_tree`                        | Build tree from array                                          |
| `BinaryTree.height(t)`              | `(binary_tree)`                         | `integer`                            | Tree height                                                    |
| `BinaryTree.inorder(t)`             | `(binary_tree)`                         | `array<T>`                           | In-order traversal (sorted)                                    |
| `BinaryTree.insert(t, v)`           | `(binary_tree, T)`                      | `binary_tree`                        | Insert value; returns new tree                                 |
| `BinaryTree.is_balanced(t)`         | `(binary_tree)`                         | `boolean`                            | Whether every node's subtree heights differ by at most 1       |
| `BinaryTree.is_empty(t)`            | `(binary_tree)`                         | `boolean`                            | Whether the tree is empty                                      |
| `BinaryTree.length(t)`              | `(binary_tree)`                         | `integer`                            | Number of nodes                                                |
| `BinaryTree.level_order(t)`         | `(binary_tree)`                         | `array<T>`                           | Level-order (breadth-first) traversal                          |
| `BinaryTree.map(t, fn)`             | `(binary_tree, function(T) -> U)`       | `result<binary_tree>`                | Apply `fn` to each value and rebuild the BST (re-sorted, deduplicated); fail if `fn` throws |
| `BinaryTree.max(t)`                 | `(binary_tree)`                         | `result<T>`                          | Maximum value; fail if empty                                   |
| `BinaryTree.min(t)`                 | `(binary_tree)`                         | `result<T>`                          | Minimum value; fail if empty                                   |
| `BinaryTree.new()`                  | `()`                                    | `binary_tree`                        | Empty tree                                                     |
| `BinaryTree.partition(t, fn)`       | `(binary_tree, function(T) -> boolean)` | `result<(binary_tree, binary_tree)>` | Split into `(matches, rest)`; fail if predicate throws         |
| `BinaryTree.predecessor(t, v)`      | `(binary_tree, T)`                      | `result<T>`                          | Largest value strictly less than `v`; fail if none exists      |
| `BinaryTree.postorder(t)`           | `(binary_tree)`                         | `array<T>`                           | Post-order traversal                                           |
| `BinaryTree.preorder(t)`            | `(binary_tree)`                         | `array<T>`                           | Pre-order traversal                                            |
| `BinaryTree.reduce(t, initial, fn)` | `(binary_tree, U, function(U, T) -> U)` | `result<U>`                          | Fold elements in order with accumulator; fail if `fn` throws   |
| `BinaryTree.remove(t, v)`           | `(binary_tree, T)`                      | `binary_tree`                        | Remove value; returns new tree                                 |
| `BinaryTree.successor(t, v)`        | `(binary_tree, T)`                      | `result<T>`                          | Smallest value strictly greater than `v`; fail if none exists  |
| `BinaryTree.to_array(t)`            | `(binary_tree)`                         | `array<T>`                           | Same as `inorder`                                              |
| `BinaryTree.union(a, b)`            | `(binary_tree, binary_tree)`            | `binary_tree`                        | Union of two trees; duplicates discarded                       |

## 4 — Calculus

Numerical calculus operations. Functions accept callable values (native functions like `Math.sine` or user-defined lambdas).

| Function                                        | Parameter Types                                                                    | Return Type             | Description                                                          |
| ----------------------------------------------- | ---------------------------------------------------------------------------------- | ----------------------- | -------------------------------------------------------------------- |
| `Calculus.convolution(f, g, t, a, b)`           | `(function(number) -> number, function(number) -> number, number, number, number)` | `number`                | Convolution `(f * g)(t)` integrated over `[a, b]`                    |
| `Calculus.arc_length(a, b, fn)`                 | `(number, number, function(number) -> number)`                                     | `number`                | Length of `y = f(x)` over `[a, b]` = `∫ √(1 + f'(x)²) dx`            |
| `Calculus.curl(point, fields)`                  | `(array<number>, array<function(array<number>) -> number>)`                        | `result<array<number>>` | Curl of a vector field at `point`; fail if dimensions mismatch       |
| `Calculus.derivative(x, fn)`                    | `(number, function(number) -> number)`                                             | `number`                | Numerical first derivative at `x`                                    |
| `Calculus.derivative_with(x, h, fn)`            | `(number, number, function(number) -> number)`                                     | `number`                | First derivative with custom step `h`                                |
| `Calculus.differentiate(fn)`                    | `(function(number) -> number)`                                                     | `function(number) -> number` | The derivative as a first-class function (`g(x) ≈ fn'(x)`)      |
| `Calculus.divergence(point, fields)`            | `(array<number>, array<function(array<number>) -> number>)`                        | `number`                | Divergence of a vector field at `point`                              |
| `Calculus.gradient(point, fn)`                  | `(array<number>, function(array<number>) -> number)`                               | `array<number>`         | Numerical partial derivatives at a point                             |
| `Calculus.hessian(point, fn)`                   | `(array<number>, function(array<number>) -> number)`                               | `array<array<number>>`  | Hessian matrix of second partial derivatives at `point`              |
| `Calculus.integrate(a, b, fn)`                  | `(number, number, function(number) -> number)`                                     | `number`                | Definite integral (Simpson's rule)                                   |
| `Calculus.integrate_with(a, b, n, fn)`          | `(number, number, integer, function(number) -> number)`                            | `number`                | Definite integral with `n` subdivisions                              |
| `Calculus.jacobian(point, fields)`              | `(array<number>, array<function(array<number>) -> number>)`                        | `result<array<array<number>>>` | Jacobian matrix; entry `(i, j)` = `∂fields[i]/∂x[j]` at `point` |
| `Calculus.laplacian(point, fn)`                 | `(array<number>, function(array<number>) -> number)`                               | `number`                | Sum of unmixed second partials `∇²f` (trace of the Hessian)         |
| `Calculus.limit(x, fn)`                         | `(number, function(number) -> number)`                                             | `result<number>`        | Numerical limit (Richardson extrapolation)                           |
| `Calculus.maximize(a, b, fn)`                   | `(number, number, function(number) -> number)`                                     | `(number, number)`      | Maximise over `[a, b]`; returns `(x_max, f(x_max))`                  |
| `Calculus.minimize(a, b, fn)`                   | `(number, number, function(number) -> number)`                                     | `(number, number)`      | Minimise over `[a, b]` (golden section); returns `(x_min, f(x_min))` |
| `Calculus.newton(x0, fn)`                       | `(number, function(number) -> number)`                                             | `result<number>`        | Root finding (Newton's method)                                       |
| `Calculus.nth_derivative(x, n, fn)`             | `(number, integer, function(number) -> number)`                                    | `number`                | The `n`-th derivative at `x` (`n ≥ 0`; `n = 0` ⇒ `fn(x)`)           |
| `Calculus.partial_derivative(point, index, fn)` | `(array<number>, integer, function(array<number>) -> number)`                      | `number`                | Partial derivative along axis `index` at `point`                     |
| `Calculus.product_series(start, n, fn)`         | `(integer, integer, function(number) -> number)`                                   | `number`                | Product `fn(start)` · ... · `fn(start + n - 1)` (empty ⇒ `1`)        |
| `Calculus.root(a, b, fn)`                       | `(number, number, function(number) -> number)`                                     | `result<number>`        | Root finding (bisection method)                                     |
| `Calculus.second_derivative(x, fn)`             | `(number, function(number) -> number)`                                             | `number`                | Numerical second derivative at `x`                                   |
| `Calculus.sum_series(start, n, fn)`             | `(integer, integer, function(number) -> number)`                                   | `number`                | Sum `fn(start)` + ... + `fn(start + n - 1)`                          |
| `Calculus.taylor(centre, n, fn)`                | `(number, integer, function(number) -> number)`                                    | `array<number>`         | Taylor series coefficients (1–20 terms)                              |

Callbacks that return `result<number>` (such as `Math.sine`) are automatically unwrapped.

## 5 — Channel

Thread-safe FIFO queues for passing values between tasks.

| Function                          | Parameter Types            | Return Type       | Description                                                                               |
| --------------------------------- | -------------------------- | ----------------- | ----------------------------------------------------------------------------------------- |
| `Channel.close(ch)`               | `(channel<T>)`             | `none`            | Close the channel                                                                         |
| `Channel.capacity(ch)`            | `(channel<T>)`             | `optional<integer>` | Buffered capacity; `none` for an unbounded channel                                      |
| `Channel.consume(ch, fn)`         | `(channel<T>, function(T) -> none)` | `result<integer>` | Receive each value until the channel closes and drains, applying `fn`; returns the count; fail if `fn` throws. Blocks until close — call inside a task |
| `Channel.is_closed(ch)`           | `(channel<T>)`             | `boolean`         | Whether the channel is closed                                                             |
| `Channel.is_empty(ch)`            | `(channel<T>)`             | `boolean`         | Whether no values are buffered                                                            |
| `Channel.is_full(ch)`             | `(channel<T>)`             | `boolean`         | Whether a buffered channel is at capacity; always `false` when unbounded                  |
| `Channel.length(ch)`              | `(channel<T>)`             | `integer`         | Number of buffered values                                                                 |
| `Channel.new()`                   | `()`                       | `channel<T>`      | Create unbounded channel (no capacity limit)                                              |
| `Channel.new_buffered(cap)`       | `(integer)`                | `channel<T>`      | Create buffered channel; throws if `cap ≤ 0`                                              |
| `Channel.poll(ch)`                | `(channel<T>)`             | `optional<T>`     | Non-blocking, non-throwing receive; `some(v)` when ready, `none` when empty (open or drained-closed). Pair with `is_closed` to tell empty-open from drained-closed |
| `Channel.receive(ch)`             | `(channel<T>)`             | `T`               | Blocking receive; throws `ChannelClosedError` if closed and drained                       |
| `Channel.receive_all(ch)`         | `(channel<T>)`             | `array<T>`        | Drain all buffered values                                                                 |
| `Channel.receive_timeout(ch, ms)` | `(channel<T>, integer)`    | `result<T>`       | Timed receive; fail on timeout, throws `ChannelClosedError` if closed                     |
| `Channel.select(channels)`        | `(array<channel<T>>)`      | `result<T>`       | Wait for the first ready channel; returns `(index, value)`; fail if all are closed        |
| `Channel.send(ch, v)`             | `(channel<T>, T)`          | `boolean`         | Blocking send; returns `false` if the channel is closed                                      |
| `Channel.send_all(ch, values)`    | `(channel<T>, array<T>)`   | `integer`         | Blocking-send each element in order; returns the count sent, stopping early if the channel closes mid-send |
| `Channel.send_timeout(ch, v, ms)` | `(channel<T>, T, integer)` | `result<boolean>` | Timed send; fail on timeout, throws `ChannelClosedError` if closed                        |
| `Channel.try_receive(ch)`         | `(channel<T>)`             | `T`               | Non-blocking receive; throws `ChannelEmptyError` if empty, `ChannelClosedError` if closed |
| `Channel.try_send(ch, v)`         | `(channel<T>, T)`          | `boolean`         | Non-blocking send; returns `false` if the channel is full or closed      |

Values are deep-copied on send to prevent shared mutable state between tasks.

**Channel Error Types:**

Channel operations use typed exceptions instead of result types for error conditions:

| Exception            | Thrown When                                                 |
| -------------------- | ----------------------------------------------------------- |
| `ChannelClosedError` | Sending to or receiving from a channel that has been closed |
| `ChannelFullError`   | Non-blocking send (`try_send`) on a full buffered channel   |
| `ChannelEmptyError`  | Non-blocking receive (`try_receive`) on an empty channel    |

These are runtime errors catchable with `try`/`catch`. Use `Channel.is_closed(ch)` to check channel state without throwing.

## 6 — Compression

Compress and decompress data using Deflate (RFC 1951), Gzip (RFC 1952), and run-length encoding.

| Function                                     | Parameter Types                | Return Type      | Description                                             |
| --------------------------------------------- | ------------------------------- | ---------------- | -------------------------------------------------------- |
| `Compression.compress(data, format)`         | `(string, Compression.Format)`  | `string`         | Compress `data` under the given format                  |
| `Compression.compressed_size(data)`          | `(string)`                      | `integer`        | Compressed size in bytes                                |
| `Compression.decode_rle(s)`                  | `(string)`                      | `result<string>` | Run-length decode                                       |
| `Compression.decompress(data, format)`       | `(string, Compression.Format)`  | `result<string>` | Decompress `data` under the given format                |
| `Compression.decompress_typed(data, format)` | `(string, Compression.Format)`  | `result<string, Compression.Error>` | Decompress `data`; on failure the error is a typed `Compression.Error` |
| `Compression.deflate(data)`                  | `(string)`                      | `string`         | Deflate-compress data                                   |
| `Compression.deflate_with(data, level)`      | `(string, integer)`             | `result<string>` | Deflate-compress with explicit level (0–9)              |
| `Compression.detect_format(data)`            | `(string)`                      | `optional<Compression.Format>` | Sniff the container from magic bytes (`Gzip`/`Zlib`); `none` if unrecognised |
| `Compression.encode_rle(s)`                  | `(string)`                      | `string`         | Run-length encode (e.g. `"aaabbbcc"` → `"3a3b2c"`)      |
| `Compression.gunzip(data)`                   | `(string)`                      | `result<string>` | Gunzip-decompress data                                  |
| `Compression.gunzip_file(path)`              | `(string)`                      | `result<string>` | Gunzip-decompress file contents                         |
| `Compression.gunzip_typed(data)`             | `(string)`                      | `result<string, Compression.Error>` | Gunzip-decompress data; on failure the error is a typed `Compression.Error` |
| `Compression.gzip(data)`                     | `(string)`                      | `string`         | Gzip-compress data                                      |
| `Compression.gzip_file(in, out)`             | `(string, string)`              | `result<string>` | Gzip-compress file to output path                       |
| `Compression.gzip_file_with(in, out, level)` | `(string, string, integer)`     | `result<string>` | Gzip-compress a file to `out` with explicit level (0–9) |
| `Compression.gzip_with(data, level)`         | `(string, integer)`             | `result<string>` | Gzip-compress with explicit level (0–9)                 |
| `Compression.inflate(data)`                  | `(string)`                      | `result<string>` | Inflate-decompress data                                 |
| `Compression.inflate_typed(data)`            | `(string)`                      | `result<string, Compression.Error>` | Inflate-decompress data; on failure the error is a typed `Compression.Error` |
| `Compression.zlib_compress(data)`            | `(string)`                      | `string`         | Zlib-wrapped deflate (RFC 1950)                         |
| `Compression.zlib_compress_with(data, level)`| `(string, integer)`             | `result<string>` | Zlib-compress with explicit level (0–9)                 |
| `Compression.zlib_decompress(data)`          | `(string)`                      | `result<string>` | Zlib-decompress (RFC 1950) data                         |
| `Compression.zlib_decompress_typed(data)`    | `(string)`                      | `result<string, Compression.Error>` | Zlib-decompress data; on failure the error is a typed `Compression.Error` |

The per-algorithm functions above (`deflate`/`inflate`, `gzip`/`gunzip`, `zlib_compress`/`zlib_decompress`, `encode_rle`/`decode_rle`) are the primary API — reach for them when the algorithm is known at the call site. `Compression.Format` is a choice type with four variants — `Deflate`, `Gzip`, `Zlib`, `Rle` — for the "algorithm decided at runtime" path: `Compression.compress` and `Compression.decompress` dispatch on it to the matching per-algorithm function, so code that only learns the format from user input or configuration doesn't need a hand-written switch over algorithm names. Unlike `Hash.Algorithm`, `Compression.Format` has no string dual-form — it is the sole runtime-dispatch entry point. `Compression.detect_format(data)` sniffs the container from its leading bytes (gzip's `1f 8b` magic or a valid zlib header), returning `some(Compression.Format)` or `none`, so `data |> Compression.detect_format()` composes with `decompress` for a format-agnostic decode. The `Zlib` variant is the RFC 1950 zlib wrapper (2-byte header + Adler-32 trailer) — the format Python's `zlib` module and most `Content-Encoding: deflate` HTTP bodies actually use, sitting between raw `Deflate` (RFC 1951) and `Gzip` (RFC 1952).

```luma
Compression.Format format = Compression.Format.Gzip
string compressed = Compression.compress("hello world", format)
string restored = Result.unwrap(Compression.decompress(compressed, format))
```

`Compression.Error` is a choice type with four variants — `Corrupt`, `Truncated`, `UnsupportedFormat`, `TooLarge` — that categorises _why_ a decompression failed, so a program decoding an untrusted or truncated blob can branch on the cause instead of substring-matching an opaque message. It is surfaced by the opt-in `*_typed` companions — `Compression.decompress_typed`, `Compression.inflate_typed`, and `Compression.gunzip_typed` — which return `result<string, Compression.Error>`: the value on success is the decompressed data, and the error on failure is the typed category. Well-framed but internally inconsistent data (bad deflate codes, a gzip CRC/size trailer mismatch, an invalid RLE count digit) is `Corrupt`; a stream that ends before a complete unit was decoded is `Truncated`; a container that is not the expected format (bad gzip magic, or a compression method other than deflate) is `UnsupportedFormat`; and output that would exceed the interpreter's maximum string size is `TooLarge`. This is additive (mirroring `FileSystem.read_file_typed` / `FileSystem.IoError`): the plain `decompress`, `inflate`, and `gunzip` keep their string-error `result<string>`.

```luma
result<string, Compression.Error> decoded = Compression.gunzip_typed(untrusted_bytes)
match decoded {
    success(data) { use(data) }
    failure(Compression.Error.Truncated) { print("incomplete download, retrying") }
    failure(Compression.Error.UnsupportedFormat) { print("not a gzip file") }
    failure(_other) { print("could not decompress") }
}
```

## 7 — Converter

Convert values between different types (e.g. string → integer, integer → string).

> **Converter vs Encoder** — `Converter` changes the **type** of a value (e.g. `"255"` → `255`). For transforming the **representation** of a string while keeping it a string (e.g. Base64 or URL percent-encoding), use `Encoder` instead.

| Function                               | Parameter Types | Return Type       | Description                                                  |
| -------------------------------------- | --------------- | ----------------- | ------------------------------------------------------------ |
| `Converter.character_to_codepoint(ch)` | `(string)`      | `result<integer>` | Unicode codepoint of character; fail on empty string         |
| `Converter.codepoint_to_character(cp)` | `(integer)`     | `result<string>`  | Character from codepoint; fail on invalid codepoint          |
| `Converter.from_binary(s)`             | `(string)`      | `result<integer>` | Parse binary string (e.g. `"1010"` → 10)                     |
| `Converter.from_hexadecimal(s)`        | `(string)`      | `result<integer>` | Parse hex string (e.g. `"ff"` → 255)                         |
| `Converter.from_octal(s)`              | `(string)`      | `result<integer>` | Parse octal string (e.g. `"755"` → 493); fail on non-octal digits |
| `Converter.from_roman(s)`              | `(string)`      | `result<integer>` | Parse Roman numeral (e.g. `"XIV"` → 14)                      |
| `Converter.number_to_words(n)`         | `(integer)`     | `string`          | English words (e.g. `42` → `"forty two"`)                    |
| `Converter.ordinal(n)`                 | `(integer)`     | `string`          | Ordinal suffix (e.g. `3` → `"3rd"`)                          |
| `Converter.to_binary(n)`               | `(integer)`     | `string`          | Binary representation (e.g. `10` → `"1010"`)                 |
| `Converter.to_boolean(s)`              | `(string)`      | `result<boolean>` | Parse `"true"`/`"false"`; throws if argument is not a string |
| `Converter.to_hexadecimal(n)`          | `(integer)`     | `string`          | Hex representation (e.g. `255` → `"ff"`)                     |
| `Converter.to_octal(n)`                | `(integer)`     | `string`          | Octal representation (e.g. `493` → `"755"`)                  |
| `Converter.to_integer(v)`              | `(number\       | string)`          | `result<integer>`                                            |
| `Converter.to_number(v)`               | `(integer\      | string)`          | `result<number>`                                             |
| `Converter.to_roman(n)`                | `(integer)`     | `result<string>`  | Roman numeral; fail if value outside [1, 3999]               |
| `Converter.to_string(v)`               | `(T)`           | `string`          | String representation of any value                           |

## 8 — Csv

Parse and serialise comma-separated values.

| Function                         | Parameter Types                              | Return Type                         | Description                                                       |
| -------------------------------- | -------------------------------------------- | ----------------------------------- | ----------------------------------------------------------------- |
| `Csv.count_rows(s)`              | `(string)`                                   | `result<integer>`                   | Number of data rows (excludes header)                             |
| `Csv.default_dialect()`          | `()`                                         | `Csv.Dialect`                       | Default dialect (comma delimiter, double quote)                   |
| `Csv.dialect(delimiter, quote)`  | `(string, string)`                           | `Csv.Dialect`                       | Build a typed dialect from a delimiter and quote character        |
| `Csv.deserialize_records(s)`     | `(string)`                                   | `result<array<dictionary<string>>>` | Parse CSV with header row into records                            |
| `Csv.deserialize(s)`             | `(string)`                                   | `result<array<array<string>>>`      | Parse CSV string into rows of fields                              |
| `Csv.deserialize_detailed(s)`    | `(string)`                                   | `result<array<array<string>>, Csv.ParseError>` | Like `Csv.deserialize`, but a failure carries the located `Csv.ParseError` |
| `Csv.header(s)`                  | `(string)`                                   | `result<array<string>>`             | Extract header row                                                |
| `Csv.deserialize_with(s, opts)`  | `(string, Csv.Dialect \| dictionary<string>)` | `result<array<array<string>>>`     | Parse with custom delimiter/quoting                               |
| `Csv.deserialize_table(s)`       | `(string)`                                   | `result<Csv.Table, Csv.ParseError>` | Parse into a `Csv.Table` (first row is the header, the rest are positional rows) |
| `Csv.serialize_table(t)`         | `(Csv.Table)`                                | `result<string>`                    | Serialise a `Csv.Table` (header row first, then each positional row) |
| `Csv.column(t, name)`            | `(Csv.Table, string)`                        | `result<array<string>>`             | Extract one named column from a `Csv.Table`; fail if no header matches |
| `Csv.row(t, index)`              | `(Csv.Table, integer)`                       | `result<dictionary<string>>`        | Extract one 0-based data row as a header-keyed record (short rows padded with `""`); fail on out-of-bounds |
| `Csv.select(t, names)`           | `(Csv.Table, array<string>)`                 | `result<Csv.Table>`                 | Project the named columns (in order) into a new `Csv.Table`; fail if any name is absent |
| `Csv.filter_rows(t, fn)`         | `(Csv.Table, function(dictionary<string>) -> boolean)` | `result<Csv.Table>`      | Keep data rows where `fn(row-record)` is true, preserving headers; fail if the predicate throws |
| `Csv.read_file(path)`            | `(string)`                                   | `result<array<dictionary<string>>>` | Read and parse CSV file                                           |
| `Csv.serialize(rows)`            | `(array<array<string>>)`                     | `result<string>`                    | Serialise rows to CSV string; fail if row is not array            |
| `Csv.serialize_records(records)` | `(array<dictionary<string>>)`                | `string`                            | Serialise records to CSV with header                              |
| `Csv.serialize_with(rows, opts)` | `(array<array<string>>, Csv.Dialect \| dictionary<string>)` | `result<string>`     | Serialise with custom delimiter/quoting; fail if row is not array |
| `Csv.write_file(path, records)`  | `(string, array<dictionary<string>>)`        | `result<boolean>`                   | Write records to CSV file                                         |

Quoted fields, embedded commas, and escaped quotes are handled. `Csv.deserialize_with` /
`Csv.serialize_with` accept options as either a typed **`Csv.Dialect`** record (built with
`Csv.dialect(delimiter, quote)` or `Csv.default_dialect()`) or the legacy options dictionary with
`"delimiter"` (single char) and `"quote"` (single char) keys — the record form is type-checked and
discoverable, so a mistyped field is a compile error rather than a silently ignored key.

**`Csv.Dialect`** record fields: `delimiter: string` (single character), `quote: string` (single
character).

**`Csv.Table`** record fields: `headers: array<string>`, `rows: array<array<string>>`. It carries the header row once plus the positional data rows, so it preserves column order (which the `array<dictionary<string>>` shape of `Csv.deserialize_records` loses) and keeps the header even when there are zero data rows. `Csv.deserialize_table(s)` parses into it (returning `Csv.ParseError` on a malformed CSV, like `deserialize_detailed`), `Csv.serialize_table(t)` round-trips it back to CSV text, and `Csv.column(t, name)` pulls a single column by header name as `array<string>` (short rows are padded with `""`), returning `failure` when no header matches. It sits alongside the existing row and record shapes — reach for it when you want "print the columns, then the rows" with column order intact.

`Csv.deserialize_detailed(s)` is an additive companion to `Csv.deserialize` — it leaves
`Csv.deserialize` unchanged and returns `result<array<array<string>>, Csv.ParseError>`, where
**`Csv.ParseError`** is a record — `message: string`, `line: integer`, `column: integer` (both
1-based) — so a program parsing a malformed CSV can point at the row/column that broke rather than
surface a bare string. Mirrors `Json.parse_detailed` / `Json.ParseError`.

## 9 — DateTime

| Function                                   | Parameter Types                                          | Return Type                  | Description                                                              |
| ------------------------------------------ | -------------------------------------------------------- | ---------------------------- | ------------------------------------------------------------------------ |
| `DateTime.add_days(ts, n)`                 | `(number, number)`                                       | `number`                     | Add `n` days to a Unix timestamp                                         |
| `DateTime.add_hours(ts, n)`                | `(number, number)`                                       | `number`                     | Add `n` hours to a Unix timestamp                                        |
| `DateTime.add_months(ts, n)`               | `(number, integer)`                                      | `result<number>`             | Add `n` calendar months (clamps day); fail if out of range               |
| `DateTime.add_milliseconds(ts, n)`         | `(number, integer)`                                      | `number`                     | Add `n` milliseconds to a Unix timestamp                                 |
| `DateTime.add_minutes(ts, n)`              | `(number, number)`                                       | `number`                     | Add `n` minutes to a Unix timestamp                                      |
| `DateTime.add_seconds(ts, n)`              | `(number, number)`                                       | `number`                     | Add `n` seconds to a Unix timestamp                                      |
| `DateTime.add_weeks(ts, n)`                | `(number, number)`                                       | `number`                     | Add `n` weeks to a Unix timestamp                                        |
| `DateTime.add_years(ts, n)`                | `(number, integer)`                                      | `result<number>`             | Add `n` calendar years (clamps Feb 29); fail if out of range             |
| `DateTime.period(years, months, days)`     | `(integer, integer, integer)`                            | `DateTime.Period`            | Construct a calendar span (any component may be negative)                |
| `DateTime.add_period(ts, p)`               | `(number, DateTime.Period)`                              | `result<number>`             | Add a calendar span (year/month with day clamping, then whole days); fail if out of range |
| `DateTime.between_dates(a, b)`             | `(number, number)`                                       | `result<DateTime.Period>`    | The calendar span from `a` to `b` (date components only; negative if `a` is after `b`) |
| `DateTime.break_duration(total_seconds)`   | `(number)`                                               | `DateTime.Duration`          | Break a span in seconds into a `days`/`hours`/`minutes`/`seconds`/`milliseconds` record (with a `negative` flag) |
| `DateTime.day_of_month(ts)`                | `(number)`                                               | `result<integer>`            | Day of month (1–31); fail if out of supported range (year 0001–9999)     |
| `DateTime.day_of_week(ts)`                 | `(number)`                                               | `result<integer>`            | 1 (Monday) to 7 (Sunday); fail if out of range                           |
| `DateTime.day_of_year(ts)`                 | `(number)`                                               | `result<integer>`            | Ordinal day of the year (1–366, leap-aware); fail if out of range        |
| `DateTime.days_in_month(year, month)`      | `(integer, integer)`                                     | `result<integer>`            | Days in given month; fail if month not in [1, 12]                        |
| `DateTime.difference_days(t1, t2)`         | `(number, number)`                                       | `number`                     | Absolute difference in days                                              |
| `DateTime.difference_hours(t1, t2)`        | `(number, number)`                                       | `number`                     | Absolute difference in hours                                             |
| `DateTime.difference_months(t1, t2)`       | `(number, number)`                                       | `result<integer>`            | Absolute difference in calendar months; fail if out of range             |
| `DateTime.difference_milliseconds(t1, t2)` | `(number, number)`                                       | `number`                     | Difference in milliseconds                                               |
| `DateTime.difference_minutes(t1, t2)`      | `(number, number)`                                       | `number`                     | Absolute difference in minutes                                           |
| `DateTime.difference_seconds(t1, t2)`      | `(number, number)`                                       | `number`                     | Absolute difference in seconds                                           |
| `DateTime.difference_weeks(t1, t2)`        | `(number, number)`                                       | `number`                     | Absolute difference in weeks                                             |
| `DateTime.difference_years(t1, t2)`        | `(number, number)`                                       | `result<integer>`            | Absolute difference in calendar years; fail if out of range              |
| `DateTime.end_of_day(ts)`                  | `(number)`                                               | `result<number>`             | Last second of the timestamp's day (UTC); fail if out of range           |
| `DateTime.end_of_hour(ts)`                 | `(number)`                                               | `result<number>`             | Last second of the timestamp's hour (UTC); fail if out of range          |
| `DateTime.end_of_month(ts)`                | `(number)`                                               | `result<number>`             | Last second of the timestamp's month (UTC); fail if out of range         |
| `DateTime.end_of_year(ts)`                 | `(number)`                                               | `result<number>`             | Last second of the timestamp's year (UTC); fail if out of range          |
| `DateTime.from_iso_string(s)`              | `(string)`                                               | `result<number>`             | Parse ISO 8601 string to Unix timestamp                                  |
| `DateTime.from_iso_string_typed(s)`        | `(string)`                                               | `result<number, DateTime.ParseError>` | Parse ISO 8601; on failure the error is a typed `DateTime.ParseError` instead of a string |
| `DateTime.combine(date, time)`             | `(DateTime.Date, DateTime.Time)`                         | `result<number>`             | Fuse a calendar date and a wall-clock time into a UTC timestamp          |
| `DateTime.date(y, m, d)`                   | `(integer, integer, integer)`                            | `result<DateTime.Date>`      | Build a validated calendar-only date; fail if out of range              |
| `DateTime.date_of(ts)`                     | `(number)`                                               | `result<DateTime.Date>`      | Extract the calendar date from a timestamp                              |
| `DateTime.time(h, min, s)`                 | `(integer, integer, integer)`                            | `result<DateTime.Time>`      | Build a validated wall-clock-only time; fail if out of range           |
| `DateTime.time_of(ts)`                     | `(number)`                                               | `result<DateTime.Time>`      | Extract the wall-clock time from a timestamp                           |
| `DateTime.from_parts(y, m, d, h, min, s)`  | `(integer, integer, integer, integer, integer, integer)` | `result<number>`             | Build timestamp from components; fail if out of range                    |
| `DateTime.format(ts, pattern)`             | `(number, string)`                                       | `result<string>`             | Format timestamp; placeholders: YYYY, MM, DD, hh, mm, ss                 |
| `DateTime.format_duration(d)`              | `(DateTime.Duration)`                                    | `string`                     | Render a `DateTime.Duration` as `"1d 2h 3m 4s 5ms"` (zero components omitted; `"0s"` when empty; `-` prefix when negative) |
| `DateTime.hour(ts)`                        | `(number)`                                               | `result<integer>`            | Hour (0–23); fail if out of range                                        |
| `DateTime.interval(start, end)`            | `(number, number)`                                       | `result<DateTime.Interval>`  | Build a time interval; fail if `end < start`                             |
| `DateTime.interval_contains(iv, ts)`       | `(DateTime.Interval, number)`                            | `boolean`                    | Whether `ts` lies within the closed interval                             |
| `DateTime.interval_duration(iv)`           | `(DateTime.Interval)`                                    | `number`                     | Length of the interval in seconds (`end - start`)                        |
| `DateTime.intervals_overlap(a, b)`         | `(DateTime.Interval, DateTime.Interval)`                 | `boolean`                    | Whether two closed intervals overlap (touching counts)                   |
| `DateTime.is_after(a, b)`                  | `(number, number)`                                       | `boolean`                    | Whether timestamp `a` is after `b`                                       |
| `DateTime.is_before(a, b)`                 | `(number, number)`                                       | `boolean`                    | Whether timestamp `a` is before `b`                                      |
| `DateTime.is_leap_year(year)`              | `(integer)`                                              | `boolean`                    | Whether `year` is a leap year                                            |
| `DateTime.is_weekday(ts)`                  | `(number)`                                               | `boolean`                    | Whether the timestamp falls on Monday–Friday                             |
| `DateTime.is_weekend(ts)`                  | `(number)`                                               | `boolean`                    | Whether the timestamp falls on Saturday or Sunday                        |
| `DateTime.minute(ts)`                      | `(number)`                                               | `result<integer>`            | Minute (0–59); fail if out of range                                      |
| `DateTime.month(ts)`                       | `(number)`                                               | `result<integer>`            | Month (1–12); fail if out of range                                       |
| `DateTime.month_from_number(n)`            | `(integer)`                                              | `result<DateTime.Month>`     | `DateTime.Month` from 1 (January)–12 (December); fail if `n` not in [1, 12] |
| `DateTime.month_name(m)`                   | `(DateTime.Month)`                                       | `string`                     | English month name, `"January"`–`"December"`                             |
| `DateTime.month_number(m)`                 | `(DateTime.Month)`                                       | `integer`                    | Number of a month, 1 (January)–12 (December)                             |
| `DateTime.month_of(ts)`                    | `(number)`                                               | `result<DateTime.Month>`     | Month of a timestamp as a `DateTime.Month` choice; fail if out of range  |
| `DateTime.milliseconds_since_start()`      | `()`                                                     | `number`                     | Milliseconds since program start                                         |
| `DateTime.now_iso_string()`                | `()`                                                     | `result<string>`             | Current time as `"YYYY-MM-DDTHH:MM:SSZ"`                                 |
| `DateTime.now_unix()`                      | `()`                                                     | `number`                     | Current Unix timestamp                                                   |
| `DateTime.parse(text, pattern)`            | `(string, string)`                                       | `result<number>`             | Parse `text` against a pattern (placeholders YYYY, MM, DD, hh, mm, ss) to a Unix timestamp; fail on mismatch or invalid fields |
| `DateTime.second(ts)`                      | `(number)`                                               | `result<integer>`            | Second (0–59); fail if out of range                                      |
| `DateTime.start_of_day(ts)`                | `(number)`                                               | `result<number>`             | First second of the timestamp's day (UTC); fail if out of range          |
| `DateTime.start_of_hour(ts)`               | `(number)`                                               | `result<number>`             | First second of the timestamp's hour (UTC); fail if out of range         |
| `DateTime.start_of_month(ts)`              | `(number)`                                               | `result<number>`             | First second of the timestamp's month (UTC); fail if out of range        |
| `DateTime.start_of_year(ts)`               | `(number)`                                               | `result<number>`             | First second of the timestamp's year (UTC); fail if out of range         |
| `DateTime.to_iso_string(ts)`               | `(number)`                                               | `result<string>`             | Format as `"YYYY-MM-DDTHH:MM:SSZ"`; fail if out of range                 |
| `DateTime.to_parts(ts)`                    | `(number)`                                               | `result<DateTime.TimeParts>` | Record with year, month, day, hour, minute, second; fail if out of range |
| `DateTime.weekday(ts)`                     | `(number)`                                               | `result<DateTime.Weekday>`   | Weekday of a timestamp as a `DateTime.Weekday` choice; fail if out of range |
| `DateTime.weekday_from_number(n)`          | `(integer)`                                              | `result<DateTime.Weekday>`   | `DateTime.Weekday` from 1 (Monday)–7 (Sunday); fail if `n` not in [1, 7]  |
| `DateTime.weekday_name(w)`                 | `(DateTime.Weekday)`                                     | `string`                     | English day name, `"Monday"`–`"Sunday"`                                   |
| `DateTime.weekday_number(w)`               | `(DateTime.Weekday)`                                     | `integer`                    | ISO number of a weekday, 1 (Monday)–7 (Sunday)                           |
| `DateTime.week_of_year(ts)`                | `(number)`                                               | `integer`                    | ISO 8601 week number (1–53; Monday-start, first-Thursday rule)           |
| `DateTime.year(ts)`                        | `(number)`                                               | `result<integer>`            | Four-digit year; fail if out of range                                    |

`DateTime` also exposes duration constants: `DateTime.seconds_per_minute` (60), `DateTime.seconds_per_hour` (3600), `DateTime.seconds_per_day` (86400), and `DateTime.days_per_week` (7) — all `integer`.

### Timezone Support (Fixed UTC Offsets)

All `DateTime` timestamps are in UTC. The following functions convert between UTC and a fixed UTC offset expressed in **minutes** (e.g. `330` for UTC+05:30, `-300` for UTC−05:00). Use `DateTime.offset_hours` to convert hours to minutes.

| Function                                                 | Parameter Types                                                  | Return Type      | Description                                            |
| -------------------------------------------------------- | ---------------------------------------------------------------- | ---------------- | ------------------------------------------------------ |
| `DateTime.from_offset(ts, offset)`                       | `(number, number)`                                               | `result<number>` | Local timestamp → UTC                                  |
| `DateTime.from_parts_offset(y, m, d, h, min, s, offset)` | `(integer, integer, integer, integer, integer, integer, number)` | `result<number>` | Build local date/time and return UTC timestamp         |
| `DateTime.offset_hours(h)`                               | `(number)`                                                       | `number`         | Convert hours to offset minutes (e.g. `5.5` → `330.0`) |
| `DateTime.to_iso_string_offset(ts, offset)`              | `(number, number)`                                               | `result<string>` | Format with UTC offset suffix                          |
| `DateTime.to_offset(ts, offset)`                         | `(number, number)`                                               | `result<number>` | UTC → local timestamp                                  |
| `DateTime.zoned(ts, offset_minutes)`                     | `(number, integer)`                                              | `result<DateTime.Zoned>` | Bundle an instant with its UTC offset; fail if the offset is out of range |
| `DateTime.zoned_to_iso_string(z)`                        | `(DateTime.Zoned)`                                               | `result<string>` | ISO 8601 string rendered in the Zoned's own offset     |
| `DateTime.zoned_to_parts(z)`                             | `(DateTime.Zoned)`                                               | `result<DateTime.TimeParts>` | Broken-down local components in the Zoned's offset |

Valid offsets range from −720 (UTC−12:00) to +840 (UTC+14:00) minutes. Out-of-range offsets return `failure`. A zero offset produces the `"Z"` suffix in ISO strings.

`DateTime.Zoned` record fields: `timestamp` (`number`, Unix seconds) and `offset_minutes` (`integer`). It keeps an instant and the UTC offset it should be rendered in together, so a timestamp can no longer drift apart from its offset the way the loose-argument `to_offset` / `to_iso_string_offset` family allows. `DateTime.zoned(timestamp, offset_minutes)` validates the offset (same −720…+840 range) so a `Zoned` is always well-formed; `DateTime.zoned_to_iso_string` formats it with the correct offset suffix (or `"Z"` at offset 0), and `DateTime.zoned_to_parts` returns the local broken-down `DateTime.TimeParts`. The timestamp stays a plain `number`, so every existing point helper still applies.

`DateTime.TimeParts` record fields: `year`, `month`, `day`, `hour`, `minute`, `second` (all `integer`).

`DateTime.Date` (fields `year`, `month`, `day`) and `DateTime.Time` (fields `hour`, `minute`, `second`) — all `integer` — are the calendar-only and wall-clock-only counterparts to the full `TimeParts` breakdown. A `Date` models a value that is genuinely only a calendar date (a birthday, a due date, with no time-of-day) and a `Time` only a wall-clock time (an alarm, opening hours, with no date), so neither carries the fields it should not — they complement, rather than re-slice, `TimeParts`. `DateTime.date(y, m, d)` and `DateTime.time(h, min, s)` are validating constructors (a `Date` is always a real calendar day, respecting leap years and month lengths; a `Time` is always a legal `00:00:00`–`23:59:59`), `DateTime.date_of(ts)` / `DateTime.time_of(ts)` extract each partial view from an instant, and `DateTime.combine(date, time)` fuses them back into a UTC timestamp. Timestamps stay plain `number` values, so this does not reintroduce a newtype over the instant.

```luma
DateTime.Date d = Result.unwrap(DateTime.date(2024, 6, 15))
DateTime.Time t = Result.unwrap(DateTime.time(9, 30, 0))
result<number> ts = DateTime.combine(d, t)
```

`DateTime.Duration` record fields: `days`, `hours`, `minutes`, `seconds`, `milliseconds` (all `integer`), and `negative` (`boolean`). `break_duration` splits a `number` span in seconds into this human-readable breakdown — the `hours`/`minutes`/`seconds` components are normalised (0–23, 0–59, 0–59) and the sign is carried in `negative` — and `format_duration` renders it back into a compact string such as `"1h 2m 5s"`.

`DateTime.Period` record fields: `years`, `months`, `days` (all `integer`, any may be negative). Where `DateTime.Duration` models a fixed wall-clock span (a number of seconds), a `Period` models a **calendar** span — "1 year, 2 months, 3 days" — whose real length depends on which month and year it is applied to. `DateTime.period(years, months, days)` is a total constructor; `DateTime.add_period(ts, p)` applies it to an instant (the year and month components first, clamping the day to the target month's length exactly like `add_months`/`add_years`, then the whole-day component), returning `failure` only when the result leaves the supported range; and `DateTime.between_dates(a, b)` measures the calendar span between two instants using their date components only (time-of-day is ignored), yielding a negative `Period` when `a` is after `b`. The two shapes are complementary: keep `Duration` for elapsed time, reach for `Period` for "add one month" calendar arithmetic.

`DateTime.Interval` record fields: `start` (`number`), `end` (`number`) — a pair of Unix timestamps modelling a time range rather than a point. `DateTime.interval(start, end)` is a validating constructor that returns `failure` when `end < start`, so an `Interval` value is always well-formed. Intervals are closed (both endpoints are included): `DateTime.interval_contains` treats a timestamp equal to `start` or `end` as contained, and `DateTime.intervals_overlap` counts two intervals that merely touch at an endpoint as overlapping. `DateTime.interval_duration` returns `end - start` in seconds. Timestamps stay plain `number` values, so the existing `DateTime` point helpers still apply.

`DateTime.Weekday` is a choice type with seven variants — `Monday`, `Tuesday`, `Wednesday`, `Thursday`, `Friday`, `Saturday`, `Sunday` — in ISO-8601 order (Monday = 1 … Sunday = 7). `DateTime.weekday(ts)` returns it for a timestamp, so a `match` over the result is exhaustive and autocompleted, and a mistyped day is a compile error rather than a magic number. It complements the integer `DateTime.day_of_week` (kept for index-style use): `DateTime.weekday_number` and `DateTime.weekday_from_number` bridge between the choice and the 1–7 integer, and `DateTime.weekday_name` gives the English day name.

```luma
match Result.unwrap(DateTime.weekday(DateTime.now_unix())) {
case DateTime.Weekday.Saturday { print("weekend!") }
case DateTime.Weekday.Sunday   { print("weekend!") }
else                           { print("weekday") }
}
```

`DateTime.Month` is a choice type with twelve variants — `January`, `February`, `March`, `April`, `May`, `June`, `July`, `August`, `September`, `October`, `November`, `December` — in calendar order (January = 1 … December = 12). `DateTime.month_of(ts)` returns it for a timestamp (named to avoid clashing with the integer `DateTime.month`, which is kept for index-style use), so a `match` over the result is exhaustive and autocompleted, and a mistyped month is a compile error rather than a magic number. `DateTime.month_number` and `DateTime.month_from_number` bridge between the choice and the 1–12 integer, and `DateTime.month_name` gives the English month name.

```luma
match Result.unwrap(DateTime.month_of(DateTime.now_unix())) {
case DateTime.Month.December { print("year end!") }
else                         { print(DateTime.month_name(Result.unwrap(DateTime.month_of(DateTime.now_unix())))) }
}
```

`DateTime.ParseError` is a choice type with four variants — `Empty`, `InvalidFormat`, `OutOfRange`, `UnsupportedPrecision` — that categorises _why_ an ISO-8601 string failed to parse, so a program can branch on the cause instead of substring-matching an opaque message. It is surfaced by `DateTime.from_iso_string_typed(s)`, which returns `result<number, DateTime.ParseError>`: the value on success is the UTC Unix timestamp, and the error on failure is the typed category. An empty or whitespace-only string is `Empty`; a present but malformed string (`"not-a-date"`, `"2024/03/15"`, a truncated time) is `InvalidFormat`; a well-formed shape with an impossible field (month 13, day 30 of February, a year outside 0001–9999) is `OutOfRange`; and a valid shape carrying sub-second precision this parser does not accept (`"...:00.5Z"`) is `UnsupportedPrecision`. This is an opt-in, additive companion (mirroring `FileSystem.read_file_typed` / `FileSystem.IoError`): the plain `DateTime.from_iso_string` keeps its string-error `result<number>`.

```luma
match DateTime.from_iso_string_typed(user_input) {
success(ts) { print("parsed: ${ts}") }
failure(DateTime.ParseError.Empty)          { print("please enter a date") }
failure(DateTime.ParseError.OutOfRange)     { print("that date can't exist") }
failure(_other)                             { print("that isn't a valid ISO-8601 date") }
}
```

## 10 — Decimal

Exact base-10 arithmetic. Unlike `number` (IEEE-754 binary floating point, where `0.1 + 0.2` is not exactly `0.3`), a `decimal` stores its value in base 10, so money and other decimal maths behave the way people expect. `decimal` is a distinct opaque type built on an arbitrary-precision coefficient, so it never silently loses precision the way `number` does.

> **`decimal` vs `number`** — Use `decimal` for currency and any calculation where exact base-10 results matter. Use `number` for measurements, scientific values, and anything where a fast binary float is fine. There is no operator overloading: combine decimals with `Decimal.add`, `Decimal.subtract`, `Decimal.multiply`, and `Decimal.divide` rather than `+`/`-`/`*`/`/`. Parsing (`from_string`) and division (`divide`) return `result` because they can fail in everyday use; the other operations return a plain value and only raise a runtime error on misuse or overflow (see `from_number`, `round`, and `multiply` below).

| Function                        | Parameter Types              | Return Type       | Description                                                          |
| ------------------------------- | ---------------------------- | ----------------- | ------------------------------------------------------------------- |
| `Decimal.absolute(d)`           | `(decimal)`                  | `decimal`         | Absolute value                                                      |
| `Decimal.add(a, b)`             | `(decimal, decimal)`         | `decimal`         | Exact sum                                                           |
| `Decimal.compare(a, b)`         | `(decimal, decimal)`         | `integer`         | `-1`, `0`, or `1` (scale-insensitive: `1.5` compares equal to `1.50`) |
| `Decimal.divide(a, b, scale)`   | `(decimal, decimal, integer)` | `result<decimal>` | Quotient rounded (half-up) to `scale` fractional digits; fail on divide-by-zero or negative scale. Use `Decimal.divide_with` to choose the rounding mode |
| `Decimal.divide_typed(a, b, scale)` | `(decimal, decimal, integer)` | `result<decimal, Decimal.Error>` | Like `divide`, but on failure the error is a typed `Decimal.Error` (`DivisionByZero`, `PrecisionExceeded`, or `Overflow`) instead of a string |
| `Decimal.divide_with(a, b, scale, mode)` | `(decimal, decimal, integer, RoundingMode \| string)` | `result<decimal>` | Quotient rounded using `mode` to `scale` fractional digits; fail on divide-by-zero or negative scale |
| `Decimal.equals(a, b)`          | `(decimal, decimal)`         | `boolean`         | Value equality, ignoring trailing-zero scale (`1.5` equals `1.50`)  |
| `Decimal.from_integer(i)`       | `(integer)`                  | `decimal`         | Exact decimal from an integer                                       |
| `Decimal.from_number(n)`        | `(number)`                   | `decimal`         | Shortest exact decimal for a `number`; throws on NaN or infinity    |
| `Decimal.from_string(s)`        | `(string)`                   | `result<decimal>` | Parse decimal text (optional sign, digits, `.`, optional `eNN` exponent); fail on malformed input |
| `Decimal.from_string_typed(s)`  | `(string)`                   | `result<decimal, Decimal.Error>` | Like `from_string`, but on failure the error is a typed `Decimal.Error` (`InvalidFormat`) instead of a string |
| `Decimal.greater_or_equal(a, b)`| `(decimal, decimal)`         | `boolean`         | Whether `a` ≥ `b` (readable wrapper over `compare`)                 |
| `Decimal.greater_than(a, b)`    | `(decimal, decimal)`         | `boolean`         | Whether `a` > `b` (readable wrapper over `compare`)                 |
| `Decimal.is_negative(d)`        | `(decimal)`                  | `boolean`         | Whether `d` is less than zero                                       |
| `Decimal.is_positive(d)`        | `(decimal)`                  | `boolean`         | Whether `d` is greater than zero                                    |
| `Decimal.is_zero(d)`            | `(decimal)`                  | `boolean`         | Whether `d` is zero                                                 |
| `Decimal.less_or_equal(a, b)`   | `(decimal, decimal)`         | `boolean`         | Whether `a` ≤ `b` (readable wrapper over `compare`)                 |
| `Decimal.less_than(a, b)`       | `(decimal, decimal)`         | `boolean`         | Whether `a` < `b` (readable wrapper over `compare`)                 |
| `Decimal.max(a, b)`             | `(decimal, decimal)`         | `decimal`         | Larger of two decimals (scale-insensitive)                         |
| `Decimal.min(a, b)`             | `(decimal, decimal)`         | `decimal`         | Smaller of two decimals (scale-insensitive)                        |
| `Decimal.multiply(a, b)`        | `(decimal, decimal)`         | `decimal`         | Exact product; throws if the result would exceed the maximum decimal size |
| `Decimal.negate(d)`             | `(decimal)`                  | `decimal`         | Additive inverse (`-d`)                                             |
| `Decimal.power(base, exp)`      | `(decimal, integer)`         | `result<decimal>` | Exact repeated multiplication for `exp ≥ 0`; fail on negative exponent, overflow, or an exponent above 1,000,000 |
| `Decimal.product(values)`       | `(array<decimal>)`           | `decimal`         | Exact product of all elements (empty ⇒ `1`); throws on overflow    |
| `Decimal.remainder(a, b)`       | `(decimal, decimal)`         | `result<decimal>` | Exact remainder of `a / b` (sign of `a`); fail on zero divisor     |
| `Decimal.round(d, places, mode)` | `(decimal, integer, RoundingMode \| string)` | `decimal`         | Round to `places` fractional digits using `mode` (a `Decimal.RoundingMode` variant or the equivalent mode string); throws on an unknown mode string |
| `Decimal.scale(d)`              | `(decimal)`                  | `integer`         | Number of stored fractional digits                                 |
| `Decimal.sign(d)`               | `(decimal)`                  | `integer`         | `-1`, `0`, or `1` according to the sign of `d`                      |
| `Decimal.subtract(a, b)`        | `(decimal, decimal)`         | `decimal`         | Exact difference                                                   |
| `Decimal.sum(values)`           | `(array<decimal>)`           | `decimal`         | Exact sum of all elements (empty ⇒ `0`)                            |
| `Decimal.to_integer(d)`         | `(decimal)`                  | `result<integer>` | Exact integer when `d` has no fractional part; else fail (pre-round with `round(d, 0, Down)` to truncate deliberately) |
| `Decimal.to_number(d)`          | `(decimal)`                  | `number`          | Nearest IEEE-754 `number` (may lose precision)                     |
| `Decimal.to_string(d)`          | `(decimal)`                  | `string`          | Canonical text; preserves the value's scale (e.g. `"2.50"`)         |

**Rounding modes** — the `mode` argument to `Decimal.round` and `Decimal.divide_with` is a `Decimal.RoundingMode` choice value (the discoverable, match-exhaustive form) or, for convenience, the equivalent lowercase string. `Decimal.divide` always uses half-up; reach for `Decimal.divide_with` when you need another mode. The seven modes are:

| Variant (`Decimal.RoundingMode`) | String        | Behaviour                                                            |
| -------------------------------- | ------------- | ------------------------------------------------------------------- |
| `HalfUp`                         | `"half_up"`   | Ties round away from zero (`2.5` → `3`, `-2.5` → `-3`)               |
| `HalfDown`                       | `"half_down"` | Ties round toward zero (`2.5` → `2`)                                 |
| `HalfEven`                       | `"half_even"` | Ties round to the even neighbour — banker's rounding (`2.5` → `2`, `3.5` → `4`) |
| `Up`                             | `"up"`        | Always away from zero                                                |
| `Down`                           | `"down"`      | Always toward zero (truncate)                                        |
| `Ceiling`                        | `"ceiling"`   | Toward positive infinity                                             |
| `Floor`                          | `"floor"`     | Toward negative infinity                                             |

Access a variant as `Decimal.RoundingMode.HalfEven`. Because it is a choice type, a `match` over a `Decimal.RoundingMode` is exhaustive and editors autocomplete the variants — a mistyped variant is a compile error, whereas a mistyped mode _string_ is only caught at runtime.

Equality and comparison are **scale-insensitive** — the value `1.5` equals `1.50` — but `Decimal.to_string` and `Decimal.scale` preserve the scale a value was created or computed with, so arithmetic that widens the scale (for example adding `1.50` and `2.25`) keeps the extra digits until you `Decimal.round` it. `Decimal` is always available and needs no imports.

`Decimal.Error` is a choice type with four variants — `InvalidFormat`, `DivisionByZero`, `Overflow`, `PrecisionExceeded` — that categorises _why_ a decimal operation failed, so a program can branch on the cause instead of substring-matching an opaque message. It is surfaced by two opt-in, additive companions: `Decimal.from_string_typed(s)` returns `result<decimal, Decimal.Error>` (an unparseable string is `InvalidFormat`), and `Decimal.divide_typed(a, b, scale)` returns `result<decimal, Decimal.Error>` (a zero divisor is `DivisionByZero`, a negative or too-large `scale` is `PrecisionExceeded`, and an unrepresentable quotient is `Overflow`). This mirrors `FileSystem.read_file_typed` / `FileSystem.IoError`: the plain `Decimal.from_string` and `Decimal.divide` keep their string-error `result<decimal>`.

```luma
match Decimal.from_string_typed(user_input) {
success(d)                            { print("parsed: ${Decimal.to_string(d)}") }
failure(Decimal.Error.InvalidFormat) { print("that isn't a number") }
failure(_other)                       { print("could not read that amount") }
}
```

```luma
@main
function void main() {
    # The classic floating-point surprise, done exactly.
    decimal a = Result.unwrap(Decimal.from_string("0.1"))
    decimal b = Result.unwrap(Decimal.from_string("0.2"))
    decimal sum = Decimal.add(a, b)

    print(Decimal.to_string(sum))          # 0.3
    print(Converter.to_string(Decimal.equals(sum, Result.unwrap(Decimal.from_string("0.3")))))  # true

    # Split a bill and round to cents (half-up).
    result<decimal> share = Decimal.divide(Result.unwrap(Decimal.from_string("100.00")), Decimal.from_integer(3), 2)
    print(Decimal.to_string(Result.unwrap(share)))   # 33.33

    # Choose a rounding mode with the discoverable, type-safe choice.
    result<decimal> truncated = Decimal.divide_with(Result.unwrap(Decimal.from_string("2")), Result.unwrap(Decimal.from_string("3")), 3, Decimal.RoundingMode.Down)
    print(Decimal.to_string(Result.unwrap(truncated)))  # 0.666
}
```

## 11 — Dictionary

Dictionaries preserve insertion order. All reads and writes use string keys.

| Function                          | Parameter Types                                   | Return Type                              | Description                                                                                     |
| --------------------------------- | ------------------------------------------------- | ---------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `Dictionary.has_value(d, v)`      | `(dictionary<T>, T)`                              | `boolean`                                | Whether any value equals `v`                                                                    |
| `Dictionary.all(d, fn)`           | `(dictionary<T>, function(string, T) -> boolean)` | `result<boolean>`                        | `true` if every entry matches; fail if callback throws                                          |
| `Dictionary.any(d, fn)`           | `(dictionary<T>, function(string, T) -> boolean)` | `result<boolean>`                        | `true` if any entry matches; fail if callback throws                                            |
| `Dictionary.count(d, fn)`         | `(dictionary<T>, function(string, T) -> boolean)` | `result<integer>`                        | Count entries matching predicate                                                                |
| `Dictionary.deep_merge(a, b)`     | `(dictionary<T>, dictionary<T>)`                  | `dictionary<T>`                          | Recursive merge; `b` wins on conflicts                                                          |
| `Dictionary.each(d, fn)`          | `(dictionary<T>, function(string, T) -> none)`    | `result<none>`                           | Iterate key–value pairs; fail if callback throws                                                |
| `Dictionary.filter(d, fn)`        | `(dictionary<T>, function(string, T) -> boolean)` | `result<dictionary<T>>`                  | Keep entries where `fn` returns `true`; fail if callback throws                                 |
| `Dictionary.find(d, fn)`          | `(dictionary<T>, function(string, T) -> boolean)` | `result<(string, T)>`                    | First entry where `fn` returns `true`; fail if not found                                        |
| `Dictionary.flip(d)`              | `(dictionary<V>)`                                 | `result<dictionary<string>>`             | Swap keys and values; fail if any value is not a string                                         |
| `Dictionary.from_entries(arr)`    | `(array<(string, T)>)`                            | `dictionary<T>`                          | Create from array of `(key, value)` tuples                                                      |
| `Dictionary.from_arrays(ks, vs)`  | `(array<string>, array<T>)`                       | `result<dictionary<T>>`                  | Pair keys and values positionally; fail on length mismatch; last value wins on duplicate keys   |
| `Dictionary.from_keys(keys, def)` | `(array<string>, T)`                              | `dictionary<T>`                          | Create from key list with default value                                                         |
| `Dictionary.get(d, k)`            | `(dictionary<T>, string)`                         | `result<T>`                              | Safe lookup; fail if key not found                                                              |
| `Dictionary.get_or(d, k, def)`    | `(dictionary<T>, string, T)`                      | `T`                                      | Lookup with default                                                                             |
| `Dictionary.has(d, k)`            | `(dictionary<T>, string)`                         | `boolean`                                | Key membership test                                                                             |
| `Dictionary.invert(d)`            | `(dictionary<string>)`                            | `dictionary<string>`                     | Swap keys and values                                                                            |
| `Dictionary.is_empty(d)`          | `(dictionary<T>)`                                 | `boolean`                                | Whether the dictionary is empty                                                                 |
| `Dictionary.keys(d)`              | `(dictionary<T>)`                                 | `array<string>`                          | Array of keys                                                                                   |
| `Dictionary.length(d)`            | `(dictionary<T>)`                                 | `integer`                                | Number of entries                                                                               |
| `Dictionary.map(d, fn)`           | `(dictionary<T>, function(string, T) -> U)`       | `result<dictionary<U>>`                  | Transform every entry; `fn` receives `(key, value)`, returns new value; fail if callback throws |
| `Dictionary.map_keys(d, fn)`      | `(dictionary<T>, function(string) -> string)`     | `result<dictionary<T>>`                  | Transform every key, keeping values; last write wins on collision; fail if callback throws      |
| `Dictionary.map_values(d, fn)`    | `(dictionary<T>, function(T) -> U)`               | `result<dictionary<U>>`                  | Transform every value; fail if callback throws                                                  |
| `Dictionary.merge(a, b)`          | `(dictionary<T>, dictionary<T>)`                  | `dictionary<T>`                          | Merge; `b` wins on conflicts                                                                    |
| `Dictionary.merge_with(a, b, fn)` | `(dictionary<T>, dictionary<T>, function(T, T) -> T)` | `dictionary<T>`                      | Merge; on a shared key `fn(value_from_a, value_from_b)` resolves the conflict                   |
| `Dictionary.omit(d, keys)`        | `(dictionary<T>, array<string>)`                  | `dictionary<T>`                          | New dictionary excluding entries whose keys are in `keys`                                       |
| `Dictionary.partition(d, fn)`     | `(dictionary<T>, function(string, T) -> boolean)` | `result<(dictionary<T>, dictionary<T>)>` | Split into `(matches, rest)`; `fn` receives `(key, value)`; fail if predicate throws            |
| `Dictionary.pick(d, keys)`        | `(dictionary<T>, array<string>)`                  | `dictionary<T>`                          | New dictionary containing only entries whose keys are in `keys`                                 |
| `Dictionary.reduce(d, init, fn)`  | `(dictionary<T>, U, function(U, string, T) -> U)` | `result<U>`                              | Fold entries; `fn` receives `(accumulator, key, value)`; fail if callback throws                |
| `Dictionary.remove(d, k)`         | `(dictionary<T>, string)`                         | `dictionary<T>`                          | New dictionary without key                                                                      |
| `Dictionary.set(d, k, v)`         | `(dictionary<T>, string, T)`                      | `dictionary<T>`                          | New dictionary with key set                                                                     |
| `Dictionary.to_array(d)`          | `(dictionary<T>)`                                 | `array<KeyValue>`                        | Each element is a record with `.key` (`string`) and `.value` fields                             |
| `Dictionary.to_entries(d)`        | `(dictionary<T>)`                                 | `array<(string, T)>`                     | Each element is a `(key, value)` tuple                                                          |
| `Dictionary.update(d, k, fn)`     | `(dictionary<T>, string, function(optional<T>) -> T)` | `dictionary<T>`                      | New dictionary with `k` set to `fn(current-or-none)` (read-modify-write for one key)            |
| `Dictionary.values(d)`            | `(dictionary<T>)`                                 | `array<T>`                               | Array of values                                                                                 |

`Dictionary.KeyValue` record fields: `key: string`, `value` (the dictionary's value type `V`). It is the element type of `Dictionary.to_array`, so a program can annotate the result as `array<Dictionary.KeyValue>` and read `.key`/`.value` directly. Use `Dictionary.to_entries` instead when you want `(key, value)` tuples rather than records.

## 12 — Encoder

Transform the representation of a string without changing its type (e.g. Base64, URL percent-encoding).

> **Encoder vs Converter** — `Encoder` transforms **string representations** (e.g. binary data → Base64 text). For changing the **type** of a value (e.g. string → integer), use `Converter` instead.

| Function                      | Parameter Types | Return Type      | Description                            |
| ----------------------------- | --------------- | ---------------- | -------------------------------------- |
| `Encoder.decode_base64(s)`    | `(string)`      | `result<string>` | Decode Base64 string                   |
| `Encoder.decode_base64_typed(s)` | `(string)`   | `result<string, Encoder.Error>` | Decode Base64; on failure the error is a typed `Encoder.Error` |
| `Encoder.decode_base64url(s)` | `(string)`      | `result<string>` | Decode URL-safe Base64 string          |
| `Encoder.decode_text(bytes, encoding)` | `(array<integer>, Encoder.Encoding)` | `result<string>` | Decode raw bytes to a string; fail on out-of-range bytes or an invalid sequence |
| `Encoder.decode_text_typed(bytes, encoding)` | `(array<integer>, Encoder.Encoding)` | `result<string, Encoder.Error>` | Decode raw bytes; on failure the error is a typed `Encoder.Error` |
| `Encoder.decode_url(s)`       | `(string)`      | `result<string>` | Decode percent-encoded string          |
| `Encoder.decode_url_typed(s)` | `(string)`      | `result<string, Encoder.Error>` | Decode percent-encoded string; on failure the error is a typed `Encoder.Error` |
| `Encoder.encode_base64(s)`    | `(string)`      | `result<string>` | Encode to Base64                       |
| `Encoder.encode_base64url(s)` | `(string)`      | `result<string>` | Encode to URL-safe Base64 (no padding) |
| `Encoder.encode_text(text, encoding)` | `(string, Encoder.Encoding)` | `result<array<integer>>` | Encode a string to raw bytes; fail if a codepoint is unrepresentable |
| `Encoder.encode_url(s)`       | `(string)`      | `result<string>` | RFC 3986 percent-encoding              |
| `Encoder.is_valid_base64(s)`  | `(string)`      | `boolean`        | Whether `s` is a well-formed Base64 string (non-throwing gate) |
| `Encoder.is_valid_utf8(bytes)`| `(array<integer>)` | `boolean`     | Whether `bytes` are a valid UTF-8 sequence (non-throwing gate) |

`Encoder.Encoding` is a choice type selecting a text encoding for `encode_text` / `decode_text` — `Encoder.Encoding.Utf8`, `Encoder.Encoding.Ascii`, `Encoder.Encoding.Latin1`. Typing the encoding as a closed choice (rather than a bare string) makes a `match` exhaustive and turns a typo into a compile error instead of a runtime "unknown encoding". Bytes are `array<integer>` with each element in 0–255. `encode_text` fails when a codepoint cannot be represented in the target encoding (any non-ASCII codepoint for `Ascii`, any codepoint above U+00FF for `Latin1`); `decode_text` fails on a byte outside 0–255, a non-ASCII byte under `Ascii`, or a malformed UTF-8 sequence under `Utf8`.

```luma
array<integer> bytes = Result.unwrap(Encoder.encode_text("café", Encoder.Encoding.Latin1))
string text = Result.unwrap(Encoder.decode_text(bytes, Encoder.Encoding.Latin1))   # "café"
```

`Encoder.Error` is a choice type with four variants — `InvalidBase64`, `InvalidPercentEncoding`, `InvalidUtf8`, `InvalidAscii` — that categorises _why_ a decode failed, so a program validating user-supplied encoded input can branch on the cause instead of substring-matching an opaque message. It is surfaced by the opt-in `*_typed` companions — `Encoder.decode_base64_typed`, `Encoder.decode_url_typed`, and `Encoder.decode_text_typed` — which return `result<string, Encoder.Error>`: a bad Base64 alphabet or padding is `InvalidBase64`; a malformed percent-escape is `InvalidPercentEncoding`; bytes that are not valid UTF-8 (or outside the byte domain) under `Utf8`/`Latin1` are `InvalidUtf8`; and bytes outside the ASCII range under `Ascii` are `InvalidAscii`. This is additive (mirroring `DateTime.from_iso_string_typed` / `DateTime.ParseError`): the plain `decode_base64`, `decode_url`, and `decode_text` keep their string-error `result<string>`.

## 13 — FileSystem

| Function                                  | Parameter Types           | Return Type             | Description                                           |
| ----------------------------------------- | ------------------------- | ----------------------- | ----------------------------------------------------- |
| `FileSystem.absolute_path(path)`          | `(string)`                | `result<string>`        | Resolve to absolute path                              |
| `FileSystem.append_file(path, data)`      | `(string, string)`        | `result<boolean>`       | Append data to a file                                 |
| `FileSystem.copy(src, dst)`               | `(string, string)`        | `result<boolean>`       | Copy a file                                           |
| `FileSystem.create_directory(path)`       | `(string)`                | `result<boolean>`       | Create a directory                                    |
| `FileSystem.delete(path)`                 | `(string)`                | `result<boolean>`       | Delete a file                                         |
| `FileSystem.delete_directory(path)`       | `(string)`                | `result<boolean>`       | Delete a directory; fail if path is not a directory   |
| `FileSystem.exists(path)`                 | `(string)`                | `result<boolean>`       | Whether the path exists                               |
| `FileSystem.extension(path)`              | `(string)`                | `string`                | File extension (e.g. `".png"`)                        |
| `FileSystem.get_modified_time(path)`      | `(string)`                | `result<number>`        | Last-modified time as a Unix timestamp; fail on error |
| `FileSystem.home_directory()`             | `()`                      | `result<string>`        | User's home directory                                 |
| `FileSystem.is_absolute(path)`            | `(string)`                | `boolean`               | Whether the path is absolute                          |
| `FileSystem.is_directory(path)`           | `(string)`                | `result<boolean>`       | Whether the path is a directory                       |
| `FileSystem.is_file(path)`                | `(string)`                | `result<boolean>`       | Whether the path is a file                            |
| `FileSystem.is_relative(path)`            | `(string)`                | `boolean`               | Whether the path is relative                          |
| `FileSystem.is_symlink(path)`             | `(string)`                | `result<boolean>`       | Whether the path is a symbolic link                   |
| `FileSystem.join(a, b)`                   | `(string, string)`        | `string`                | Join two path components                              |
| `FileSystem.kind(path)`                   | `(string)`                | `result<FileSystem.FileKind>` | Classify a path as `File`, `Directory`, `Symlink`, or `Other`; fail if the path does not exist |
| `FileSystem.list_directories(path)`       | `(string)`                | `result<array<string>>` | List subdirectories                                   |
| `FileSystem.list_files(path)`             | `(string)`                | `result<array<string>>` | List files in a directory                             |
| `FileSystem.list_recursively(path)`       | `(string)`                | `result<array<string>>` | Recursively list all files beneath a directory        |
| `FileSystem.metadata(path)`               | `(string)`                | `result<FileSystem.FileInfo>` | Gather size, modified time, and kind in one call; fail if the path does not exist |
| `FileSystem.name(path)`                   | `(string)`                | `string`                | File name (e.g. `"file.txt"`)                         |
| `FileSystem.normalize(path)`              | `(string)`                | `string`                | Normalise path (e.g. `"a/b/../c"` → `"a/c"`)          |
| `FileSystem.parent(path)`                 | `(string)`                | `string`                | Parent directory                                      |
| `FileSystem.permissions(path)`            | `(string)`                | `result<FileSystem.Permissions>` | Report readable/writable/executable flags and POSIX mode bits; fail if the path does not exist |
| `FileSystem.read_file(path)`              | `(string)`                | `result<string>`        | Read entire file as string                            |
| `FileSystem.read_file_limited(path, max)` | `(string, integer)`       | `result<string>`        | Read file; fail if it exceeds `max` bytes             |
| `FileSystem.read_file_typed(path)`        | `(string)`                | `result<string, FileSystem.IoError>` | Read entire file; on failure the error is a typed `FileSystem.IoError` instead of a string |
| `FileSystem.read_lines(path)`             | `(string)`                | `result<array<string>>` | Read file as array of lines                           |
| `FileSystem.relative(path, base)`         | `(string, string)`        | `string`                | Relative path from `base`                             |
| `FileSystem.rename(old, new)`             | `(string, string)`        | `result<boolean>`       | Rename a file                                         |
| `FileSystem.rename_directory(old, new)`   | `(string, string)`        | `result<boolean>`       | Rename a directory; fail if path is not a directory   |
| `FileSystem.size(path)`                   | `(string)`                | `result<integer>`       | File size in bytes                                    |
| `FileSystem.split_path(path)`             | `(string)`                | `FileSystem.PathParts`  | Decompose a path into parent, name, stem, extension in one call |
| `FileSystem.stem(path)`                   | `(string)`                | `string`                | File name without extension (e.g. `"hello"`)          |
| `FileSystem.write_file(path, data)`       | `(string, string)`        | `result<boolean>`       | Write string to file                                  |
| `FileSystem.write_lines(path, lines)`     | `(string, array<string>)` | `result<boolean>`       | Write array of lines to file                          |

`copy`, `delete`, `delete_directory`, `list_directories`, and `list_files` reject symbolic links and return `failure` to prevent symlink-following attacks.

`FileSystem.FileInfo` record fields: `size` (`integer`, bytes; `0` for directories and other non-regular files), `modified_time` (`number`, fractional seconds since the Unix epoch, matching `get_modified_time`), `is_directory` (`boolean`), `is_file` (`boolean`), `is_symlink` (`boolean`), and `kind` (`FileSystem.FileKind`, the single mutually-exclusive answer described below). `metadata` answers in one call what `size`, `get_modified_time`, `is_directory`, `is_file`, `is_symlink`, and `kind` answer individually; the `is_symlink` flag reflects the path itself (it is not followed) while the size, time, and directory/file flags follow symlinks.

`FileSystem.Permissions` record fields: `readable` (`boolean`), `writable` (`boolean`), `executable` (`boolean`), and `mode` (`integer`, the POSIX mode bits — owner/group/others read/write/execute plus set-uid, set-gid, and sticky). It answers "what may I do with this file?" in one call, returned by `FileSystem.permissions(path)` as a `result<FileSystem.Permissions>`. The three booleans are the beginner-facing answer (read from the owner permission bits), while `mode` is the escape hatch for advanced users who want the raw bits — no numeric magic is forced on beginners. It is cross-platform and never null: on POSIX the flags and `mode` reflect the file's actual permission bits (so a `chmod +x` script reports `executable`), while on Windows the bits are synthesised from the read-only attribute (a read-only file is not `writable`; `readable` and `executable` are reported for every file). `permissions` follows symlinks (it describes the target) and fails only when the path does not exist.

```luma
match FileSystem.permissions("build.sh") {
success(perms) {
    if perms.executable { print("runnable") }
    else { print("not executable") }
}
failure(_e) { print("no such file") }
}
```

`FileSystem.FileKind` is a choice type with four variants — `File`, `Directory`, `Symlink`, `Other` — the single, mutually-exclusive answer to "what kind of thing is this path?". `FileSystem.kind(path)` returns it (and it is also the `kind` field on `FileSystem.FileInfo`), so a `match` is exhaustive and autocompleted instead of a nested `if` chain over the `is_file` / `is_directory` / `is_symlink` booleans. It is classified symlink-first, like `lstat`: a symbolic link is reported as `Symlink` even when its target is a directory or a regular file (so the four variants never overlap), and anything that is none of these — a device, FIFO, or socket — is `Other`. `kind` fails only when the path does not exist.

```luma
match Result.unwrap(FileSystem.kind("README.md")) {
case FileSystem.FileKind.File      { print("a file") }
case FileSystem.FileKind.Directory { print("a directory") }
case FileSystem.FileKind.Symlink   { print("a link") }
case FileSystem.FileKind.Other     { print("something else") }
}
```

`FileSystem.IoError` is a choice type with five variants — `NotFound`, `PermissionDenied`, `AlreadyExists`, `InvalidInput`, `Other` — that categorises _why_ a filesystem operation failed, so a program can branch on the cause instead of substring-matching an opaque message. It is surfaced by `FileSystem.read_file_typed(path)`, which returns `result<string, FileSystem.IoError>`: the value on success is the file contents, and the error on failure is the typed category. A missing path is `NotFound`; an OS permission refusal is `PermissionDenied`; reading a directory or a path rejected by sandbox validation is `InvalidInput`; anything else is `Other`. This is an opt-in, additive prototype — the plain `FileSystem.read_file` (and every other `FileSystem` function) keeps its string-error `result<T>` — offered on a single function so typed I/O errors can be evaluated before deciding whether to generalise the pattern across the standard library.

```luma
match FileSystem.read_file_typed("config.txt") {
success(contents) { print(contents) }
failure(FileSystem.IoError.NotFound) { print("no config yet, using defaults") }
failure(FileSystem.IoError.PermissionDenied) { print("cannot read config: permission denied") }
failure(_other) { print("could not read config") }
}
```

`FileSystem.PathParts` record fields: `parent` (`string`), `name` (`string`), `stem` (`string`), `extension` (`string`). `split_path` answers in one typed call what `parent`, `name`, `stem`, and `extension` answer individually. It is pure string manipulation — no I/O — so it needs no filesystem access, works in sandbox mode, and never fails.

> **Security note** — `append_file`, `read_file`, `read_lines`, `write_file`, and `write_lines` validate that the resolved path stays within the current working directory, which blocks cross-directory symlink traversal (e.g. a symlink pointing to `/etc/passwd` is rejected). However, a symbolic link that points to another file **within** the working directory is followed transparently. If your program accepts a user-supplied file path, validate that the resolved path refers to the expected file before reading or writing.

## 14 — Graph

A weighted graph supporting directed and undirected edges. Vertices are identified by strings. All operations are immutable.

| Function                                 | Parameter Types                   | Return Type                              | Description                                                 |
| ---------------------------------------- | --------------------------------- | ---------------------------------------- | ----------------------------------------------------------- |
| `Graph.add_edge(g, from, to)`            | `(graph, string, string)`         | `graph`                                  | Add an edge with weight 1                                   |
| `Graph.add_edge(g, from, to, w)`         | `(graph, string, string, number)` | `graph`                                  | Add a weighted edge                                         |
| `Graph.add_vertex(g, v)`                 | `(graph, string)`                 | `graph`                                  | Add a vertex                                                |
| `Graph.all_pairs_shortest_paths(g)`      | `(graph)`                         | `result<dictionary<dictionary<number>>>` | Shortest-path distance between every pair of vertices       |
| `Graph.breadth_first_search(g, start)`   | `(graph, string)`                 | `result<array<string>>`                  | BFS traversal from start vertex                             |
| `Graph.connected_components(g)`          | `(graph)`                         | `result<array<array<string>>>`           | Connected components; undirected only                       |
| `Graph.degree(g, v)`                     | `(graph, string)`                 | `result<integer>`                        | Degree of vertex; fail if not found                         |
| `Graph.depth_first_search(g, start)`     | `(graph, string)`                 | `result<array<string>>`                  | DFS traversal from start vertex                             |
| `Graph.directed()`                       | `()`                              | `graph`                                  | Create an empty directed graph                              |
| `Graph.edge_count(g)`                    | `(graph)`                         | `integer`                                | Number of edges                                             |
| `Graph.edge_weight(g, from, to)`         | `(graph, string, string)`         | `result<number>`                         | Weight of edge; fail if not found                           |
| `Graph.edges(g)`                         | `(graph)`                         | `array<Graph.Edge>`                      | Every edge as a typed `Graph.Edge` record, in deterministic order |
| `Graph.has_cycle(g)`                     | `(graph)`                         | `boolean`                                | Whether the graph contains a cycle                          |
| `Graph.has_edge(g, from, to)`            | `(graph, string, string)`         | `boolean`                                | Whether edge exists                                         |
| `Graph.has_vertex(g, v)`                 | `(graph, string)`                 | `boolean`                                | Whether vertex exists                                       |
| `Graph.is_directed(g)`                   | `(graph)`                         | `boolean`                                | Whether the graph is directed                               |
| `Graph.minimum_spanning_tree(g)`         | `(graph)`                         | `result<graph>`                          | Minimum spanning tree; undirected graphs only               |
| `Graph.neighbors(g, v)`                  | `(graph, string)`                 | `result<array<string>>`                  | Adjacent vertices; fail if not found                        |
| `Graph.remove_edge(g, from, to)`         | `(graph, string, string)`         | `graph`                                  | Remove edge                                                 |
| `Graph.remove_vertex(g, v)`              | `(graph, string)`                 | `graph`                                  | Remove vertex and its edges                                 |
| `Graph.shortest_path(g, from, to)`       | `(graph, string, string)`         | `result<array<string>>`                  | Shortest path between vertices                              |
| `Graph.shortest_path_detailed(g, from, to)` | `(graph, string, string)`      | `result<Graph.Path>`                     | Shortest path as a `{ vertices, cost }` record             |
| `Graph.strongly_connected_components(g)` | `(graph)`                         | `result<array<array<string>>>`           | Groups of mutually reachable vertices; directed graphs only |
| `Graph.to_adjacency_list(g)`             | `(graph)`                         | `dictionary<array<string>>`              | Convert to adjacency list                                   |
| `Graph.topological_sort(g)`              | `(graph)`                         | `result<array<string>>`                  | Topological ordering; directed only; fail if cycle          |
| `Graph.undirected()`                     | `()`                              | `graph`                                  | Create an empty undirected graph                            |
| `Graph.vertex_count(g)`                  | `(graph)`                         | `integer`                                | Number of vertices                                          |
| `Graph.vertices(g)`                      | `(graph)`                         | `array<string>`                          | All vertex labels                                           |

`Graph.Edge` record fields: `from` (`string`), `to` (`string`), `weight` (`number`). `Graph.edges(g)` returns every edge as one of these records so you can enumerate a graph's structure directly instead of iterating vertices, calling `Graph.neighbors`, and re-querying `Graph.edge_weight` for each pair. The order is deterministic: vertices are visited in sorted order, then each vertex's out-edges in sorted order. An undirected edge is emitted once, oriented so `from <= to`, so `Array.length(Graph.edges(g)) == Graph.edge_count(g)`.

```luma
for edge in Graph.edges(g) {
    print("${edge.from} -> ${edge.to} (${edge.weight})")
}
```

`Graph.Path` record fields: `vertices` (`array<string>`), `cost` (`number`). `Graph.shortest_path_detailed(g, from, to)` returns both the route and its total weight in one call, so you no longer have to re-walk a `Graph.shortest_path` result calling `Graph.edge_weight` for each consecutive pair. It is additive — `Graph.shortest_path` is unchanged — and fails (like `shortest_path`) on a missing vertex, a negative edge weight, or when no path exists.

## 15 — Solaris and GraphicalUi

Luma's GUI story is two layers under one section:

- **`Solaris`** — the beginner-first authoring surface (§14.1). A built-in
  module (no `include`) following the Model-View-Update pattern: typed `record`
  models, `choice` messages, an immutable `View` tree, fluent `|>` modifiers, and
  semantic design tokens. Full guide: [Solaris Guide](Luma_Solaris_Guide.md).
- **`GraphicalUi`** — the low-level webview engine beneath the surface (§14.2):
  widgets, layouts, charts, theming, commands, subscriptions, and the headless
  test harness. Full guide: [GraphicalUi Guide](Luma_GraphicalUi_Guide.md).

**Platform support (both layers).** Requires platform webview support: WebView2 on Windows, WebKit on macOS, WebKitGTK on Linux. When the interpreter is built without webview support (`LUMA_HAS_WEBVIEW` not defined), the GUI functions throw a descriptive error at runtime.

### 14.1 — Solaris (beginner surface)

The surface ships built in: the moment a program refers to `Solaris`, its
design tokens, the `View` record, and the `Solaris` functions are available.
The `Model` is a `record`, each `Message` is a `choice` type, and `view` is a
pure `(Model) -> View`. `update` is a pure `(Model, Msg) -> Model`, or
`(Model, Msg) -> any` when a branch returns a `(model, command)` pair (see
_Effects_ below).

**Global design tokens** (top-level `choice` types — written unqualified, e.g. `Spacing.L`):

| Token | Variants |
|---|---|
| `Emphasis` | `Normal`, `Primary`, `Secondary`, `Success`, `Warning`, `Danger`, `Muted` |
| `TextScale` | `Caption`, `Body`, `Large`, `Heading`, `Title` |
| `Weight` | `Regular`, `Bold` |
| `Spacing` | `None`, `XS`, `S`, `M`, `L`, `XL` |
| `Align` | `Start`, `Center`, `End`, `Stretch` |
| `Justify` | `Start`, `Center`, `End`, `SpaceBetween`, `SpaceAround` |
| `Length` | `Shrink`, `Fill`, `Fixed(number value)`, `FillPortion(integer weight)` |
| `Radius` | `None`, `Small`, `Medium`, `Large`, `Full` |
| `Scheme` | `Light`, `Dark`, `Auto` |
| `Color` | `Primary`, `Success`, `Warning`, `Danger`, `Muted`, `Custom(string hex)` |

`View` is a global `record` describing one piece of UI; `view` returns a tree of them.

**Constructors** (each returns a `View`; containers take `array<View>`):

| Function | Signature | Purpose |
|---|---|---|
| `Solaris.text` | `(string content) -> View` | A run of body text |
| `Solaris.heading` | `(string content) -> View` | A prominent section title |
| `Solaris.badge` | `(string text) -> View` | A small status pill |
| `Solaris.icon` | `(string name) -> View` | A named glyph (size with `icon_size`) |
| `Solaris.spinner` | `(string label) -> View` | An indeterminate busy indicator |
| `Solaris.divider` | `() -> View` | A horizontal rule |
| `Solaris.button` | `(string label) -> View` | A clickable action (pair with `on_click`) |
| `Solaris.text_field` | `(string value) -> View` | Single-line input (pair with `on_change`) |
| `Solaris.text_area` | `(string value) -> View` | Multi-line input (pair with `on_change`) |
| `Solaris.checkbox` | `(string label) -> View` | A boolean toggle (pair with `checked`/`on_toggle`) |
| `Solaris.switch` | `(string label, boolean state) -> View` | An on/off switch (pair with `on_toggle`) |
| `Solaris.radio` | `(array<string> options, string chosen) -> View` | Single choice (pair with `on_select`) |
| `Solaris.dropdown` | `(array<string> options, string chosen) -> View` | Compact single choice (pair with `on_select`) |
| `Solaris.slider` | `(number value, number min, number max) -> View` | Numeric range (pair with `on_slide`) |
| `Solaris.date_picker` | `(string value) -> View` | Date input (pair with `on_change`) |
| `Solaris.spacer` | `() -> View` | Flexible empty space |
| `Solaris.column` | `(array<View> children) -> View` | Stacks children vertically |
| `Solaris.row` | `(array<View> children) -> View` | Arranges children horizontally |
| `Solaris.grid` | `(integer columns, array<View> children) -> View` | Fixed-column grid |
| `Solaris.z_stack` | `(array<View> children) -> View` | Layers children on top of each other |
| `Solaris.scroll` | `(array<View> children) -> View` | A vertically scrollable region |
| `Solaris.card` | `(array<View> children) -> View` | A padded, elevated group |
| `Solaris.list` | `(array<View> items) -> View` | A vertical list |
| `Solaris.panel` | `(string title, array<View> children) -> View` | A titled, bordered group |
| `Solaris.table` | `(array<string> headers, array<array<string>> rows) -> View` | A read-only data grid |
| `Solaris.progress` | `(number value, number max) -> View` | A determinate progress bar |
| `Solaris.image` | `(string source) -> View` | A picture from a path or URL |
| `Solaris.line_chart` | `(array<string> labels, array<number> values) -> View` | A line chart plotting `values` over `labels` |
| `Solaris.bar_chart` | `(array<string> labels, array<number> values) -> View` | A vertical bar chart, one bar per label |
| `Solaris.pie_chart` | `(array<string> labels, array<number> values) -> View` | A pie chart, one slice per label |
| `Solaris.tabs` | `(array<string> labels, integer active, array<View> panels) -> View` | Panel switcher (pair with `on_tab`) |
| `Solaris.menu` | `(string label, array<string> items) -> View` | In-page dropdown menu (pair with `on_select`) |
| `Solaris.dialog` | `(string title, boolean open, array<View> children) -> View` | A modal (pair with `on_close`) |
| `Solaris.toast` | `(string message) -> View` | A transient notification banner |
| `Solaris.sidebar` | `(array<View> children) -> View` | A fixed-width navigation rail |
| `Solaris.app_shell` | `(View side, View content) -> View` | Full-height sidebar-plus-content layout |

**Modifiers** (take the piped `View` first, return a new `View`; chain with `|>`):

| Function | Effect |
|---|---|
| `on_click(View, any msg)` | Send `msg` when a button is clicked |
| `on_change(View, function(string) -> any)` | Handle each edit of a text field/area or date |
| `on_toggle(View, function(boolean) -> any)` | Handle a checkbox/switch toggle |
| `on_select(View, function(string) -> any)` | Handle a radio/dropdown/menu choice |
| `on_slide(View, function(number) -> any)` | Handle a slider change |
| `on_tab(View, function(integer) -> any)` | Handle a tab switch |
| `on_close(View, any msg)` | Send `msg` when a dialog is dismissed |
| `level(View, integer)` | Heading level (1–6) |
| `size(View, TextScale)` · `weight(View, Weight)` · `bold(View)` | Typography |
| `primary` · `secondary` · `danger` · `muted` · `emphasis(View, Emphasis)` | Semantic emphasis |
| `gap(View, Spacing)` · `padding(View, Spacing)` | Container spacing |
| `width(View, Length)` · `height(View, Length)` | Sizing |
| `align(View, Align)` · `justify(View, Justify)` · `center(View)` | Layout alignment |
| `rounded(View, Radius)` | Corner rounding |
| `checked(View, boolean)` · `placeholder(View, string)` · `icon_size(View, integer)` | Input/glyph state |
| `key(View, string)` · `label(View, string)` | Identity (keyed reconciliation) and accessible label |

**Application and configuration** (build a config with `app`, refine it with `|>`, then `run`):

| Function | Signature | Description |
|---|---|---|
| `Solaris.app` | `(string title, any model, function(any, any) -> any update, function(any) -> View view) -> dictionary` | Build a runnable app config |
| `Solaris.run` | `(dictionary config) -> void` | Open the window and run the MVU loop |
| `Solaris.render` | `(View node) -> widget` | Reconcile a view tree into a root widget |
| `Solaris.window` · `min_size` · `max_size` | `(dictionary, integer w, integer h) -> dictionary` | Window size and bounds |
| `Solaris.resizable` | `(dictionary, boolean) -> dictionary` | Whether the window may be resized |
| `Solaris.fullscreen` · `devtools` | `(dictionary) -> dictionary` | Start fullscreen / open the inspector |
| `Solaris.accent` · `font` | `(dictionary, string) -> dictionary` | Accent colour / UI font |
| `Solaris.accent_color` | `(dictionary, Color) -> dictionary` | Accent from a typed `Color` (semantic token or `Solaris.hex`) |
| `Solaris.hex` | `(string) -> Color` | Build a `Color.Custom` from any CSS colour string |
| `Solaris.color_value` | `(Color) -> string` | Resolve a `Color` to its CSS string (for any `theme` override) |
| `Solaris.color_scheme` | `(dictionary, Scheme) -> dictionary` | Pin light/dark or follow the OS |
| `Solaris.theme` | `(dictionary, dictionary overrides) -> dictionary` | Advanced token overrides |
| `Solaris.persist` | `(dictionary, string path) -> dictionary` | Save/restore the model across runs |
| `Solaris.on_error` | `(dictionary, function(string) -> View) -> dictionary` | Custom error view (keep last good frame) |
| `Solaris.subscribe` | `(dictionary, function(any) -> array<any>) -> dictionary` | Register timers/keyboard subscriptions |
| `Solaris.on_start` | `(dictionary, any command) -> dictionary` | Run a command once at launch |

**Effects** (commands returned from `update`/`on_start`, and subscriptions from `subscribe`):

| Function | Signature | Effect |
|---|---|---|
| `Solaris.no_command` | `() -> any` | An explicit empty effect |
| `Solaris.with_command` | `(any model, any command) -> any` | Return the next model plus a command |
| `Solaris.batch` | `(array<any> commands) -> any` | Run several commands at once |
| `Solaris.after` | `(integer ms, any msg) -> any` | Send `msg` once after a delay |
| `Solaris.fetch` | `(string url, function(any) -> any) -> any` | HTTP GET, then map the reply to a `Msg` |
| `Solaris.notify` | `(string title, string body) -> any` | An OS desktop notification |
| `Solaris.set_scheme` | `(Scheme) -> any` | Switch light/dark/auto at runtime |
| `Solaris.every` | `(string id, integer ms, any msg) -> any` | A repeating-timer subscription |
| `Solaris.on_key_press` | `(string id, string key, any msg) -> any` | A global keyboard-shortcut subscription |

**Testing.** Because `update`/`view` are pure, test logic by calling `update` directly. To test wiring end to end, use the engine's headless harness — `GraphicalUi.test_click`, `GraphicalUi.test_input`, `GraphicalUi.test_render`, `GraphicalUi.test_count`, etc. (see §14.2) — which drive the same reconciler without opening a window. Set `LUMA_GUI_HEADLESS=1` to render once and exit.

### 14.2 — GraphicalUi (low-level engine)

Declarative graphical user interface engine following the Elm architecture (Model–Update–View). Build native-window GUI applications with widgets, layouts, charts, and theming — rendered via an embedded HTML/CSS/JS webview. This is the engine the `Solaris` surface (§14.1) compiles down to; reach for it directly only for advanced scenarios the surface does not yet expose.

> **Full guide.** This section summarises the engine's API. For comprehensive usage — widgets, layouts, charts, theming, routing, animation, and accessibility — see the [GraphicalUi Guide](Luma_GraphicalUi_Guide.md).

**Architecture.** A GraphicalUi application has three parts:

- `model` — the initial application state (any value)
- `view` — a function `(model) -> widget` that builds the widget tree
- `update` — a function `(model, msg) -> model` that produces a new model from an event

**Callback returns.** Event callbacks — button clicks, input changes, keyboard and timer subscriptions, and command results — return either a **new model**, applied directly, or a **message** (a string) that is routed through `update(model, msg)`. This lets the Elm message pattern (a button `() -> "inc"`) and direct model updates (a reset `() -> 0`) coexist. If the model is itself a string, return models directly and omit `update`, because a string return is always interpreted as a message.

**Widgets are dictionaries.** Every widget, layout, chart, style, command, and helper function in this module returns a value typed `widget` — the module's universal value type. At runtime a `widget` is a plain dictionary, so the type checker treats `widget` and `dictionary` as interchangeable: a `widget` result may be stored in a `dictionary` variable, and a `dictionary` may be passed wherever a `widget` is expected. This is why style and layout helpers such as `GraphicalUi.center()`, `GraphicalUi.merge_styles(...)`, and `GraphicalUi.responsive(...)` are typed `widget` yet produce plain style dictionaries you pass directly as a `style` argument, and why data helpers such as `GraphicalUi.classify_device(...)` and `GraphicalUi.undo(...)` return inspectable `{ ... }` dictionaries.

Use the provided constants as config dictionary keys instead of raw strings.

```luma
# ── App config key constants ──
GraphicalUi.MODEL   # string — "model"
GraphicalUi.VIEW    # string — "view"
GraphicalUi.UPDATE  # string — "update"
GraphicalUi.TITLE   # string — "title"
GraphicalUi.THEME   # string — "theme"

# ── Subscription config key constant ──
GraphicalUi.SUBSCRIBE  # string — "subscribe"

# ── Init config key constant ──
GraphicalUi.INIT       # string — "init"

# ── Alert severity constants ──
GraphicalUi.INFO     # string — "info"
GraphicalUi.WARNING  # string — "warning"
GraphicalUi.ERROR    # string — "error"
GraphicalUi.SUCCESS  # string — "success"

# ── Button variant constants (button "variant" style key) ──
GraphicalUi.PRIMARY    # string — "primary"
GraphicalUi.SECONDARY  # string — "secondary"
GraphicalUi.GHOST      # string — "ghost"
GraphicalUi.DANGER     # string — "danger"

# ── CSS variable reference constants ──
GraphicalUi.VAR_PRIMARY        # string — "var(--gui-primary)"
GraphicalUi.VAR_PRIMARY_HOVER  # string — "var(--gui-primary-hover)"
GraphicalUi.VAR_BG             # string — "var(--gui-bg)"
GraphicalUi.VAR_FG             # string — "var(--gui-fg)"
GraphicalUi.VAR_BORDER         # string — "var(--gui-border)"
GraphicalUi.VAR_INPUT_BG       # string — "var(--gui-input-bg)"
GraphicalUi.VAR_INPUT_BORDER   # string — "var(--gui-input-border)"
GraphicalUi.VAR_INPUT_FOCUS    # string — "var(--gui-input-focus)"
GraphicalUi.VAR_RADIUS         # string — "var(--gui-radius)"
GraphicalUi.VAR_SHADOW         # string — "var(--gui-shadow)"
GraphicalUi.VAR_SHADOW_NONE    # string — "none"                   (Solaris Shadow.None)
GraphicalUi.VAR_SHADOW_SM      # string — "var(--gui-elevation-1)" (Solaris Shadow.Small)
GraphicalUi.VAR_SHADOW_MD      # string — "var(--gui-elevation-3)" (Solaris Shadow.Medium)
GraphicalUi.VAR_SHADOW_LG      # string — "var(--gui-elevation-6)" (Solaris Shadow.Large)
GraphicalUi.VAR_GAP            # string — "var(--gui-gap)"
GraphicalUi.VAR_DISABLED_BG    # string — "var(--gui-disabled-bg)"
GraphicalUi.VAR_DISABLED_FG    # string — "var(--gui-disabled-fg)"
GraphicalUi.VAR_SUCCESS        # string — "var(--gui-success)"
GraphicalUi.VAR_WARNING        # string — "var(--gui-warning)"
GraphicalUi.VAR_ERROR          # string — "var(--gui-error)"
GraphicalUi.VAR_FONT           # string — "var(--gui-font)"

# ── Spacing scale constants (4px base) ──
GraphicalUi.VAR_SPACE_XS       # string — "var(--gui-space-xs)"  (0.25rem / 4px)
GraphicalUi.VAR_SPACE_SM       # string — "var(--gui-space-sm)"  (0.5rem / 8px)
GraphicalUi.VAR_SPACE_MD       # string — "var(--gui-space-md)"  (1rem / 16px)
GraphicalUi.VAR_SPACE_LG       # string — "var(--gui-space-lg)"  (1.5rem / 24px)
GraphicalUi.VAR_SPACE_XL       # string — "var(--gui-space-xl)"  (2rem / 32px)

# ── Radius scale constants (named corners) ──
GraphicalUi.VAR_RADIUS_NONE    # string — "var(--gui-radius-none)"  (0, square)
GraphicalUi.VAR_RADIUS_SM      # string — "var(--gui-radius-sm)"    (0.25rem / 4px)
GraphicalUi.VAR_RADIUS_MD      # string — "var(--gui-radius-md)"    (0.5rem / 8px)
GraphicalUi.VAR_RADIUS_LG      # string — "var(--gui-radius-lg)"    (1rem / 16px)
GraphicalUi.VAR_RADIUS_FULL    # string — "var(--gui-radius-full)"  (999px, pill)

# ── Type scale constants ──
GraphicalUi.VAR_TEXT_XS        # string — "var(--gui-font-size-xs)"  (0.75rem / 12px)
GraphicalUi.VAR_TEXT_SM        # string — "var(--gui-font-size-sm)"  (0.875rem / 14px)
GraphicalUi.VAR_TEXT_MD        # string — "var(--gui-font-size-md)"  (1rem / 16px, base)
GraphicalUi.VAR_TEXT_LG        # string — "var(--gui-font-size-lg)"  (1.25rem / 20px)
GraphicalUi.VAR_TEXT_XL        # string — "var(--gui-font-size-xl)"  (1.5rem / 24px)
GraphicalUi.VAR_TEXT_2XL       # string — "var(--gui-font-size-2xl)" (2rem / 32px)

# ── Secondary / de-emphasised text colour ──
GraphicalUi.VAR_TEXT_MUTED     # string — "var(--gui-text-muted)"  (captions, hints, metadata)

# ── Reading measure (line-length cap) ──
GraphicalUi.VAR_MEASURE        # string — "var(--gui-measure)"  (65ch)
```

The `VAR_*` constants return CSS `var()` references that resolve to the current theme values at render time. Use them in style dictionaries to keep widgets theme-aware.

### App Lifecycle

| Function                        | Parameter Types | Return Type  | Description                                     |
| ------------------------------- | --------------- | ------------ | ----------------------------------------------- |
| `GraphicalUi.app(config)`       | `(dictionary)`  | `none`       | Open a window and run the Elm-architecture loop |
| `GraphicalUi.style(properties)` | `(dictionary)`  | `widget`     | Identity helper — validates and returns a style |

The `config` dictionary accepts these keys:

| Key           | Type                              | Default              | Description                                              |
| ------------- | --------------------------------- | -------------------- | -------------------------------------------------------- |
| `"height"`    | integer                           | `600`                | Window height in pixels                                  |
| `"max_height"` / `"max_width"` | integer          | optional             | Maximum window size (resizable windows only)             |
| `"min_height"` / `"min_width"` | integer          | optional             | Minimum window size (resizable windows only)             |
| `"model"`     | any                               | `null`               | Initial application state                                |
| `"persist"`   | string (file path)                | optional             | Save the model to a JSON file on exit and restore it on the next launch |
| `"resizable"` | boolean                           | `true`               | Allow window resize; `false` fixes the size              |
| `"fullscreen"` / `"maximized"` | boolean                           | `false`              | Start the window maximised / full screen                 |
| `"allow_remote_images"` | boolean                           | `false`              | Load remote `http(s)` `image()`/`avatar()` sources; off by default (only `data:`/`blob:` and relative URLs load) |
| `"subscribe"` | `func(model) -> array<sub>`       | optional             | Subscription function (see Subscriptions below)          |
| `"theme"`     | dictionary                        | optional             | Theme overrides (see Theme below)                        |
| `"title"`     | string                            | `"Luma Application"` | Window title                                             |
| `"init"`      | `func(model) -> model\|pair`      | optional             | Initialisation function; may return a command pair       |
| `"update"`    | `func(model, msg) -> model\|pair` | optional             | Update function; may return model or `with_command` pair |
| `"on_error"`  | `func(string) -> widget`          | optional             | Custom error view shown when `view`/`update` raises      |
| `"view"`      | `func(model) -> widget`           | _required_           | View function returning a widget tree                    |
| `"width"`     | integer                           | `800`                | Window width in pixels                                   |

> **Headless testing.** Setting the environment variable `LUMA_GUI_HEADLESS=1` makes `GraphicalUi.app` run the application's `init` → `view` → `subscribe` lifecycle once and then return, without creating a window. This lets GUI programs be exercised in automated tests and CI. The optional `LUMA_GUI_MESSAGES` variable (comma-separated, for example `inc,dec`) drives scripted `update` messages after the initial render. When these variables are unset, `app` opens a window and runs normally.

### Interaction Testing

For finer-grained verification than the headless lifecycle above, the `GraphicalUi.test_*` functions drive an application **without opening a window** by rendering its view, simulating a real interaction, and returning the resulting model. Each call is stateless and takes the same `config` dictionary that `GraphicalUi.app` consumes, so examples and tests can assert that user actions produce the expected state. Widgets are located by their visible text (label, placeholder, value, name, title) or by a unique style `id`. When several widgets share a locator, a 0-based `index` selects which one to act on (use `test_count` to count them); alternatively, give a widget a style `id` to address it by identity.

| Function                                                       | Parameter Types                                | Return Type  | Description                                                                                                                                       |
| -------------------------------------------------------------- | ---------------------------------------------- | ------------ | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `GraphicalUi.test_init(config)`                                | `(dictionary)`                                 | `any`        | Run `init` (or fall back to the configured model) and return the initial model                                                                   |
| `GraphicalUi.test_render(config, model)`                       | `(dictionary, any)`                            | `dictionary` | Render `view(model)` and return the widget tree for structural assertions                                                                        |
| `GraphicalUi.test_count(config, model, locator)`               | `(dictionary, any, string)`                    | `integer`    | Count the widgets matching `locator` (the same matching `test_find` uses) for disambiguating duplicates before addressing one by index           |
| `GraphicalUi.test_find(config, model, locator, index?)`        | `(dictionary, any, string, integer?)`          | `dictionary` | Return the matching widget's rendered dictionary so tests can assert on its serialized state (value, type, `_element_id`, …) without acting on it |
| `GraphicalUi.test_click(config, model, locator, index?)`       | `(dictionary, any, string, integer?)`          | `any`        | Click the `index`-th widget matching `locator` (default 0) and return the new model                                                              |
| `GraphicalUi.test_input(config, model, locator, value, index?)`| `(dictionary, any, string, any, integer?)`     | `any`        | Send `value` to the widget matching `locator` (text input, checkbox, toggle, slider, dropdown) and return the new model                          |
| `GraphicalUi.test_event(config, model, locator, event, args?, index?)` | `(dictionary, any, string, string, array<any>?, integer?)` | `any` | Fire `event` on the matching widget, forwarding optional `args`, and return the new model                                                        |
| `GraphicalUi.test_key(config, model, key)`                     | `(dictionary, any, string)`                    | `any`        | Deliver `key` to the application's keyboard subscriptions (`on_key`) and return the new model                                                     |
| `GraphicalUi.test_message(config, model, message)`             | `(dictionary, any, any)`                       | `any`        | Deliver `message` to `update(model, message)` and return the new model                                                                           |

`test_click`, `test_input`, and `test_event` fire the widget's own callback and resolve its return value exactly as a live app does — a returned message (string) is routed through `update`, while a returned model or `with_command` pair is applied directly; command side effects are inert without a window, so the model update is deterministic. `test_event` covers the secondary handlers a widget exposes: valid `event` names are `click`, `change`, `double_click`, `right_click`, `mouse_enter`, `mouse_leave`, `mouse_move` (pointer handlers declared in the style dictionary), plus `close` (a `dialog`'s `on_close`) and `clear` (a `search_input`'s `on_clear`). The optional `args` array is forwarded to the handler so handlers of any arity (e.g. `on_mouse_move(x, y)`) can be tested. `test_key` drives the keyboard path end-to-end (subscribe → matching `on_key` callback → `update`), so keyboard shortcuts are verified through the same dispatch the live runtime uses. `test_message` exercises the `update` function directly, and `test_find` returns rendered widget state for assertions. A locator that matches no widget (or an out-of-range `index`) raises a runtime error.

### Style Parameter

Most widget functions accept an optional `style?` dictionary as their last parameter. Keys are CSS property names written with underscores (converted to hyphens at render time). Values are strings.

### Basic Widgets

| Function                                       | Parameter Types                   | Return Type | Description                              |
| ---------------------------------------------- | --------------------------------- | ----------- | ---------------------------------------- |
| `GraphicalUi.heading(text, level?, style?)`    | `(string, integer?, dictionary?)` | `widget`    | Heading (level 1–6, default 1)           |
| `GraphicalUi.image(source, style?)`            | `(string, dictionary?)`           | `widget`    | Image from URL or data URI               |
| `GraphicalUi.label(text, style?)`              | `(string, dictionary?)`           | `widget`    | Static text span                         |
| `GraphicalUi.progress(value, max, style?)`     | `(number, number, dictionary?)`   | `widget`    | Progress bar                             |
| `GraphicalUi.progress_bar(value, max, style?)` | `(number, number, dictionary?)`   | `widget`    | Progress bar (alias for `progress`)      |
| `GraphicalUi.spinner(label?, style?)`          | `(string?, dictionary?)`          | `widget`    | Indeterminate busy indicator (default label `"Loading…"`) |
| `GraphicalUi.separator(style?)`                | `(dictionary?)`                   | `widget`    | Horizontal rule                          |
| `GraphicalUi.spacer(height?, style?)`          | `(integer?, dictionary?)`         | `widget`    | Vertical spacing (default 16 px)         |
| `GraphicalUi.horizontal_spacer(width?)`        | `(integer?)`                      | `widget`    | Horizontal spacing (default 16 px)       |
| `GraphicalUi.flexible_space(style?)`           | `(dictionary?)`                   | `widget`    | Fills remaining space in a row or column |

### Interactive Widgets

| Function                                                         | Parameter Types                                   | Return Type | Description                               |
| ---------------------------------------------------------------- | ------------------------------------------------- | ----------- | ----------------------------------------- |
| `GraphicalUi.button(label, on_click, style?)`                    | `(string, function, dictionary?)`                 | `widget`    | Clickable button (`"variant"` style key: `primary`/`secondary`/`ghost`/`danger`) |
| `GraphicalUi.checkbox(label, checked, on_toggle, style?)`        | `(string, boolean, function, dictionary?)`        | `widget`    | Checkbox toggle                           |
| `GraphicalUi.color_picker(value, on_change)`                     | `(string, func(string) -> any)`                   | `widget`    | Colour picker input                       |
| `GraphicalUi.date_picker(value, on_change)`                      | `(string, func(string) -> any)`                   | `widget`    | Date picker input                         |
| `GraphicalUi.dropdown(options, value, on_select, style?)`        | `(array<string>, string, function, dictionary?)`  | `widget`    | Select dropdown                           |
| `GraphicalUi.file_input(on_select, accept?)`                     | `(func(string) -> any, string?)`                  | `widget`    | File chooser; `accept` filters file types |
| `GraphicalUi.radio_group(options, selected, on_select, style?)`  | `(array<string>, string, function, dictionary?)`  | `widget`    | Radio button group                        |
| `GraphicalUi.slider(value, min, max, on_change, style?)`         | `(number, number, number, function, dictionary?)` | `widget`    | Range slider                              |
| `GraphicalUi.text_area(value, on_change, on_commit?, style?)`    | `(string, function, function?, dictionary?)`      | `widget`    | Multi-line text input (optional `on_commit` fires on blur / Ctrl+Enter) |
| `GraphicalUi.text_input(value, on_change, placeholder?, on_commit?, style?)` | `(string, function, string?, function?, dictionary?)` | `widget`    | Single-line text input (optional `on_commit` fires on blur / Enter) |
| `GraphicalUi.time_picker(value, on_change)`                      | `(string, func(string) -> any)`                   | `widget`    | Time picker input                         |
| `GraphicalUi.toggle(label, checked, on_toggle, style?)`          | `(string, boolean, function, dictionary?)`        | `widget`    | Toggle switch                             |

### Layout Containers

Layout containers accept `array<widget>` for their `children` parameter. A `result<array<widget>>` is also accepted and automatically unwrapped, so piped `Array.map` / `Array.filter` results can be passed directly.

| Function                                                        | Parameter Types                                                  | Return Type | Description                          |
| --------------------------------------------------------------- | ---------------------------------------------------------------- | ----------- | ------------------------------------ |
| `GraphicalUi.column(children, style?)`                          | `(array<widget>, dictionary?)`                                   | `widget`    | Vertical flex container              |
| `GraphicalUi.grid(columns, children, style?)`                   | `(integer, array<widget>, dictionary?)`                          | `widget`    | CSS grid layout with N columns       |
| `GraphicalUi.panel(title, children, style?)`                    | `(string, array<widget>, dictionary?)`                           | `widget`    | Bordered card with title             |
| `GraphicalUi.row(children, style?)`                             | `(array<widget>, dictionary?)`                                   | `widget`    | Horizontal flex container            |
| `GraphicalUi.scroll_column(children, style?)`                   | `(array<widget>, dictionary?)`                                   | `widget`    | Vertical scroll container            |
| `GraphicalUi.scroll_row(children, style?)`                      | `(array<widget>, dictionary?)`                                   | `widget`    | Horizontal scroll container          |
| `GraphicalUi.tabs(labels, active, on_select, children, style?)` | `(array<string>, integer, function, array<widget>, dictionary?)` | `widget`    | Tabbed container                     |
| `GraphicalUi.toolbar(children, style?)`                         | `(array<widget>, dictionary?)`                                   | `widget`    | Horizontal toolbar                   |
| `GraphicalUi.wrapped_row(children, style?)`                     | `(array<widget>, dictionary?)`                                   | `widget`    | Row that wraps children to next line |

### Nearby / Overlay Elements

Nearby functions position an overlay widget relative to a child widget using absolute CSS positioning. The child is rendered normally and the overlay floats on top of (or beside) it. Each function produces a `nearby` widget with a different `position` value.

| Function                                       | Parameter Types                 | Return Type | Description                                  |
| ---------------------------------------------- | ------------------------------- | ----------- | -------------------------------------------- |
| `GraphicalUi.above(child, overlay, style?)`    | `(widget, widget, dictionary?)` | `widget`    | Overlay positioned above the child           |
| `GraphicalUi.below(child, overlay, style?)`    | `(widget, widget, dictionary?)` | `widget`    | Overlay positioned below the child           |
| `GraphicalUi.on_left(child, overlay, style?)`  | `(widget, widget, dictionary?)` | `widget`    | Overlay positioned to the left of the child  |
| `GraphicalUi.on_right(child, overlay, style?)` | `(widget, widget, dictionary?)` | `widget`    | Overlay positioned to the right of the child |
| `GraphicalUi.in_front(child, overlay, style?)` | `(widget, widget, dictionary?)` | `widget`    | Overlay centred on top of the child          |
| `GraphicalUi.behind(child, overlay, style?)`   | `(widget, widget, dictionary?)` | `widget`    | Overlay behind the child (lower z-index)     |

### Typed Sizing Helpers

Sizing helpers return CSS `flex` shorthand strings or dictionaries that can be passed as style values to control how a widget fills its container.

| Function                                 | Parameter Types      | Return Type  | Description                               |
| ---------------------------------------- | -------------------- | ------------ | ----------------------------------------- |
| `GraphicalUi.fill()`                     | `()`                 | `string`     | Fill available space equally (`"1 1 0%"`) |
| `GraphicalUi.fill_portion(n)`            | `(integer)`          | `string`     | Fill with weight `n` (`"n 1 0%"`)         |
| `GraphicalUi.shrink()`                   | `()`                 | `string`     | Shrink to content size (`"0 1 auto"`)     |
| `GraphicalUi.px(n)`                      | `(integer)`          | `string`     | Fixed pixel size (`"npx"`)                |
| `GraphicalUi.constrained_fill(min, max)` | `(integer, integer)` | `widget`     | Fill with min/max width constraints       |

### Typed Alignment Helpers

Alignment helpers return style dictionaries with the appropriate CSS alignment properties. Pass the result as a style dictionary (or merge it with other styles) to align widgets within their container.

| Function                     | Parameter Types | Return Type  | Description                                          |
| ---------------------------- | --------------- | ------------ | ---------------------------------------------------- |
| `GraphicalUi.center()`       | `()`            | `widget`     | Centre both axes (`align_items` + `justify_content`) |
| `GraphicalUi.center_x()`     | `()`            | `widget`     | Centre along the main axis (`justify_content`)       |
| `GraphicalUi.center_y()`     | `()`            | `widget`     | Centre along the cross axis (`align_items`)          |
| `GraphicalUi.align_left()`   | `()`            | `widget`     | Align to start of cross axis (`align_self`)          |
| `GraphicalUi.align_right()`  | `()`            | `widget`     | Align to end of cross axis (`align_self`)            |
| `GraphicalUi.align_top()`    | `()`            | `widget`     | Align to start of cross axis (`align_self`)          |
| `GraphicalUi.align_bottom()` | `()`            | `widget`     | Align to end of cross axis (`align_self`)            |

### Spacing Helpers

Spacing helpers return style dictionaries that control gap and padding within containers. They can be passed directly as the style parameter or merged with other styles via `GraphicalUi.merge_styles`.

| Function                       | Parameter Types      | Return Type  | Description                                                 |
| ------------------------------ | -------------------- | ------------ | ----------------------------------------------------------- |
| `GraphicalUi.space_evenly()`   | `()`                 | `widget`     | Distribute children with equal space around each            |
| `GraphicalUi.space_between()`  | `()`                 | `widget`     | Place equal space between children, none at edges           |
| `GraphicalUi.space_around()`   | `()`                 | `widget`     | Place equal space around each child                         |
| `GraphicalUi.spacing(pixels)`  | `(integer)`          | `widget`     | Uniform gap between children                                |
| `GraphicalUi.spacing_xy(x, y)` | `(integer, integer)` | `widget`     | Separate horizontal (`column_gap`) and vertical (`row_gap`) |
| `GraphicalUi.padding(pixels)`  | `(integer)`          | `widget`     | Uniform padding on all sides                                |
| `GraphicalUi.padding_xy(x, y)` | `(integer, integer)` | `widget`     | Horizontal and vertical padding                             |

### Layout Debugging

| Function                           | Parameter Types         | Return Type | Description                                                   |
| ---------------------------------- | ----------------------- | ----------- | ------------------------------------------------------------- |
| `GraphicalUi.debug(child, style?)` | `(widget, dictionary?)` | `widget`    | Wrap a widget tree with coloured outlines to visualise layout |

`debug` renders coloured borders around every element in the subtree, cycling through red, blue, and green at each nesting depth. Useful for diagnosing alignment and sizing issues during development.

### Device Classification

| Function                                     | Parameter Types      | Return Type  | Description                                                                   |
| -------------------------------------------- | -------------------- | ------------ | ----------------------------------------------------------------------------- |
| `GraphicalUi.classify_device(width, height)` | `(integer, integer)` | `widget`     | Classify viewport into `"phone"`, `"tablet"`, `"desktop"`, or `"big_desktop"` |

Returns a dictionary with `class` (device category), `orientation` (`"portrait"` or `"landscape"`), `width`, and `height`. Combine with `on_resize` to build responsive layouts:

| Width Range | Class           |
| ----------- | --------------- |
| < 640       | `"phone"`       |
| 640 – 1023  | `"tablet"`      |
| 1024 – 1919 | `"desktop"`     |
| ≥ 1920      | `"big_desktop"` |

### Display Widgets

| Function                                                          | Parameter Types                                                 | Return Type | Description                                                                |
| ----------------------------------------------------------------- | --------------------------------------------------------------- | ----------- | -------------------------------------------------------------------------- |
| `GraphicalUi.alert(message, severity?, style?)`                   | `(string, string?, dictionary?)`                                | `widget`    | Styled alert box                                                           |
| `GraphicalUi.badge(text)`                                         | `(string)`                                                      | `widget`    | Small inline status badge                                                  |
| `GraphicalUi.dialog(title, children, is_open, on_close?, style?)` | `(string, array<widget>, boolean, function?, dictionary?)`      | `widget`    | Modal dialog; clicking overlay triggers `on_close`                         |
| `GraphicalUi.icon(name, size?, style?)`                           | `(string, integer?, dictionary?)`                               | `widget`    | Lucide SVG icon by kebab-case `name` (e.g. `"trending-up"`); see Guide §22 |
| `GraphicalUi.link(text, on_click_or_url, style?)`                 | `(string, string\|function, dictionary?)`                       | `widget`    | String opens URL; function dispatches message to `update`                  |
| `GraphicalUi.list(items, on_select?, style?)`                     | `(array<string\|widget>, function?, dictionary?)`               | `widget`    | Vertical list (items may be strings or widgets)                            |
| `GraphicalUi.table(headers, rows, on_row_click?, options?)`       | `(array<string>, array<array<string>>, function?, dictionary?)` | `widget`    | Data table (sticky header + zebra by default; `options`: `align`, `selected`, `on_sort`, `sort_column`, `sort_direction`) |
| `GraphicalUi.tooltip(text, child, style?)`                        | `(string, widget, dictionary?)`                                 | `widget`    | Hover tooltip                                                              |

### Composite & Advanced Widgets

Higher-level widgets composed from the primitives above. Each is a first-class
catalog function with a browser renderer; callbacks are dispatched through the
same `update` cycle as the basic widgets.

| Function                                                              | Parameter Types                                  | Return Type | Description                                                     |
| --------------------------------------------------------------------- | ------------------------------------------------ | ----------- | --------------------------------------------------------------- |
| `GraphicalUi.accordion(sections)`                                     | `(array<dictionary>)`                            | `widget`    | Stack of collapsible sections (each: `title`/`label` + content) |
| `GraphicalUi.animate(child, keyframes, options?)`                     | `(widget, array<dictionary>, dictionary?)`       | `widget`    | Apply keyframe animations to a child                            |
| `GraphicalUi.avatar(name, url?)`                                      | `(string, string?)`                              | `widget`    | Circular avatar — image when `url` is given, else name initials |
| `GraphicalUi.breadcrumb(items, on_navigate?)`                         | `(array<string>, func(string) -> any)`           | `widget`    | Navigation trail; non-final items are clickable                 |
| `GraphicalUi.card(children)`                                          | `(array<widget>)`                                | `widget`    | Bordered surface that stacks its children (a titleless panel)   |
| `GraphicalUi.combobox(value, options, on_change, on_select?)`         | `(string, array<string>, func(string) -> any, func(string) -> any)` | `widget`    | Autocomplete text input with a filtered, keyboard-navigable list |
| `GraphicalUi.confirm(title, message, on_confirm, on_cancel?, options?)` | `(string, string, func() -> any, func() -> any, dictionary?)`   | `widget`    | Modal confirmation dialog (`role="alertdialog"`); `options`: `confirm_label`/`cancel_label`/`danger` |
| `GraphicalUi.draggable(child, data)`                                  | `(widget, string)`                               | `widget`    | Marks a child as a drag source carrying `data`                  |
| `GraphicalUi.drop_target(child, on_drop)`                             | `(widget, func(string) -> any)`                  | `widget`    | Drop zone; `on_drop` receives the dragged data string           |
| `GraphicalUi.empty_state(message, options?)`                          | `(string, dictionary?)`                          | `widget`    | Placeholder for a blank list/panel; `options`: `title`/`icon`/`action_label`/`on_action` |
| `GraphicalUi.field(label, control, options?)`                        | `(string, widget, dictionary?)`                  | `widget`    | Labelled form control; `options`: `required`/`help`/`error`     |
| `GraphicalUi.field_error(message)`                                    | `(string)`                                       | `widget`    | Inline form validation message (icon + text)                    |
| `GraphicalUi.form(children, on_submit)`                               | `(array<widget>, func() -> any)`                 | `widget`    | Form container; submits on Enter or a submit button             |
| `GraphicalUi.infinite_scroll(items, item_height, on_load_more)`       | `(array, integer, func() -> any)`                | `widget`    | Scrolling list that requests more items near the end            |
| `GraphicalUi.inspect(child)`                                          | `(widget)`                                       | `widget`    | Wraps a child with a debug inspector overlay                    |
| `GraphicalUi.menu(label, items, on_select)`                          | `(string, array<string>, func(string) -> any)`   | `widget`    | Click-to-open action menu; arrow-key + Esc keyboard support     |
| `GraphicalUi.number_input(value, min, max, on_change)`                | `(number, number, number, func(number) -> any)`  | `widget`    | Numeric input field                                             |
| `GraphicalUi.paginator(current_page, total_pages, on_page_change)`    | `(integer, integer, func(integer) -> any)`       | `widget`    | Page navigation control                                         |
| `GraphicalUi.popover(label, content)`                                | `(string, widget)`                               | `widget`    | Click-to-open floating panel; closes on outside-click or Esc    |
| `GraphicalUi.search_input(value, on_change, on_clear?)`               | `(string, func(string) -> any, func() -> any)`   | `widget`    | Text field with an optional clear button                        |
| `GraphicalUi.skeleton(width?, height?)`                               | `(integer?, integer?)`                           | `widget`    | Shimmering placeholder block for loading states                 |
| `GraphicalUi.switch(label, checked, on_toggle)`                       | `(string, boolean, func(boolean) -> any)`        | `widget`    | On/off switch (a toggle with the `switch` ARIA role)            |
| `GraphicalUi.toast(message, severity?, duration?, action_label?, on_action?, style?)` | `(string, string?, integer?, string?, func() -> any, dictionary?)` | `widget`    | Transient notification (reuses the alert severity palette); optional inline action (e.g. "Undo") |
| `GraphicalUi.toast_region(toasts, options?)`                          | `(array<widget>, dictionary?)`                   | `widget`    | Fixed-position stack of toasts; `options.position` (default `bottom-right`); schedule auto-dismiss with `delay` |
| `GraphicalUi.transition(child, properties)`                           | `(widget, dictionary)`                           | `widget`    | Animate CSS property changes on a child                         |
| `GraphicalUi.virtual_list(items, item_height, visible_count, style?)` | `(array, integer, integer, dictionary?)`         | `widget`    | Windowed list that renders only visible rows                    |
| `GraphicalUi.when(condition, widget)`                                 | `(boolean, widget)`                              | `widget`    | Render `widget` only when `condition` is true                   |
| `GraphicalUi.wizard(steps, active_step, on_step_change)`              | `(array<widget>, integer, func(integer) -> any)` | `widget`    | Multi-step flow with a step indicator                           |

### Chart Widgets

All charts are rendered as inline SVG. The `labels` array provides category names and the `values` array provides the corresponding numeric data points; both arrays must have the same length. A `result<array<T>>` is also accepted and automatically unwrapped.

| Function                                                                   | Parameter Types                                                 | Return Type | Description            |
| -------------------------------------------------------------------------- | --------------------------------------------------------------- | ----------- | ---------------------- |
| `GraphicalUi.area_chart(labels, values, style?)`                           | `(array<string>, array<number>, dictionary?)`                   | `widget`    | Filled area chart      |
| `GraphicalUi.donut_chart(labels, values, center_label?, style?)`           | `(array<string>, array<number>, string?, dictionary?)`          | `widget`    | Donut chart            |
| `GraphicalUi.horizontal_bar_chart(labels, values, style?)`                 | `(array<string>, array<number>, dictionary?)`                   | `widget`    | Horizontal bar chart   |
| `GraphicalUi.line_chart(labels, values, style?)`                           | `(array<string>, array<number>, dictionary?)`                   | `widget`    | Line chart with points |
| `GraphicalUi.pie_chart(labels, values, style?)`                            | `(array<string>, array<number>, dictionary?)`                   | `widget`    | Pie chart with legend  |
| `GraphicalUi.scatter_plot(x_values, y_values, x_label?, y_label?, style?)` | `(array<number>, array<number>, string?, string?, dictionary?)` | `widget`    | Scatter plot           |
| `GraphicalUi.vertical_bar_chart(labels, values, style?)`                   | `(array<string>, array<number>, dictionary?)`                   | `widget`    | Vertical bar chart     |

Charts show a hover tooltip by default. The trailing dictionary also accepts the
presentation options `x_label`, `y_label`, `legend` (boolean), and `tooltip`
(boolean, default `true`); any other key is treated as CSS on the chart container.

### Theme

Pass a `"theme"` dictionary in the app config to customise the visual appearance.
Keys map to CSS custom properties. The defaults derive from the bundled
[Pico CSS](https://picocss.com/) theme; the swatches show the light-mode rendering
and dark mode flips automatically (see below).

| Theme Key             | CSS Variable          | Default (Pico-derived)                                |
| --------------------- | --------------------- | ----------------------------------------------------- |
| `accent_hover`        | `--gui-primary-hover` | `var(--pico-primary-hover)` (≈ `#015887`)             |
| `accent`              | `--gui-primary`       | `var(--pico-primary)` (≈ `#0172ad`)                   |
| `background`          | `--gui-bg`            | `var(--pico-background-color)` (≈ `#fff`)             |
| `border`              | `--gui-border`        | `var(--pico-muted-border-color)`                      |
| `disabled_background` | `--gui-disabled-bg`   | `var(--pico-form-element-disabled-background-color)`  |
| `disabled_text`       | `--gui-disabled-fg`   | `var(--pico-muted-color)`                             |
| `error`               | `--gui-error`         | `hsl(0 85% 60%)` (≈ `#ef4444`)                        |
| `font`                | `--gui-font`          | `var(--pico-font-family)` (system stack)             |
| `gap`                 | `--gui-gap`           | `0.5rem` (8px)                                        |
| `input_background`    | `--gui-input-bg`      | `var(--pico-form-element-background-color)`           |
| `input_border`        | `--gui-input-border`  | `var(--pico-form-element-border-color)`               |
| `input_focus`         | `--gui-input-focus`   | `var(--pico-primary)` (≈ `#0172ad`)                   |
| `radius`              | `--gui-radius`        | `var(--pico-border-radius)` (`0.25rem` / 4px)         |
| `shadow`              | `--gui-shadow`        | `var(--pico-box-shadow)`                              |
| `success`             | `--gui-success`       | `hsl(160 84% 39%)` (≈ `#10b981`)                      |
| `text_color`          | `--gui-fg`            | `var(--pico-color)` (≈ `#373c44`)                     |
| `text_muted`          | `--gui-text-muted`    | `var(--pico-muted-color)` (secondary / muted text)    |
| `warning`             | `--gui-warning`       | `hsl(38 92% 50%)` (≈ `#f59e0b`)                       |

Dark mode is applied automatically via `@media (prefers-color-scheme: dark)` with adjusted defaults. Explicit theme keys override both light and dark defaults.

Theme values can also be specified per mode using a dictionary with `"light"` and `"dark"` keys.

Custom theme variables use the `custom_` prefix and are exposed as `--gui-custom-*` CSS properties.

Set `"animations": false` in the theme to disable **all** framework motion (transitions and animations), independent of the OS reduced-motion setting, which is always honoured. Omit the key or set it to `true` to keep motion enabled.

### Styling

Styling functions provide advanced control over widget appearance — style composition, responsive layouts, external stylesheets, CSS validation, and theme mode switching.

| Function                                       | Parameter Types                 | Return Type          | Description                                                                    |
| ---------------------------------------------- | ------------------------------- | -------------------- | ------------------------------------------------------------------------------ |
| `GraphicalUi.merge_styles(base, overrides...)` | `(dictionary, dictionary, ...)` | `widget`             | Merge style dictionaries; later values override earlier ones                   |
| `GraphicalUi.stylesheet(css)`                  | `(string)`                      | `command`            | Inject a CSS `<style>` block (deduplicated by content hash)                    |
| `GraphicalUi.load_stylesheet(path)`            | `(string)`                      | `command`            | Load a `.css` file from a relative path                                        |
| `GraphicalUi.font_face(path, family, opts?)`   | `(string, string, dictionary)`  | `command`            | Embed a local `.woff2`/`.woff`/`.ttf`/`.otf` font file, optionally as the default UI font |
| `GraphicalUi.set_theme_mode(mode)`             | `(string)`                      | `command`            | Force `"light"`, `"dark"`, or `"auto"` mode                                    |
| `GraphicalUi.responsive(breakpoints)`          | `(dictionary)`                  | `widget`             | Select a style based on window width (mobile-first breakpoints)                |
| `GraphicalUi.validate_style(style)`            | `(dictionary)`                  | `result<widget>`     | Validate CSS property names; suggests corrections on failure                   |
| `GraphicalUi.if_dark(dark_value, light_value)` | `(string, string)`              | `widget`             | Theme-conditional value: `dark_value` in dark mode, else `light_value`         |
| `GraphicalUi.transition_preset(name)`          | `(string)`                      | `widget`             | Preset transition style (`ease`, `spring`, `bounce`, `fade`, `slide`, `scale`) |

**Notes.** `stylesheet` rejects `<script>` tags, `javascript:` URLs, and `expression()` calls; `load_stylesheet` accepts only relative `.css` paths. `font_face` accepts only relative font paths (no absolute paths, remote URLs, or `..` traversal), inlines the file as a `data:`-URI `@font-face` rule, and by default sets the family as the UI font — options are `{"weight", "style", "default"}` (an explicit theme `font` still wins). For `responsive`, the `"0"` key is the base style, merged with the best matching breakpoint. See the [GraphicalUi Guide](Luma_GraphicalUi_Guide.md) §8 for worked styling examples.

### Pseudo-Class Styles

Style dictionary keys can use pseudo-class prefixes to apply styles on hover, focus, and other states. The prefix is separated from the property name by an underscore:

| Prefix      | CSS Pseudo-Class |
| ----------- | ---------------- |
| `hover_`    | `:hover`         |
| `focus_`    | `:focus`         |
| `active_`   | `:active`        |
| `disabled_` | `:disabled`      |
| `checked_`  | `:checked`       |

### CSS Classes

The `"class"` key in a style dictionary assigns one or more CSS class names to the widget element. Use this together with `stylesheet` or `load_stylesheet` for class-based styling.

### Commands (Side Effects)

Commands represent side effects that the runtime executes on behalf of the application. Instead of performing effects directly in `update`, return a `(model, command)` pair via `GraphicalUi.with_command` so the runtime manages the effect and delivers the result as a message.

| Function                                                           | Parameter Types                                                        | Return Type  | Description                                                     |
| ------------------------------------------------------------------ | ---------------------------------------------------------------------- | ------------ | --------------------------------------------------------------- |
| `GraphicalUi.none()`                                               | `()`                                                                   | `command`    | No-op command (no side effect)                                  |
| `GraphicalUi.batch(commands)`                                      | `(array<command>)`                                                     | `command`    | Execute multiple commands                                       |
| `GraphicalUi.http_get(url, on_result, headers?, timeout?)`         | `(string, func(result<string>) -> any, dictionary?, integer?)`         | `command`    | HTTP GET; optional headers and timeout                          |
| `GraphicalUi.http_post(url, body, on_result, headers?, timeout?)`  | `(string, string, func(result<string>) -> any, dictionary?, integer?)` | `command`    | HTTP POST with body; optional headers and timeout               |
| `GraphicalUi.delay(milliseconds, on_done)`                         | `(integer, func(result<string>) -> any)`                               | `command`    | Wait for `milliseconds`, then invoke callback                   |
| `GraphicalUi.write_clipboard(text)`                                | `(string)`                                                             | `command`    | Copy text to the system clipboard                               |
| `GraphicalUi.random(min, max, on_result)`                          | `(number, number, func(number) -> any)`                                | `command`    | Generate a random number in `[min, max]`                        |
| `GraphicalUi.focus(widget_id)`                                     | `(string)`                                                             | `command`    | Move keyboard focus to the widget with the given `id`           |
| `GraphicalUi.announce(text)`                                       | `(string)`                                                             | `command`    | Announce text to screen readers via a live region               |
| `GraphicalUi.debounce(id, milliseconds, callback)`                 | `(string, integer, func(result<string>) -> any)`                       | `command`    | Run `callback` only after `milliseconds` of inactivity for `id` |
| `GraphicalUi.download_file(url, filename)`                         | `(string, string)`                                                     | `command`    | Download a URL to a local file named `filename`                 |
| `GraphicalUi.get_local_storage(key, on_result)`                    | `(string, func(string) -> any)`                                        | `command`    | Read a browser local-storage value by `key`                     |
| `GraphicalUi.http_delete(url, on_result, headers?, timeout?)`      | `(string, func(result<string>) -> any, dictionary?, integer?)`         | `command`    | HTTP DELETE; optional headers and timeout                       |
| `GraphicalUi.http_patch(url, body, on_result, headers?, timeout?)` | `(string, string, func(result<string>) -> any, dictionary?, integer?)` | `command`    | HTTP PATCH with body; optional headers and timeout              |
| `GraphicalUi.http_put(url, body, on_result, headers?, timeout?)`   | `(string, string, func(result<string>) -> any, dictionary?, integer?)` | `command`    | HTTP PUT with body; optional headers and timeout                |
| `GraphicalUi.notify(title, body?, icon?)`                          | `(string, string?, string?)`                                           | `command`    | Show a system desktop notification                              |
| `GraphicalUi.open_url(url)`                                        | `(string)`                                                             | `command`    | Open a URL in the default browser                               |
| `GraphicalUi.print()`                                              | `()`                                                                   | `command`    | Open the browser print dialog                                   |
| `GraphicalUi.read_clipboard(on_result)`                            | `(func(result<string>) -> any)`                                        | `command`    | Read text from the system clipboard                             |
| `GraphicalUi.set_local_storage(key, value)`                        | `(string, string)`                                                     | `command`    | Write a browser local-storage value                             |
| `GraphicalUi.set_title(title)`                                     | `(string)`                                                             | `command`    | Set the application window title                                |
| `GraphicalUi.with_command(model, command)`                         | `(any, command)`                                                       | `widget`     | Pair a new model with a command for the runtime to run          |

The `update` function may return either a plain model (no side effect) or a `with_command` pair.

### Subscriptions

Subscriptions let an application react to external events that are not tied to a specific widget — timers, keyboard input, window resize, and focus changes. Provide a `"subscribe"` function in the app config that returns an array of active subscriptions based on the current model. The runtime diffs subscription arrays across renders and automatically sets up or tears down listeners.

| Function                                            | Parameter Types                             | Return Type    | Description                                                              |
| --------------------------------------------------- | ------------------------------------------- | -------------- | ------------------------------------------------------------------------ |
| `GraphicalUi.on_tick(id, interval_ms, on_tick)`     | `(string, integer, func() -> any)`          | `subscription` | Fire `on_tick` every `interval_ms` milliseconds                          |
| `GraphicalUi.on_key(id, key_filter, on_key)`        | `(string, string, func(string) -> any)`     | `subscription` | Fire on key press; `"*"` matches any key                                 |
| `GraphicalUi.on_resize(id, on_resize)`              | `(string, func(integer, integer) -> any)`   | `subscription` | Fire when the window is resized with `(width, height)`                   |
| `GraphicalUi.on_focus(id, on_focus)`                | `(string, func(boolean) -> any)`            | `subscription` | Fire when the window gains or loses focus                                |
| `GraphicalUi.on_mouse(id, event_type, on_event)`    | `(string, string, func(dictionary) -> any)` | `subscription` | Fire on mouse events (`"click"`, `"move"`, `"down"`, `"up"`, `"scroll"`) |
| `GraphicalUi.on_animation_frame(id, on_frame)`      | `(string, func() -> any)`                   | `subscription` | Fire before each browser animation frame                                 |
| `GraphicalUi.on_drag(id, event_type, on_drag)`      | `(string, string, func(dictionary) -> any)` | `subscription` | Fire on drag events; `event_type` selects the phase                      |
| `GraphicalUi.on_idle(id, timeout_ms, on_idle)`      | `(string, integer, func() -> any)`          | `subscription` | Fire after `timeout_ms` of user inactivity                               |
| `GraphicalUi.on_media_query(id, query, on_match)`   | `(string, string, func(boolean) -> any)`    | `subscription` | Fire when a CSS media-query match state changes                          |
| `GraphicalUi.on_offline(id, on_offline)`            | `(string, func() -> any)`                   | `subscription` | Fire when the browser goes offline                                       |
| `GraphicalUi.on_online(id, on_online)`              | `(string, func() -> any)`                   | `subscription` | Fire when the browser comes online                                       |
| `GraphicalUi.on_scroll(id, on_scroll)`              | `(string, func(dictionary) -> any)`         | `subscription` | Fire on window scroll with the scroll position                           |
| `GraphicalUi.on_storage_change(id, key, on_change)` | `(string, string, func(string) -> any)`     | `subscription` | Fire when local-storage `key` changes (e.g. another tab)                 |
| `GraphicalUi.on_visibility_change(id, on_change)`   | `(string, func(boolean) -> any)`            | `subscription` | Fire when page visibility changes (tab shown/hidden)                     |

The `on_mouse` callback receives a dictionary with `x`, `y`, `button` (`"left"`, `"middle"`, `"right"`), and modifier keys (`ctrl`, `shift`, `alt`).

Each subscription requires a unique `id` string. The runtime uses the `id` to match subscriptions across renders — if the `id` disappears from the returned array, the listener is removed.

### State History

Undo/redo helpers manage a history stack of model states outside the widget
tree. Each returns a `{model, history}` dictionary — assign the new model and
history back into application state inside `update`.

| Function                                | Parameter Types | Return Type  | Description                                                   |
| --------------------------------------- | --------------- | ------------ | ------------------------------------------------------------- |
| `GraphicalUi.undo(model, undo_history)` | `(any, array)`  | `widget`     | Restore the previous model; returns a `{model, history}` pair |
| `GraphicalUi.redo(model, redo_history)` | `(any, array)`  | `widget`     | Reapply an undone model; returns a `{model, history}` pair    |

### Components

Components encapsulate a slice of the model and a render function into a reusable widget. The runtime memoizes components by `id` — if the same `id` is rendered again with an identical `model_slice` (compared by JSON serialisation), the cached widget is returned without invoking `render_fn`.

| Function                                            | Parameter Types                      | Return Type | Description                                    |
| --------------------------------------------------- | ------------------------------------ | ----------- | ---------------------------------------------- |
| `GraphicalUi.component(id, model_slice, render_fn)` | `(string, any, func(any) -> widget)` | `widget`    | Reusable component with identity-based caching |

### Routing

Routing enables multi-page navigation within a single-window application. The `router` widget renders the child that matches the current route. Route values may be callables `func() -> widget` or pre-built widgets. Parameterised routes use `{name}` placeholders — the matched segments are passed as a dictionary to the callable.

Use `navigate` and `navigate_back` commands to change routes, and `navigation_link` to create clickable navigation elements.

| Function                                             | Parameter Types              | Return Type | Description                                        |
| ---------------------------------------------------- | ---------------------------- | ----------- | -------------------------------------------------- |
| `GraphicalUi.router(route, routes)`                  | `(string, dictionary)`       | `widget`    | Render the widget matching the current `route` key |
| `GraphicalUi.navigate(route)`                        | `(string)`                   | `command`   | Command to navigate to a route                     |
| `GraphicalUi.navigate_back()`                        | `()`                         | `command`   | Command to navigate to the previous route          |
| `GraphicalUi.navigation_link(text, message, style?)` | `(string, any, dictionary?)` | `widget`    | Styled link that dispatches `message` when clicked |

### Keyed Lists

When rendering dynamic lists, wrap each item in `keyed` to give it a stable identity. This allows the DOM-diffing algorithm to match elements efficiently when the list is reordered, inserted into, or filtered.

| Function                        | Parameter Types    | Return Type | Description                              |
| ------------------------------- | ------------------ | ----------- | ---------------------------------------- |
| `GraphicalUi.keyed(key, child)` | `(string, widget)` | `widget`    | Assign a stable identity key to a widget |

### Error Boundaries

Wrap a view function in `error_boundary` to catch rendering errors gracefully. If `view_fn` throws, `fallback_fn` is called with the error message instead of crashing the entire view.

| Function                                           | Parameter Types                              | Return Type | Description                                   |
| -------------------------------------------------- | -------------------------------------------- | ----------- | --------------------------------------------- |
| `GraphicalUi.error_boundary(fallback_fn, view_fn)` | `(func(string) -> widget, func() -> widget)` | `widget`    | Catch rendering errors with a fallback widget |

### Accessibility

Accessibility functions add ARIA attributes, manage focus, and provide screen reader announcements.

| Function                                       | Parameter Types        | Return Type | Description                                                     |
| ---------------------------------------------- | ---------------------- | ----------- | --------------------------------------------------------------- |
| `GraphicalUi.accessible(child, attributes)`    | `(widget, dictionary)` | `widget`    | Wrap a widget with ARIA attributes (`role`, `aria_label`, etc.) |
| `GraphicalUi.aria_describedby(desc_id, child)` | `(string, widget)`     | `widget`    | Link a child to a description element by `id`                   |
| `GraphicalUi.aria_live(level, child)`          | `(string, widget)`     | `widget`    | Mark a child as an ARIA live region (`"polite"`/`"assertive"`)  |
| `GraphicalUi.focus(widget_id)`                 | `(string)`             | `command`   | Move keyboard focus to a widget (see Commands)                  |
| `GraphicalUi.announce(text)`                   | `(string)`             | `command`   | Announce text to screen readers (see Commands)                  |

The `attributes` dictionary accepts `"role"` and any key starting with `"aria_"` (underscores are converted to hyphens in the rendered HTML).

## 16 — Hash

Cryptographic and non-cryptographic hash digests, HMAC, and verification.

| Function                        | Parameter Types            | Return Type      | Description                    |
| ------------------------------- | -------------------------- | ---------------- | ------------------------------ |
| `Hash.algorithms()`             | `()`                       | `array<string>`  | List supported algorithm names |
| `Hash.crc32(s)`                 | `(string)`                 | `integer`        | CRC-32 checksum                |
| `Hash.digest(algo, s)`          | `(Hash.Algorithm \| string, string)` | `string` | Digest `s` with the named algorithm; accepts the `Hash.Algorithm` choice or its string name |
| `Hash.hmac_sha256(key, msg)`    | `(string, string)`         | `string`         | HMAC-SHA-256                   |
| `Hash.hmac_sha512(key, msg)`    | `(string, string)`         | `string`         | HMAC-SHA-512                   |
| `Hash.md5(s)`                   | `(string)`                 | `string`         | MD5 digest (32-char hex)       |
| `Hash.md5_typed(s)`             | `(string)`                 | `Hash.Digest`    | MD5 digest tagged with its algorithm |
| `Hash.sha1(s)`                  | `(string)`                 | `string`         | SHA-1 digest (40-char hex)     |
| `Hash.sha1_typed(s)`            | `(string)`                 | `Hash.Digest`    | SHA-1 digest tagged with its algorithm |
| `Hash.sha256_file(path)`        | `(string)`                 | `result<string>` | SHA-256 of file contents       |
| `Hash.sha256(s)`                | `(string)`                 | `string`         | SHA-256 digest (64-char hex)   |
| `Hash.sha256_typed(s)`          | `(string)`                 | `Hash.Digest`    | SHA-256 digest tagged with its algorithm |
| `Hash.sha512_file(path)`        | `(string)`                 | `result<string>` | SHA-512 of file contents       |
| `Hash.sha512(s)`                | `(string)`                 | `string`         | SHA-512 digest (128-char hex)  |
| `Hash.sha512_typed(s)`          | `(string)`                 | `Hash.Digest`    | SHA-512 digest tagged with its algorithm |
| `Hash.verify(algo, data, hash)` | `(Hash.Algorithm \| string, string, string)` | `boolean` | Verify `hash` matches `data`; accepts the `Hash.Algorithm` choice or its string name |

`Hash.Algorithm` is a choice type with five variants — `Md5`, `Sha1`, `Sha256`, `Sha512`, `Crc32` — the discoverable, closed set of algorithms that `Hash.digest` and `Hash.verify` accept. Both functions take `Hash.Algorithm | string`: passing the choice variant is autocompleted and a typo becomes a compile error, while the string form (`"sha256"`, matching `Hash.algorithms()`) keeps every existing call working. This is the same "stringly-typed argument → choice, keep the string form for compatibility" dual-form as `Terminal.Color | string`.

```luma
string h = Hash.digest(Hash.Algorithm.Sha256, "hello")   # typed, autocompleted
boolean ok = Hash.verify(Hash.Algorithm.Sha256, "hello", h)
boolean also_ok = Hash.verify("sha256", "hello", h)       # string form still works
```

`Hash.Digest` is a record — `algorithm` (`Hash.Algorithm`), `hex` (`string`) — that pairs a hex digest with the algorithm that produced it, so a SHA-256 and an MD5 digest are no longer the same (bare-string) type and cannot be compared across algorithms by accident. The per-algorithm `*_typed` companions (`Hash.md5_typed`, `Hash.sha1_typed`, `Hash.sha256_typed`, `Hash.sha512_typed`) return it; the plain digest functions (`Hash.sha256`, …) still return a bare hex string.

```luma
Hash.Digest d = Hash.sha256_typed("hello")
print(d.hex)                                              # the 64-char hex string
match d.algorithm { case Hash.Algorithm.Sha256 { print("sha-256") } else { print("other") } }
```

## 17 — HashSet

A hash-based set providing O(1) average-case membership testing. Supports hashable primitive types (boolean, integer, number, string). All operations are immutable — they return a new hash set.

> **HashSet vs Set** — `HashSet` uses hashing for **O(1)** average-case lookups but only supports **primitive types** (`boolean`, `integer`, `number`, `string`). Elements are **unordered**. If you need ordered elements or non-primitive value types (records, tuples, etc.), use `Set` instead — it stores elements in an array with O(n) membership tests but preserves insertion order and accepts any type.

| Function                                  | Parameter Types                      | Return Type                    | Description                                                    |
| ----------------------------------------- | ------------------------------------ | ------------------------------ | -------------------------------------------------------------- |
| `HashSet.add(hs, v)`                      | `(hash_set, T)`                      | `hash_set`                     | Set with `v` added                                             |
| `HashSet.contains(hs, v)`                 | `(hash_set, T)`                      | `boolean`                      | Whether `v` is in the set                                      |
| `HashSet.difference(hs, other)`           | `(hash_set, hash_set)`               | `hash_set`                     | Elements in `hs` but not `other`                               |
| `HashSet.each(hs, fn)`                    | `(hash_set, function(T) -> none)`    | `result<none>`                 | Apply `fn` to each element; fail if callback throws            |
| `HashSet.equals(hs, other)`               | `(hash_set, hash_set)`               | `boolean`                      | Whether the sets contain the same elements                     |
| `HashSet.filter(hs, fn)`                  | `(hash_set, function(T) -> boolean)` | `result<hash_set>`             | Elements for which `fn` returns true; fail if predicate throws |
| `HashSet.from_array(arr)`                 | `(array<T>)`                         | `hash_set`                     | Create from array (deduplicates)                               |
| `HashSet.from_set(s)`                     | `(set)`                              | `hash_set`                     | Convert from `Set`                                             |
| `HashSet.intersection(hs, other)`         | `(hash_set, hash_set)`               | `hash_set`                     | Intersection of two sets                                       |
| `HashSet.is_disjoint(hs, other)`          | `(hash_set, hash_set)`               | `boolean`                      | Whether the sets share no elements                             |
| `HashSet.is_empty(hs)`                    | `(hash_set)`                         | `boolean`                      | Whether the set is empty                                       |
| `HashSet.is_subset(hs, other)`            | `(hash_set, hash_set)`               | `boolean`                      | Whether `hs` ⊆ `other`                                         |
| `HashSet.is_superset(hs, other)`          | `(hash_set, hash_set)`               | `boolean`                      | Whether `hs` ⊇ `other`                                         |
| `HashSet.length(hs)`                      | `(hash_set)`                         | `integer`                      | Number of elements                                             |
| `HashSet.map(hs, fn)`                     | `(hash_set, function(T) -> U)`       | `result<hash_set>`             | Transform each element into a new set; fail if callback throws |
| `HashSet.new()`                           | `()`                                 | `hash_set`                     | Empty hash set                                                 |
| `HashSet.partition(hs, fn)`               | `(hash_set, function(T) -> boolean)` | `result<(hash_set, hash_set)>` | Split into `(matches, rest)`; fail if predicate throws         |
| `HashSet.reduce(hs, initial, fn)`         | `(hash_set, U, function(U, T) -> U)` | `result<U>`                    | Fold elements with accumulator; fail if `fn` throws            |
| `HashSet.remove(hs, v)`                   | `(hash_set, T)`                      | `hash_set`                     | Set with `v` removed                                           |
| `HashSet.symmetric_difference(hs, other)` | `(hash_set, hash_set)`               | `hash_set`                     | Elements in one but not both                                   |
| `HashSet.to_array(hs)`                    | `(hash_set)`                         | `array<T>`                     | Convert to array                                               |
| `HashSet.to_set(hs)`                      | `(hash_set)`                         | `set`                          | Convert to `Set`                                               |
| `HashSet.union(hs, other)`                | `(hash_set, hash_set)`               | `hash_set`                     | Union of two sets                                              |

## 18 — Http

Plain HTTP/1.1 client built on raw sockets. Only `http://` is supported; `https://` URLs return an error result.

| Function                              | Parameter Types                            | Return Type             | Description                                                          |
| ------------------------------------- | ------------------------------------------ | ----------------------- | -------------------------------------------------------------------- |
| `Http.authorization_header(auth)`     | `(Http.Auth)`                              | `string`                | Render an `Http.Auth` choice into its `Authorization` header value    |
| `Http.basic_auth(user, pass)`         | `(string, string)`                         | `string`                | Build a Basic `Authorization` header value                           |
| `Http.bearer_auth(token)`             | `(string)`                                 | `string`                | Build a Bearer `Authorization` header value                          |
| `Http.build_query(params)`            | `(dictionary<string>)`                     | `string`                | Build query string (e.g. `"a=1&b=2"`)                                |
| `Http.cookie_header(cookie)`          | `(Http.Cookie)`                            | `string`                | Format an `Http.Cookie` back into a `Set-Cookie`-style header string  |
| `Http.delete(url)`                    | `(string)`                                 | `result<Http.Response>` | DELETE request                                                       |
| `Http.delete_with(url, headers)`      | `(string, dictionary<string>)`             | `result<Http.Response>` | DELETE with custom headers                                           |
| `Http.download(url, path)`            | `(string, string)`                         | `result<string>`        | Download file to local path                                          |
| `Http.get(url)`                       | `(string)`                                 | `result<Http.Response>` | GET request                                                          |
| `Http.get_typed(url)`                 | `(string)`                                 | `result<Http.Response, Http.Error>` | GET request; on failure the error is a typed `Http.Error` instead of a string |
| `Http.get_with(url, headers)`         | `(string, dictionary<string>)`             | `result<Http.Response>` | GET with custom headers                                              |
| `Http.head(url)`                      | `(string)`                                 | `result<Http.Response>` | HEAD request                                                         |
| `Http.is_success(response)`           | `(Http.Response)`                          | `boolean`               | Whether the response status is in the 2xx (`Success`) class          |
| `Http.method_to_string(method)`       | `(Http.Method)`                            | `string`                | Convert an `Http.Method` variant to its uppercase HTTP verb           |
| `Http.parse_query(qs)`                | `(string)`                                 | `dictionary<string>`    | Parse query string into dictionary                                   |
| `Http.parse_cookie(header)`           | `(string)`                                 | `result<Http.Cookie>`   | Parse a `Set-Cookie` header into a typed `Http.Cookie`               |
| `Http.parse_media_type(header)`       | `(string)`                                 | `result<Http.MediaType>` | Parse a `Content-Type` header into a typed `Http.MediaType`         |
| `Http.parse_url(url)`                 | `(string)`                                 | `Http.UrlParts`         | Parse URL into record with `scheme`, `host`, `port`, `path`, `query` |
| `Http.patch(url, body)`               | `(string, string)`                         | `result<Http.Response>` | PATCH request                                                        |
| `Http.patch_with(url, body, headers)` | `(string, string, dictionary<string>)`     | `result<Http.Response>` | PATCH with body and custom headers                                   |
| `Http.post(url, body)`                | `(string, string)`                         | `result<Http.Response>` | POST request                                                         |
| `Http.post_with(url, body, headers)`  | `(string, string, dictionary<string>)`     | `result<Http.Response>` | POST with custom headers                                             |
| `Http.put(url, body)`                 | `(string, string)`                         | `result<Http.Response>` | PUT request                                                          |
| `Http.put_with(url, body, headers)`   | `(string, string, dictionary<string>)`     | `result<Http.Response>` | PUT with body and custom headers                                     |
| `Http.request(opts, headers)`         | `(dictionary<string>, dictionary<string>)` | `result<Http.Response>` | Generic request (`opts` has `"method"` and `"url"` keys)             |
| `Http.request_of(method, url)`        | `(Http.Method, string)`                    | `Http.Request`          | Build a typed request (empty headers/body, default 30 s timeout)     |
| `Http.request_with(method, url, headers, body, timeout_ms)` | `(Http.Method, string, dictionary<string>, string, integer)` | `Http.Request` | Build a fully-specified typed request                    |
| `Http.send(request)`                  | `(Http.Request)`                           | `result<Http.Response>` | Perform a typed `Http.Request` and return the response               |
| `Http.status_class(status)`           | `(integer)`                                | `result<Http.StatusClass>` | Classify a status code into its family; fail if outside 100–599    |

`Http.Response` record fields: `status` (`integer`), `reason` (`string`), `body` (`string`), `headers` (`dictionary<string>`).

**`Http.Cookie`** is a flat record decoding a `Set-Cookie` header — `name: string`, `value: string`, `domain: string`, `path: string`, `expires: string`, `secure: boolean`, `http_only: boolean`. `Http.parse_cookie(header)` returns `result<Http.Cookie>` (the parse is lenient — unknown attributes are ignored — and fails only when the mandatory `name=value` pair is missing or its name is empty), and `Http.cookie_header(cookie)` formats a cookie back into a header string. A `Set-Cookie` value from `Http.Response.headers` is otherwise an opaque string a program must hand-split on `;` and `=`; this pair gives it the same structured treatment `Http.parse_url` gives a URL.

**`Http.MediaType`** is a record decoding a `Content-Type` header value — `type: string`, `subtype: string`, `parameters: dictionary<string>`. `Http.parse_media_type(header)` returns `result<Http.MediaType>`: `"text/html; charset=utf-8"` becomes `{ type = "text", subtype = "html", parameters = { charset = "utf-8" } }`. The `type`/`subtype` are lower-cased (RFC 9110 makes them case-insensitive) and parameter keys lower-cased, while parameter values keep their case (a quoted value like `boundary="a b c"` is unquoted); the parse is lenient on the parameter list but fails when the essential `type/subtype` form is absent. This gives the last stringly-typed response header the same structured treatment as `Http.parse_url` / `Http.parse_cookie`, so deciding "is this JSON?" (`mt.subtype == "json"`) or reading the charset no longer means manual string splitting.

```luma
Http.MediaType mt = Result.unwrap(Http.parse_media_type("application/json; charset=utf-8"))
boolean is_json = mt.type == "application" && mt.subtype == "json"
string charset = Dictionary.get_or(mt.parameters, "charset", "utf-8")
```

`Http.Request` record fields: `method` (`Http.Method`), `url` (`string`), `headers` (`dictionary<string>`), `body` (`string`), `timeout_ms` (`integer`). Unlike the `Http.request` options dictionary — which is homogeneous and so forces the verb to be stringified — an `Http.Request` carries the `Http.Method` choice natively, so the request is type-checked and discoverable and its method can be matched exhaustively. Build one with `Http.request_of` (common case) or `Http.request_with` (full control), then run it with `Http.send`:

```luma
Http.Request req = Http.request_of(Http.Method.Get, "http://example.com/api")
result<Http.Response> r = Http.send(req)
```

`Http.Method` is a choice type with variants `Get`, `Post`, `Put`, `Patch`, `Delete`, `Head`, and `Options` — a type-safe, match-exhaustive way to name an HTTP verb (autocomplete lists them all; a typo is a compile error). Because a Luma dictionary literal is homogeneous, an `Http.Method` value cannot be stored directly under the `"method"` key of `Http.request`'s options dictionary alongside the string `"url"`. Instead, convert it with `Http.method_to_string` so the options dictionary stays all-string:

```luma
result<Http.Response> r = Http.request(
    {"method": Http.method_to_string(Http.Method.Post), "url": u, "body": body},
    headers)
```

`Http.StatusClass` is a choice type with the five RFC 9110 status families as variants — `Informational` (1xx), `Success` (2xx), `Redirection` (3xx), `ClientError` (4xx), `ServerError` (5xx) — so a response can be branched on its family without hand-written magic ranges like `status >= 200 && status < 300`. `Http.status_class(status)` classifies a raw code and fails for a code outside 100–599; `Http.is_success(response)` is the common-case shortcut for "is this a 2xx?", equivalent to matching `Success`.

```luma
result<Http.Response> r = Http.get("http://example.com")
match r {
success(response) {
    match Result.unwrap(Http.status_class(response.status)) {
    case Http.StatusClass.Success     { print("ok") }
    case Http.StatusClass.ClientError { print("we sent something wrong") }
    case Http.StatusClass.ServerError { print("the server failed") }
    case _other                       { print("informational or redirect") }
    }
}
failure(_e) { print("request failed") }
}
```

`Http.Error` is a choice type classifying _why_ a request failed at the transport level — `InvalidUrl` (unsupported scheme, empty or malformed host), `ConnectionFailed` (DNS resolution, socket, or send failure), `Timeout` (the connection did not complete within the timeout), `TlsError` (a TLS handshake failure, or an `https://` request in a build without TLS support), `TooManyRedirects` (reserved for future redirect following — not currently produced), `Blocked` (an SSRF-guarded private/reserved/loopback target, or a CRLF-injected request), and `Malformed` (an empty or unparseable response). It is surfaced by `Http.get_typed(url)`, which returns `result<Http.Response, Http.Error>`: the value on success is the same `Http.Response` record `Http.get` returns, and the error on failure is the typed category, so a program can retry only on `Timeout`, fall back only on `ConnectionFailed`, or distinguish a `Blocked` URL from a `Malformed` response — instead of substring-matching an opaque message. This is an opt-in, additive companion (mirroring `FileSystem.read_file_typed` / `FileSystem.IoError`): the plain `Http.get` and every other `Http` request function keep their string-error `result<Http.Response>`.

```luma
match Http.get_typed("http://example.com/api") {
success(response) { print("got ${response.status}") }
failure(Http.Error.Timeout) { print("timed out — will retry") }
failure(Http.Error.ConnectionFailed) { print("connection failed — using cache") }
failure(Http.Error.Blocked) { print("refusing to fetch a blocked URL") }
failure(_other) { print("request failed") }
}
```

`Http.Auth` is a choice type modelling request credentials as a closed, exhaustive set: `Basic(username, password)` carries HTTP Basic credentials and `Bearer(token)` a bearer/OAuth token. `Http.authorization_header(auth)` renders it into the exact `Authorization` header value — `Basic(u, p)` becomes `"Basic " + base64("u:p")` and `Bearer(t)` becomes `"Bearer " + t` — so a scheme typo is a compile error instead of a hand-built `"Authrization"` header. It is the typed companion to the stringly-typed `Http.basic_auth` / `Http.bearer_auth` helpers (which remain), mirroring how `Http.method_to_string` renders an `Http.Method`:

```luma
Http.Auth auth = Http.Auth.Bearer("my-token")
result<Http.Response> r = Http.get_with(
    "http://example.com/api",
    {"Authorization": Http.authorization_header(auth)})
```

> **Security note** — HTTP header names and values are validated to reject carriage-return (`\r`) and line-feed (`\n`) characters. Supplying headers that contain these characters returns a `failure` result to prevent CRLF header injection.
> **Proxy support** — When the `HTTPS_PROXY`, `HTTP_PROXY`, or `ALL_PROXY` environment variables are set (lower-case variants are also honoured), requests are routed through the named HTTP proxy: `https` URLs use a `CONNECT` tunnel (TLS remains end-to-end with the origin server, so certificate verification is unaffected), and plain `http` URLs are forwarded with an absolute-form request line. `NO_PROXY` (comma-separated host or domain suffixes) bypasses the proxy for matching hosts. Proxy credentials supplied in the proxy URL's userinfo are sent via `Proxy-Authorization`. SSRF protection still applies to the request target: requests resolving to private, loopback, or otherwise reserved addresses are rejected even when a proxy is configured.

## 19 — Console

| Function                       | Parameter Types | Return Type       | Description                                                               |
| ------------------------------ | --------------- | ----------------- | ------------------------------------------------------------------------- |
| `Console.prompt(msg)`          | `(string)`      | `result<string>`  | Print prompt, read line from stdin; fail on EOF or if it exceeds the maximum string size |
| `Console.read_from_stdin()`    | `()`            | `result<string>`  | Read all of stdin; fail if it exceeds the maximum string size            |
| `Console.write_to_stderr(msg)` | `(string)`      | `result<boolean>` | Write to stderr                                                           |
| `Console.write_to_stdout(msg)` | `(string)`      | `result<boolean>` | Write to stdout                                                           |

> **Resource limit** — Console input is bounded by the maximum string size (see the [resource-limit table](Luma_Performance_Guide.md#6--resource-limits), `LUMA_LIMIT_MAX_STRING_SIZE`). `Console.prompt` and `Console.read_from_stdin` return `failure` if the input would exceed it.

> **Console vs FileSystem:** `Console` handles console I/O — reading from stdin and writing to stdout/stderr. `FileSystem` handles file content — reading, writing, and appending data — as well as file metadata and paths (checking existence, querying size, listing directories, copying, renaming, and manipulating path components). Use `Console` for interactive console I/O; use `FileSystem` to read, write, and manage files and directories.

## 20 — Json

Serialise and deserialise Luma values as JSON.

| Function                       | Parameter Types       | Return Type                    | Description                                                                      |
| ------------------------------ | --------------------- | ------------------------------ | -------------------------------------------------------------------------------- |
| `Json.as_array(v)`             | `(Json.Value)`        | `result<array<Json.Value>>`    | Extract the array payload; fail if `v` is not a `JsonArray`                       |
| `Json.as_boolean(v)`           | `(Json.Value)`        | `result<boolean>`              | Extract the boolean payload; fail if `v` is not a `JsonBool`                      |
| `Json.as_number(v)`            | `(Json.Value)`        | `result<number>`               | Extract the numeric payload; fail if `v` is not a `JsonNumber`                    |
| `Json.as_object(v)`            | `(Json.Value)`        | `result<dictionary<Json.Value>>` | Extract the object payload; fail if `v` is not a `JsonObject`                   |
| `Json.as_string(v)`            | `(Json.Value)`        | `result<string>`               | Extract the string payload; fail if `v` is not a `JsonString`                    |
| `Json.deserialize(s)`          | `(string)`            | `result<T>`                    | Parse JSON string                                                                |
| `Json.field(v, key)`           | `(Json.Value, string)` | `optional<Json.Value>`        | Look up a key in a `JsonObject`; `none` if absent or `v` is not an object        |
| `Json.get(json, path)`         | `(string, string)`    | `result<T>`                    | Navigate dot-separated path (e.g. `"user.age"`, `"items.0"`)                     |
| `Json.get_path(json, path)`    | `(string, string)`    | `result<T>`                    | Read a value using dot and `[index]` path syntax; fail if missing                |
| `Json.index(v, i)`             | `(Json.Value, integer)` | `optional<Json.Value>`       | Index into a `JsonArray`; `none` if out of bounds or `v` is not an array          |
| `Json.is_valid(s)`             | `(string)`            | `boolean`                      | Whether `s` is valid JSON                                                        |
| `Json.merge(a, b)`             | `(string, string)`    | `result<string>`               | Merge two JSON objects; `b` wins on conflicts                                    |
| `Json.parse(s)`                | `(string)`            | `result<Json.Value>`           | Parse a JSON string into the typed `Json.Value` ADT                              |
| `Json.parse_detailed(s)`       | `(string)`            | `result<Json.Value, Json.ParseError>` | Like `Json.parse`, but a failure carries the located `Json.ParseError`  |
| `Json.serialize(v)`            | `(T)`                 | `string`                       | Serialise value to compact JSON                                                  |
| `Json.serialize_pretty(v)`     | `(T)`                 | `string`                       | Serialise value to formatted JSON                                                |
| `Json.set(json, path, v)`      | `(string, string, T)` | `result<string>`               | Replace key at path; return new JSON string                                      |
| `Json.set_path(json, path, v)` | `(string, string, T)` | `result<string>`               | Replace the value at a dot/`[index]` path; return new JSON; fail on invalid path |
| `Json.to_string(v)`            | `(Json.Value)`        | `string`                       | Serialise a `Json.Value` back to a compact JSON string                           |

Supported types: `integer`, `number`, `string`, `boolean`, `none` (→ JSON `null`), `array`, and `dictionary`. Nested structures are handled recursively.

`Json.get` navigates a dot-separated path (e.g. `"user.address.city"`) into a parsed JSON string and returns the value wrapped in a `result`. Array elements are accessed by numeric index (e.g. `"items.0"`).

`Json.set` navigates to a key inside a JSON object string and returns a new serialised JSON string with that key replaced by the given value. The path must point to an existing key inside a JSON object.

`Json.merge` merges two JSON object strings; keys from the second object overwrite those in the first. Both inputs must be JSON objects.

### The typed `Json.Value` ADT

The functions above round-trip through dynamic values the type checker cannot see into. For code that must walk untrusted JSON in a type-safe, exhaustive way, `Json.Value` is a recursive choice type with six variants:

```luma
choice Json.Value {
    JsonObject(dictionary<Json.Value>)
    JsonArray(array<Json.Value>)
    JsonString(string)
    JsonNumber(number)
    JsonBool(boolean)
    JsonNull
}
```

`Json.parse(s)` returns a `result<Json.Value>`, so a `match` over the parsed value is exhaustive and autocompleted — "is this a string or an object?" becomes a compile-checked question rather than a stringly-typed guess. The `as_string` / `as_number` / `as_boolean` / `as_array` / `as_object` accessors extract a variant's payload as a `result` (failing on a type mismatch), while `field(v, key)` and `index(v, i)` return an `optional<Json.Value>` for safe navigation (`none` on a missing key, out-of-bounds index, or wrong container type). `Json.to_string(v)` serialises a `Json.Value` back to compact JSON, emitting object keys in sorted order and integral numbers without a trailing decimal. The existing dynamic `Json.deserialize` / `Json.get` / `Json.serialize` API is unchanged for simple cases.

```luma
function string summarise(Json.Value v) {
    return match v {
    case Json.Value.JsonObject(fields) { "object with ${Dictionary.length(fields)} keys" }
    case Json.Value.JsonArray(items)   { "array of ${Array.length(items)}" }
    case Json.Value.JsonString(s)      { "string: ${s}" }
    case Json.Value.JsonNumber(n)      { "number: ${n}" }
    case Json.Value.JsonBool(b)        { "bool: ${b}" }
    case Json.Value.JsonNull           { "null" }
    }
}

Json.Value doc = Result.unwrap(Json.parse("{\"user\": {\"age\": 30}}"))
match Json.field(doc, "user") {
case some(user) {
    match Json.field(user, "age") {
    case some(age_value) { print("age is ${Result.unwrap(Json.as_number(age_value))}") }
    case none            { print("no age") }
    }
}
case none { print("no user") }
}
```

`Json.parse_detailed(s)` is an additive companion to `Json.parse` for when malformed input must be diagnosed precisely. It leaves `Json.parse` unchanged and returns `result<Json.Value, Json.ParseError>`, where `Json.ParseError` is a record — `message: string`, `line: integer`, `column: integer` (both 1-based) — so a program can point at the exact byte in a large document rather than surface a bare string. The success payload is the same `Json.Value` tree that `Json.parse` produces.

```luma
match Json.parse_detailed(user_input) {
    success(value) { use(value) }
    failure(e)     { print("parse error at line ${e.line}, column ${e.column}: ${e.message}") }
}
```

## 21 — KeyValueStore

Persistent file-backed key-value store. Keys and values are strings. The store uses a tab-separated format with proper escaping. Mutation functions (`set`, `remove`, `set_many`, `clear`) return `result<key_value_store>` — `success` with a new copy of the store, or `failure` if the store is read-only.

| Function                                    | Parameter Types                         | Return Type               | Description                                       |
| ------------------------------------------- | --------------------------------------- | ------------------------- | ------------------------------------------------- |
| `KeyValueStore.clear(s)`                    | `(key_value_store)`                     | `result<key_value_store>` | Remove all entries; fail if read-only             |
| `KeyValueStore.count(s)`                    | `(key_value_store)`                     | `integer`                 | Number of entries                                 |
| `KeyValueStore.destroy(s)`                  | `(key_value_store)`                     | `result<string>`          | Delete backing file                               |
| `KeyValueStore.find_by_pattern(s, pattern)` | `(key_value_store, string)`             | `dictionary<string>`      | Entries whose keys match a glob pattern           |
| `KeyValueStore.get(s, key)`                 | `(key_value_store, string)`             | `result<string>`          | Lookup by key                                     |
| `KeyValueStore.get_many(s, keys)`           | `(key_value_store, array<string>)`      | `dictionary<string>`      | Lookup multiple keys                              |
| `KeyValueStore.has(s, key)`                 | `(key_value_store, string)`             | `boolean`                 | Whether key exists                                |
| `KeyValueStore.is_read_only(s)`             | `(key_value_store)`                     | `boolean`                 | Whether the store was opened read-only            |
| `KeyValueStore.keys(s)`                     | `(key_value_store)`                     | `array<string>`           | All keys                                          |
| `KeyValueStore.open(path)`                  | `(string)`                              | `result<key_value_store>` | Open or create a store file                       |
| `KeyValueStore.open_read_only(path)`        | `(string)`                              | `result<key_value_store>` | Open an existing store read-only; fail if missing |
| `KeyValueStore.reload(s)`                   | `(key_value_store)`                     | `result<key_value_store>` | Re-read from disk                                 |
| `KeyValueStore.remove(s, key)`              | `(key_value_store, string)`             | `result<key_value_store>` | Remove a key; fail if read-only                   |
| `KeyValueStore.save(s)`                     | `(key_value_store)`                     | `result<string>`          | Write to disk                                     |
| `KeyValueStore.set(s, key, value)`          | `(key_value_store, string, string)`     | `result<key_value_store>` | Set a key; fail if read-only                      |
| `KeyValueStore.set_many(s, entries)`        | `(key_value_store, dictionary<string>)` | `result<key_value_store>` | Batch set; fail if read-only                      |
| `KeyValueStore.to_dictionary(s)`            | `(key_value_store)`                     | `dictionary<string>`      | Convert to dictionary                             |
| `KeyValueStore.values(s)`                   | `(key_value_store)`                     | `array<string>`           | All values                                        |

## 22 — LinearAlgebra

Vector and matrix operations using arrays of numbers.

**Vectors** are `array<number>`. **Matrices** are `array<array<number>>`.

**Vector operations:**

| Function                            | Parameter Types                  | Return Type             | Description                                   |
| ----------------------------------- | -------------------------------- | ----------------------- | --------------------------------------------- |
| `LinearAlgebra.add(a, b)`           | `(array<number>, array<number>)` | `result<array<number>>` | Vector addition                               |
| `LinearAlgebra.angle(a, b)`         | `(array<number>, array<number>)` | `result<number>`        | Angle between vectors in radians              |
| `LinearAlgebra.cross(a, b)`         | `(array<number>, array<number>)` | `result<array<number>>` | Cross product (3D only)                       |
| `LinearAlgebra.dimension(v)`        | `(array<number>)`                | `integer`               | Number of elements                            |
| `LinearAlgebra.distance(a, b)`      | `(array<number>, array<number>)` | `result<number>`        | Euclidean distance; fail if dimensions differ |
| `LinearAlgebra.dot(a, b)`           | `(array<number>, array<number>)` | `result<number>`        | Dot product                                   |
| `LinearAlgebra.is_orthogonal(a, b)` | `(array<number>, array<number>)` | `boolean`               | Whether two vectors are orthogonal            |
| `LinearAlgebra.negate(v)`           | `(array<number>)`                | `array<number>`         | Negate all elements                           |
| `LinearAlgebra.norm(v)`             | `(array<number>)`                | `number`                | Euclidean norm                                |
| `LinearAlgebra.normalize(v)`        | `(array<number>)`                | `result<array<number>>` | Unit vector in same direction                 |
| `LinearAlgebra.scale(v, s)`         | `(array<number>, number)`        | `array<number>`         | Scalar multiplication                         |
| `LinearAlgebra.subtract(a, b)`      | `(array<number>, array<number>)` | `result<array<number>>` | Vector subtraction                            |
| `LinearAlgebra.unit_vector(n, i)`   | `(integer, integer)`             | `array<number>`         | Unit vector with 1 at index `i`               |
| `LinearAlgebra.zero_vector(n)`      | `(integer)`                      | `array<number>`         | Zero vector of dimension `n`                  |

**Matrix operations:**

| Function                              | Parameter Types                                | Return Type                    | Description                     |
| ------------------------------------- | ---------------------------------------------- | ------------------------------ | ------------------------------- |
| `LinearAlgebra.add_matrix(a, b)`      | `(array<array<number>>, array<array<number>>)` | `result<array<array<number>>>` | Element-wise matrix addition    |
| `LinearAlgebra.columns(m)`            | `(array<array<number>>)`                       | `integer`                      | Number of columns               |
| `LinearAlgebra.determinant(m)`        | `(array<array<number>>)`                       | `result<number>`               | Determinant                     |
| `LinearAlgebra.diagonal(v)`           | `(array<number>)`                              | `array<array<number>>`         | Diagonal matrix from vector     |
| `LinearAlgebra.identity(n)`           | `(integer)`                                    | `array<array<number>>`         | n×n identity matrix             |
| `LinearAlgebra.inverse(m)`            | `(array<array<number>>)`                       | `result<array<array<number>>>` | Matrix inverse                  |
| `LinearAlgebra.is_square(m)`          | `(array<array<number>>)`                       | `boolean`                      | Whether the matrix is square    |
| `LinearAlgebra.is_symmetric(m)`       | `(array<array<number>>)`                       | `boolean`                      | Whether the matrix is symmetric |
| `LinearAlgebra.multiply(a, b)`        | `(array<array<number>>, array<array<number>>)` | `result<array<array<number>>>` | Matrix multiplication           |
| `LinearAlgebra.multiply_vector(m, v)` | `(array<array<number>>, array<number>)`        | `result<array<number>>`        | Matrix–vector multiplication    |
| `LinearAlgebra.rows(m)`               | `(array<array<number>>)`                       | `integer`                      | Number of rows                  |
| `LinearAlgebra.scale_matrix(m, s)`    | `(array<array<number>>, number)`               | `array<array<number>>`         | Multiply all elements by scalar |
| `LinearAlgebra.shape(m)`              | `(array<array<number>>)`                       | `(integer, integer)`           | `(rows, columns)`               |
| `LinearAlgebra.solve(a, b)`           | `(array<array<number>>, array<number>)`        | `result<array<number>>`        | Solve Ax = b                    |
| `LinearAlgebra.trace(m)`              | `(array<array<number>>)`                       | `result<number>`               | Trace (sum of diagonal)         |
| `LinearAlgebra.transpose(m)`          | `(array<array<number>>)`                       | `array<array<number>>`         | Transpose matrix                |
| `LinearAlgebra.zero_matrix(r, c)`     | `(integer, integer)`                           | `array<array<number>>`         | r×c zero matrix                 |

## 23 — LinkedList

A doubly-linked list with O(1) prepend/append and O(n) indexed access. All operations are immutable — they return a new linked list.

| Function                          | Parameter Types                            | Return Type                          | Description                                                                  |
| --------------------------------- | ------------------------------------------ | ------------------------------------ | ---------------------------------------------------------------------------- |
| `LinkedList.append(ll, v)`        | `(linked_list, T)`                         | `linked_list`                        | Add to back                                                                  |
| `LinkedList.at(ll, i)`            | `(linked_list, integer)`                   | `result<T>`                          | Element at index; fail if out of bounds                                      |
| `LinkedList.concat(a, b)`         | `(linked_list, linked_list)`               | `linked_list`                        | Concatenate two lists                                                        |
| `LinkedList.contains(ll, v)`      | `(linked_list, T)`                         | `boolean`                            | Whether `v` is in the list                                                   |
| `LinkedList.each(ll, fn)`         | `(linked_list, function(T) -> none)`       | `result<none>`                       | Iterate for side effects; fail if callback throws                            |
| `LinkedList.filter(ll, fn)`       | `(linked_list, function(T) -> boolean)`    | `result<linked_list>`                | Keep matching elements; fail if callback throws                              |
| `LinkedList.find(ll, fn)`         | `(linked_list, function(T) -> boolean)`    | `result<T>`                          | First matching element; fail if not found                                    |
| `LinkedList.first(ll)`            | `(linked_list)`                            | `result<T>`                          | First element; fail if empty                                                 |
| `LinkedList.from_array(arr)`      | `(array<T>)`                               | `linked_list`                        | Create from array                                                            |
| `LinkedList.insert_at(ll, i, v)`  | `(linked_list, integer, T)`                | `result<linked_list>`                | Insert at index; fail if out of bounds                                       |
| `LinkedList.is_empty(ll)`         | `(linked_list)`                            | `boolean`                            | Whether the list is empty                                                    |
| `LinkedList.last(ll)`             | `(linked_list)`                            | `result<T>`                          | Last element; fail if empty                                                  |
| `LinkedList.length(ll)`           | `(linked_list)`                            | `integer`                            | Number of elements                                                           |
| `LinkedList.map(ll, fn)`          | `(linked_list, function(T) -> U)`          | `result<linked_list>`                | Transform each element; fail if callback throws                              |
| `LinkedList.new()`                | `()`                                       | `linked_list`                        | Empty linked list                                                            |
| `LinkedList.partition(ll, fn)`    | `(linked_list, function(T) -> boolean)`    | `result<(linked_list, linked_list)>` | Split into `(matches, rest)`; fail if predicate throws                       |
| `LinkedList.prepend(ll, v)`       | `(linked_list, T)`                         | `linked_list`                        | Add to front                                                                 |
| `LinkedList.push(ll, v)`          | `(linked_list, T)`                         | `linked_list`                        | Add to back (alias for `append`)                                             |
| `LinkedList.reduce(ll, init, fn)` | `(linked_list, U, function(U, T) -> U)`    | `result<U>`                          | Fold elements; fail if callback throws                                       |
| `LinkedList.remove_at(ll, i)`     | `(linked_list, integer)`                   | `result<linked_list>`                | Remove at index; fail if out of bounds                                       |
| `LinkedList.remove_first(ll)`     | `(linked_list)`                            | `result<linked_list>`                | Remove first element; fail if empty                                          |
| `LinkedList.remove_last(ll)`      | `(linked_list)`                            | `result<linked_list>`                | Remove last element; fail if empty                                           |
| `LinkedList.reverse(ll)`          | `(linked_list)`                            | `linked_list`                        | Reversed copy                                                                |
| `LinkedList.sort(ll, fn)`         | `(linked_list, function(T, T) -> boolean)` | `result<linked_list>`                | Sort by comparator (`true` = first arg comes first); fail if callback throws |
| `LinkedList.to_array(ll)`         | `(linked_list)`                            | `array<T>`                           | Convert to array                                                             |
| `LinkedList.unique(ll)`           | `(linked_list)`                            | `linked_list`                        | Remove duplicate elements                                                    |
| `LinkedList.zip(a, b)`            | `(linked_list, linked_list)`               | `linked_list`                        | Pair elements into tuples; truncate to shorter list                          |

> **Performance note:** LinkedList operations (`prepend`, `append`, `remove_at`, etc.) create a full deep copy of the list, making each mutation O(n). For performance-critical workloads with frequent mutations, consider using `Array` instead, which offers O(1) amortized `push` and `pop`.

## 24 — Log

Structured logging with configurable levels. Messages are written to stderr by default.

| Function                      | Parameter Types    | Return Type    | Description                                          |
| ----------------------------- | ------------------ | -------------- | ---------------------------------------------------- |
| `Log.clear_context()`         | `()`               | `none`         | Remove all context                                   |
| `Log.debug(msg)`              | `(string)`         | `none`         | Log at debug level                                   |
| `Log.error(msg)`              | `(string)`         | `none`         | Log at error level                                   |
| `Log.get_level()`             | `()`               | `Log.Level`    | Current log level                                    |
| `Log.info(msg)`               | `(string)`         | `none`         | Log at info level                                    |
| `Log.reset()`                 | `()`               | `none`         | Reset level, format, output, and context to defaults |
| `Log.set_context(key, value)` | `(string, string)` | `none`         | Add key–value context to log messages                |
| `Log.set_format(format)`      | `(string)`         | `none`         | Set custom format template                           |
| `Log.set_level(level)`        | `(Log.Level)`      | `none`         | Set minimum log level                                |
| `Log.set_output(target)`      | `(Log.Output \| string)` | `result<void>` | Redirect to a file, `Stderr`, or `Stdout`            |
| `Log.warn(msg)`               | `(string)`         | `none`         | Log at warn level                                    |

Levels are ordered: `Debug` < `Info` < `Warn` < `Error` < `Off`. The `Log.Level` choice type provides variants: `Log.Level.Debug`, `Log.Level.Info`, `Log.Level.Warn`, `Log.Level.Error`, `Log.Level.Off`. For convenience, `Log.set_level` also accepts lowercase strings (`"debug"`, `"info"`, `"warn"`, `"error"`, `"off"`).

`Log.set_output` accepts a file path or one of the special strings `"stderr"` (default) or `"stdout"`. When given a file path it appends to the file, creating it if it does not exist.

`Log.set_output` also accepts a `Log.Output` choice in place of the string, mirroring how `Log.set_level` accepts `Log.Level`. The choice has three variants: `Log.Output.Stderr`, `Log.Output.Stdout`, and `Log.Output.File(path: string)`. The typed form removes the ambiguity of the string overload — where a mistyped stream name such as `"stdrr"` is silently treated as a file path — because a stream and a file path are now distinct variants: `Log.set_output(Log.Output.File("app.log"))` can only mean a file.

## 25 — Math

| Function                              | Parameter Types                  | Return Type       | Description                                                                      |
| ------------------------------------- | -------------------------------- | ----------------- | -------------------------------------------------------------------------------- |
| `Math.absolute(x)`                    | `(integer \| number)`            | `result<integer \| number>` | Absolute value of `x`; fail on integer overflow                                  |
| `Math.approximately_equal(a, b)`      | `(number, number)`               | `boolean`         | Whether `a` and `b` are equal within a tolerance (default `1e-9`)                |
| `Math.approximately_equal(a, b, eps)` | `(number, number, number)`       | `boolean`         | Whether `a` and `b` are equal within `eps`                                       |
| `Math.arc_cosine(x)`                  | `(number)`                       | `result<number>`  | Inverse cosine; fail if `x` outside [−1, 1]                                      |
| `Math.arc_sine(x)`                    | `(number)`                       | `result<number>`  | Inverse sine; fail if `x` outside [−1, 1]                                        |
| `Math.arc_tangent(x)`                 | `(number)`                       | `result<number>`  | Inverse tangent                                                                  |
| `Math.atan2(y, x)`                    | `(number, number)`               | `result<number>`  | Arctangent of y/x considering signs; fail if result is NaN                       |
| `Math.ceil(x)`                        | `(number)`                       | `result<integer>` | Round up to nearest integer; fail on overflow                                    |
| `Math.combinations(n, k)`             | `(integer, integer)`             | `result<integer>` | Number of ways to choose `k` of `n` (nCr); fail if `n < 0`, `k < 0`, `k > n`, or overflow |
| `Math.complex(real, imaginary)`       | `(number, number)`               | `Math.Complex`    | Construct a complex number                                                        |
| `Math.complex_add(a, b)`              | `(Math.Complex, Math.Complex)`   | `Math.Complex`    | Sum `a + b`                                                                       |
| `Math.complex_argument(c)`            | `(Math.Complex)`                 | `number`          | Argument (phase angle) of `c` in radians                                          |
| `Math.complex_conjugate(c)`           | `(Math.Complex)`                 | `Math.Complex`    | Complex conjugate of `c`                                                          |
| `Math.complex_divide(a, b)`           | `(Math.Complex, Math.Complex)`   | `result<Math.Complex>` | Quotient `a / b`; fail if `b` is zero                                             |
| `Math.complex_from_polar(p)`          | `(Math.Polar)`                   | `Math.Complex`    | Convert a `Math.Polar` to a `Math.Complex` (total conversion)                    |
| `Math.complex_magnitude(c)`           | `(Math.Complex)`                 | `number`          | Magnitude (modulus) of `c`                                                        |
| `Math.complex_multiply(a, b)`         | `(Math.Complex, Math.Complex)`   | `Math.Complex`    | Product `a * b`                                                                   |
| `Math.complex_subtract(a, b)`         | `(Math.Complex, Math.Complex)`   | `Math.Complex`    | Difference `a - b`                                                                |
| `Math.complex_to_polar(c)`            | `(Math.Complex)`                 | `Math.Polar`      | Convert a `Math.Complex` to a `Math.Polar` (total conversion)                    |
| `Math.clamp(x, lo, hi)`               | `(number, number, number)`       | `result<number>`  | Clamp `x` to `[lo, hi]`; fail if `lo > hi`                                       |
| `Math.correlation(xs, ys)`            | `(array<number>, array<number>)` | `result<number>`  | Pearson correlation coefficient; fail if arrays differ in length or < 2 elements |
| `Math.cosine(x)`                      | `(number)`                       | `result<number>`  | Cosine; fail if result is NaN or infinite                                        |
| `Math.cube_root(x)`                   | `(number)`                       | `number`          | Cube root of `x`                                                                 |
| `Math.degrees(rad)`                   | `(number)`                       | `number`          | Convert radians to degrees                                                       |
| `Math.exponential(x)`                 | `(number)`                       | `result<number>`  | e^x                                                                              |
| `Math.factorial(n)`                   | `(integer)`                      | `result<integer>` | n!; fail if `n < 0` or `n > 20`                                                  |
| `Math.five_number_summary(arr)`       | `(array<number>)`                | `result<Math.FiveNumberSummary>` | Box-plot quartiles (min, Q1, median, Q3, max) in one pass; fail if empty |
| `Math.floor(x)`                       | `(number)`                       | `result<integer>` | Round down to nearest integer; fail on overflow                                  |
| `Math.fraction(numerator, denominator)` | `(integer, integer)`           | `result<Math.Fraction>` | Exact rational in lowest terms with a positive denominator; fail if `denominator` is 0 |
| `Math.fraction_add(a, b)`             | `(Math.Fraction, Math.Fraction)` | `Math.Fraction`   | Exact sum `a + b`; runtime error on int64 overflow                               |
| `Math.fraction_compare(a, b)`         | `(Math.Fraction, Math.Fraction)` | `Ordering`        | Order `a` against `b` exactly (`Less`, `Equal`, `Greater`)                        |
| `Math.fraction_divide(a, b)`          | `(Math.Fraction, Math.Fraction)` | `result<Math.Fraction>` | Exact quotient `a / b`; fail if `b` is zero                                       |
| `Math.fraction_multiply(a, b)`        | `(Math.Fraction, Math.Fraction)` | `Math.Fraction`   | Exact product `a * b`; runtime error on int64 overflow                           |
| `Math.fraction_subtract(a, b)`        | `(Math.Fraction, Math.Fraction)` | `Math.Fraction`   | Exact difference `a - b`; runtime error on int64 overflow                        |
| `Math.fraction_to_number(f)`          | `(Math.Fraction)`                | `number`          | Approximate `f` as a floating-point `number`                                     |
| `Math.from_polar(p)`                  | `(Math.Polar)`                   | `Math.Vector2`    | Convert a `Math.Polar` to a `Math.Vector2` (total conversion)                     |
| `Math.greatest_common_divisor(a, b)`  | `(integer, integer)`             | `result<integer>` | GCD of `a` and `b`                                                               |
| `Math.histogram(values, bins)`        | `(array<number>, integer)`       | `result<Math.Histogram>` | Bin `values` into `bins` equal-width half-open bins; fail if empty or `bins < 1` |
| `Math.hyperbolic_cosine(x)`           | `(number)`                       | `result<number>`  | Hyperbolic cosine; fail if result is infinite                                    |
| `Math.hyperbolic_sine(x)`             | `(number)`                       | `result<number>`  | Hyperbolic sine; fail if result is infinite                                      |
| `Math.hyperbolic_tangent(x)`          | `(number)`                       | `number`          | Hyperbolic tangent (always bounded to [−1, 1])                                   |
| `Math.hypot(x, y)`                    | `(number, number)`               | `number`          | Hypotenuse √(x² + y²)                                                            |
| `Math.is_infinite(x)`                 | `(number)`                       | `boolean`         | Whether `x` is +∞ or −∞                                                          |
| `Math.is_finite(x)`                   | `(number)`                       | `boolean`         | Whether `x` is neither ±∞ nor NaN                                                |
| `Math.is_even(n)`                     | `(integer)`                      | `boolean`         | Whether `n` is even (negative-safe)                                             |
| `Math.is_not_a_number(x)`             | `(number)`                       | `boolean`         | Whether `x` is NaN                                                               |
| `Math.is_odd(n)`                      | `(integer)`                      | `boolean`         | Whether `n` is odd (negative-safe)                                              |
| `Math.is_prime(n)`                    | `(integer)`                      | `boolean`         | Whether `n` is prime                                                             |
| `Math.least_common_multiple(a, b)`    | `(integer, integer)`             | `result<integer>` | LCM of `a` and `b`; fail on overflow                                             |
| `Math.lerp(a, b, t)`                  | `(number, number, number)`       | `result<number>`  | Linear interpolation; fail if `t` outside [0, 1]                                 |
| `Math.linear_fit(xs, ys)`             | `(array<number>, array<number>)` | `result<Math.LineFit>` | Ordinary least-squares line fit; fail on unequal lengths, < 2 points, or zero x-variance |
| `Math.log(base, value)`               | `(number, number)`               | `result<number>`  | Logarithm of `value` with `base`; fail if base ≤ 0, base = 1, or value ≤ 0       |
| `Math.log_10(x)`                      | `(number)`                       | `result<number>`  | Base-10 logarithm                                                                |
| `Math.log_2(x)`                       | `(number)`                       | `result<number>`  | Base-2 logarithm                                                                 |
| `Math.log_e(x)`                       | `(number)`                       | `result<number>`  | Natural logarithm                                                                |
| `Math.max(a, b)`                      | `(number, number)`               | `number`          | Larger of two values                                                             |
| `Math.mean(arr)`                      | `(array<number>)`                | `result<number>`  | Arithmetic mean; fail if empty                                                   |
| `Math.median(arr)`                    | `(array<number>)`                | `result<number>`  | Median value; fail if empty                                                      |
| `Math.min(a, b)`                      | `(number, number)`               | `number`          | Smaller of two values                                                            |
| `Math.mode(arr)`                      | `(array<number>)`                | `result<number>`  | Most frequent value; fail if empty                                               |
| `Math.percentile(arr, p)`             | `(array<number>, number)`        | `result<number>`  | p-th percentile; fail if empty or `p` outside [0, 100]                           |
| `Math.permutations(n, k)`             | `(integer, integer)`             | `result<integer>` | Number of ordered arrangements of `k` of `n` (nPr); fail if `n < 0`, `k < 0`, `k > n`, or overflow |
| `Math.power(base, exp)`               | `(number, number)`               | `result<number>`  | `base` raised to `exp`; fail if result is NaN or Inf                             |
| `Math.radians(deg)`                   | `(number)`                       | `number`          | Convert degrees to radians                                                       |
| `Math.remainder(a, b)`                | `(integer \| number, integer \| number)` | `result<integer \| number>` | Remainder of `a` divided by `b`; fail if `b` is zero                             |
| `Math.remap(value, in_min, in_max, out_min, out_max)` | `(number, number, number, number, number)` | `result<number>` | Linearly re-map `value` from input range to output range; fail if `in_min == in_max` |
| `Math.round(x)`                       | `(number)`                       | `result<integer>` | Round to nearest integer; fail on overflow                                       |
| `Math.round_to(x, places)`            | `(number, integer)`              | `result<number>`  | Round `x` to `places` decimal places; fail if `places` is negative or above 15   |
| `Math.sign(x)`                        | `(number)`                       | `integer`         | −1, 0, or 1                                                                      |
| `Math.sign_of(x)`                     | `(number)`                       | `Sign`            | Sign of `x` as a typed `Sign` choice (`Negative`, `Zero`, `Positive`)           |
| `Math.sin_of(angle)`                  | `(Math.Angle)`                   | `number`          | Sine of a unit-safe `Math.Angle` (converts to radians first)                    |
| `Math.sine(x)`                        | `(number)`                       | `result<number>`  | Sine; fail if result is NaN or infinite                                          |
| `Math.smooth_step(edge0, edge1, x)`   | `(number, number, number)`       | `result<number>`  | Smoothstep interpolation between `edge0` and `edge1`; fail if `edge0 == edge1`   |
| `Math.square_root(x)`                 | `(number)`                       | `result<number>`  | Square root; fail if `x` is negative                                             |
| `Math.standard_deviation(arr)`        | `(array<number>)`                | `result<number>`  | Standard deviation; fail if empty                                                |
| `Math.sum(arr)`                       | `(array<number>)`                | `result<integer \| number>` | Sum of all elements; fail on a non-numeric element                               |
| `Math.summarize(arr)`                 | `(array<number>)`                | `result<Math.Summary>` | Descriptive statistics (count, min, max, mean, median, std. dev.) in one pass; fail if empty |
| `Math.tangent(x)`                     | `(number)`                       | `result<number>`  | Tangent; fail if result is NaN or infinite                                       |
| `Math.to_degrees(angle)`              | `(Math.Angle)`                   | `number`          | The `Math.Angle` as a number of degrees                                         |
| `Math.to_polar(v)`                    | `(Math.Vector2)`                 | `Math.Polar`      | Convert a `Math.Vector2` to a `Math.Polar` (total conversion)                     |
| `Math.to_radians(angle)`              | `(Math.Angle)`                   | `number`          | The `Math.Angle` as a number of radians                                         |
| `Math.truncate(x)`                    | `(number)`                       | `result<integer>` | Truncate toward zero; fail on overflow                                           |
| `Math.variance(arr)`                  | `(array<number>)`                | `result<number>`  | Variance; fail if empty                                                          |
| `Math.vector2(x, y)`                  | `(number, number)`               | `Math.Vector2`    | Construct a 2D vector                                                            |
| `Math.vec2_add(a, b)`                 | `(Math.Vector2, Math.Vector2)`   | `Math.Vector2`    | Component-wise sum `a + b`                                                        |
| `Math.vec2_sub(a, b)`                 | `(Math.Vector2, Math.Vector2)`   | `Math.Vector2`    | Component-wise difference `a - b`                                                |
| `Math.vec2_scale(v, s)`               | `(Math.Vector2, number)`         | `Math.Vector2`    | Scale `v` by scalar `s`                                                          |
| `Math.vec2_dot(a, b)`                 | `(Math.Vector2, Math.Vector2)`   | `number`          | Dot product                                                                      |
| `Math.vec2_length(v)`                 | `(Math.Vector2)`                 | `number`          | Euclidean length                                                                 |
| `Math.vec2_normalize(v)`              | `(Math.Vector2)`                 | `Math.Vector2`    | Unit vector (the zero vector is returned unchanged)                             |
| `Math.vector3(x, y, z)`               | `(number, number, number)`       | `Math.Vector3`    | Construct a 3D vector                                                            |
| `Math.vec3_add(a, b)`                 | `(Math.Vector3, Math.Vector3)`   | `Math.Vector3`    | Component-wise sum `a + b`                                                        |
| `Math.vec3_sub(a, b)`                 | `(Math.Vector3, Math.Vector3)`   | `Math.Vector3`    | Component-wise difference `a - b`                                                |
| `Math.vec3_scale(v, s)`               | `(Math.Vector3, number)`         | `Math.Vector3`    | Scale `v` by scalar `s`                                                          |
| `Math.vec3_dot(a, b)`                 | `(Math.Vector3, Math.Vector3)`   | `number`          | Dot product                                                                      |
| `Math.vec3_cross(a, b)`               | `(Math.Vector3, Math.Vector3)`   | `Math.Vector3`    | Cross product `a × b`                                                            |
| `Math.vec3_length(v)`                 | `(Math.Vector3)`                 | `number`          | Euclidean length                                                                 |
| `Math.vec3_normalize(v)`              | `(Math.Vector3)`                 | `Math.Vector3`    | Unit vector (the zero vector is returned unchanged)                             |
| `Math.vector4(x, y, z, w)`            | `(number, number, number, number)` | `Math.Vector4`  | Construct a 4D (homogeneous) vector                                              |
| `Math.vec4_add(a, b)`                 | `(Math.Vector4, Math.Vector4)`   | `Math.Vector4`    | Component-wise sum `a + b`                                                        |
| `Math.vec4_sub(a, b)`                 | `(Math.Vector4, Math.Vector4)`   | `Math.Vector4`    | Component-wise difference `a - b`                                                |
| `Math.vec4_scale(v, s)`               | `(Math.Vector4, number)`         | `Math.Vector4`    | Scale `v` by scalar `s`                                                          |
| `Math.vec4_dot(a, b)`                 | `(Math.Vector4, Math.Vector4)`   | `number`          | Dot product                                                                      |
| `Math.vec4_length(v)`                 | `(Math.Vector4)`                 | `number`          | Euclidean length                                                                 |
| `Math.vec4_normalize(v)`              | `(Math.Vector4)`                 | `Math.Vector4`    | Unit vector (the zero vector is returned unchanged)                             |
| `Math.matrix2(m00, m01, m10, m11)`    | `(number, number, number, number)` | `Math.Matrix2`  | Construct a 2×2 matrix (row-major)                                              |
| `Math.mat2_identity()`                | `()`                             | `Math.Matrix2`    | The 2×2 identity matrix                                                          |
| `Math.mat2_multiply(a, b)`            | `(Math.Matrix2, Math.Matrix2)`   | `Math.Matrix2`    | Matrix product `a · b`                                                           |
| `Math.mat2_determinant(m)`            | `(Math.Matrix2)`                 | `number`          | Determinant of a 2×2 matrix                                                      |
| `Math.mat2_transform(m, v)`           | `(Math.Matrix2, Math.Vector2)`   | `Math.Vector2`    | Apply the transform `m · v`                                                      |
| `Math.matrix3(m00, …, m22)`           | `(number × 9)`                   | `Math.Matrix3`    | Construct a 3×3 matrix (row-major)                                              |
| `Math.mat3_identity()`                | `()`                             | `Math.Matrix3`    | The 3×3 identity matrix                                                          |
| `Math.mat3_multiply(a, b)`            | `(Math.Matrix3, Math.Matrix3)`   | `Math.Matrix3`    | Matrix product `a · b`                                                           |
| `Math.mat3_determinant(m)`            | `(Math.Matrix3)`                 | `number`          | Determinant of a 3×3 matrix                                                      |
| `Math.mat3_transform(m, v)`           | `(Math.Matrix3, Math.Vector3)`   | `Math.Vector3`    | Apply the transform `m · v`                                                      |
| `Math.matrix4(m00, …, m33)`           | `(number × 16)`                  | `Math.Matrix4`    | Construct a 4×4 matrix (row-major)                                              |
| `Math.mat4_identity()`                | `()`                             | `Math.Matrix4`    | The 4×4 identity matrix                                                          |
| `Math.mat4_multiply(a, b)`            | `(Math.Matrix4, Math.Matrix4)`   | `Math.Matrix4`    | Matrix product `a · b`                                                           |
| `Math.mat4_determinant(m)`            | `(Math.Matrix4)`                 | `number`          | Determinant of a 4×4 matrix                                                      |
| `Math.mat4_transform(m, v)`           | `(Math.Matrix4, Math.Vector4)`   | `Math.Vector4`    | Apply the transform `m · v` to a 4D vector                                       |
| `Math.mat4_transform_point(m, v)`     | `(Math.Matrix4, Math.Vector3)`   | `Math.Vector3`    | Transform a 3D point as homogeneous `(x, y, z, 1)`, dividing by the result `w`   |
| `Math.mat4_perspective(fov_y, aspect, near, far)` | `(number, number, number, number)` | `Math.Matrix4` | Right-handed perspective projection (`fov_y` in radians)                |
| `Math.mat4_look_at(eye, center, up)`  | `(Math.Vector3, Math.Vector3, Math.Vector3)` | `Math.Matrix4` | Right-handed look-at view matrix                                    |
| `Math.quaternion(w, x, y, z)`         | `(number, number, number, number)` | `Math.Quaternion` | Construct a quaternion from its components                                     |
| `Math.quat_from_axis_angle(axis, angle)` | `(Math.Vector3, number)`      | `Math.Quaternion` | Unit rotation quaternion about `axis` by `angle` radians (axis is normalised)   |
| `Math.quat_multiply(a, b)`            | `(Math.Quaternion, Math.Quaternion)` | `Math.Quaternion` | Compose two rotations (Hamilton product `a · b`)                            |
| `Math.quat_normalize(q)`              | `(Math.Quaternion)`              | `Math.Quaternion` | Unit quaternion (the zero quaternion is returned unchanged)                     |
| `Math.quat_rotate_vector(q, v)`       | `(Math.Quaternion, Math.Vector3)` | `Math.Vector3`   | Rotate `v` by `q` (`q` is normalised first)                                     |
| `Math.interval(min, max)`             | `(number, number)`               | `result<Math.Interval>` | Build a closed numeric interval; fail if `max < min`                      |
| `Math.interval_contains(iv, x)`       | `(Math.Interval, number)`        | `boolean`         | Whether `x` lies within the closed interval                                     |
| `Math.interval_clamp(iv, x)`          | `(Math.Interval, number)`        | `number`          | Clamp `x` into `[min, max]`                                                      |
| `Math.interval_length(iv)`            | `(Math.Interval)`                | `number`          | Interval width (`max - min`)                                                     |
| `Math.intervals_overlap(a, b)`        | `(Math.Interval, Math.Interval)` | `boolean`         | Whether two closed intervals overlap (touching counts)                          |
| `Math.rect(x, y, width, height)`      | `(number, number, number, number)` | `Math.Rect`     | Build an axis-aligned rectangle (negative extents clamp to 0)                   |
| `Math.rect_contains(r, x, y)`         | `(Math.Rect, number, number)`    | `boolean`         | Whether point `(x, y)` lies in `r` (half-open: min edges in, max edges out)      |
| `Math.rect_intersects(a, b)`          | `(Math.Rect, Math.Rect)`         | `boolean`         | Whether two rectangles overlap (touching edges do not count)                    |
| `Math.rect_intersection(a, b)`        | `(Math.Rect, Math.Rect)`         | `optional<Math.Rect>` | The overlapping rectangle, or `none` when they are disjoint                  |
| `Math.rect_union(a, b)`               | `(Math.Rect, Math.Rect)`         | `Math.Rect`       | The smallest rectangle containing both                                          |
| `Math.rect_center(r)`                 | `(Math.Rect)`                    | `Math.Vector2`    | The centre point of the rectangle                                               |
| `Math.rect_area(r)`                   | `(Math.Rect)`                    | `number`          | Area (`width × height`)                                                          |
| `Math.circle(center, radius)`         | `(Math.Vector2, number)`         | `Math.Circle`     | Build a circle (a negative radius clamps to 0)                                   |
| `Math.circle_contains(c, point)`      | `(Math.Circle, Math.Vector2)`    | `boolean`         | Whether `point` lies in the closed disk (the boundary is inclusive)             |
| `Math.circle_intersects(a, b)`        | `(Math.Circle, Math.Circle)`     | `boolean`         | Whether two circles overlap or touch                                            |
| `Math.circle_rect_intersects(c, r)`   | `(Math.Circle, Math.Rect)`       | `boolean`         | Whether a circle and a rectangle overlap                                        |

`Math.Summary` record fields: `count: integer`, `minimum: number`, `maximum: number`, `mean: number`, `median: number`, `standard_deviation: number` (population standard deviation).

`Math.FiveNumberSummary` is the box-plot sibling of `Math.Summary` — `minimum: number`, `q1: number`, `median: number`, `q3: number`, `maximum: number` — returned by `Math.five_number_summary(arr)` (fails on an empty array). The quartiles use the same linear-interpolation method as `Math.percentile`, so `Math.five_number_summary(v)` agrees with `Math.percentile(v, 25/50/75)` — a single typed answer for the five order statistics a box plot needs.

`Math.Histogram { bin_edges: array<number>, counts: array<integer>, bin_width: number }` is the binned frequency distribution behind every bar chart. `Math.histogram(values, bins)` splits the data range `[min, max]` into `bins` equal-width half-open bins and tallies how many samples fall in each — `counts[i]` is the number of values in `[bin_edges[i], bin_edges[i+1])`, so `bin_edges` always has one more element than `counts`, and the final bin is closed on the right so the maximum is counted. It fails on an empty array or `bins < 1`. When every value is identical (a zero-width range) the range is widened by half a unit on each side so the bins stay positive-width. The `integer` counts and `number` edges respect the numeric convention, and the shape feeds the GraphicalUi bar chart directly. Mirrors `Math.summarize` / `Math.five_number_summary`: pure data returned by one pipe-first `result`-typed call.

```luma
Math.Histogram h = Result.unwrap(Math.histogram([0.0, 1.0, 2.0, 3.0, 4.0, 5.0], 3))
assert(Array.length(h.bin_edges) == 4)   # one more edge than counts
assert(Array.length(h.counts) == 3)
```

`Math.Vector2 { x: number, y: number }` and `Math.Vector3 { x: number, y: number, z: number }` are typed geometry vectors for 2D/3D work — game positions, GraphicalUi layout, physics — where named `.x` / `.y` / `.z` components are far more teachable than the index arithmetic of `LinearAlgebra`'s general `array<number>` vectors. Like `Math.Complex`, they are pure data plus a pipe-first free-function family — `vec2_*` / `vec3_*` for `add`, `sub`, `scale`, `dot`, `length`, and `normalize`, with `vec3_cross` for the 3D cross product — and no operator overloading. `normalize` returns the zero vector unchanged rather than dividing by zero. `Math.to_polar` / `Math.from_polar` bridge `Math.Vector2` to the `Math.Polar` record below.

`Math.Vector4 { x, y, z, w }` (all `number`) completes the fixed-size vector ladder: the extra `w` component carries the homogeneous coordinate that full 3D transforms (`Math.Matrix4`) need, so a point is `(x, y, z, 1.0)` and a direction is `(x, y, z, 0.0)`. It has the same pipe-first `vec4_*` family — `add`, `sub`, `scale`, `dot`, `length`, `normalize` (there is no 4D cross product) — with the same zero-vector leniency in `normalize`.

`Math.Matrix2 { m00, m01, m10, m11 }` and `Math.Matrix3 { m00 … m22 }` (all `number`, row-major) are the typed transform-matrix companions to the vectors, for the 2×2/3×3 linear transforms that `LinearAlgebra`'s general `array<array<number>>` expresses only through index arithmetic. Build them with `Math.matrix2` / `Math.matrix3` or the ready-made `Math.mat2_identity` / `Math.mat3_identity`; `mat*_multiply` composes transforms, `mat*_determinant` reports the scale factor, and `mat2_transform` / `mat3_transform` apply a matrix to a `Math.Vector2` / `Math.Vector3`. Data plus free functions, no operator overloading — the same philosophy as the vectors. `LinearAlgebra` remains for general N-dimensional work.

`Math.Matrix4 { m00 … m33 }` (all `number`, row-major) is the 4×4 homogeneous-transform companion — the full model / view / projection matrix of 3D graphics that the smaller matrices cannot express. Build one with `Math.matrix4`, the ready-made `Math.mat4_identity`, or the two camera constructors: `Math.mat4_perspective(fov_y, aspect, near, far)` (a right-handed perspective projection, `fov_y` in radians, OpenGL clip-space convention) and `Math.mat4_look_at(eye, center, up)` (a right-handed view matrix, all three arguments `Math.Vector3`). `Math.mat4_multiply(a, b)` composes transforms (`a · b`, so the rightmost is applied first) and `Math.mat4_determinant(m)` reports the scale factor. Apply a matrix with either `Math.mat4_transform(m, v)` — the direct `m · v` on a `Math.Vector4` — or `Math.mat4_transform_point(m, v)`, which transforms a `Math.Vector3` point as `(x, y, z, 1.0)` and divides the result by its `w` so a perspective matrix yields the projected point. Degenerate inputs (a zero `w`, `near == far`, an `eye == center` look-at) fail safe — the divide is skipped or the identity is returned — rather than producing `NaN`. Data plus pipe-first free functions, the same philosophy as `Math.Matrix3`.

```luma
# Project a world-space point through a camera.
Math.Matrix4 view = Math.mat4_look_at(Math.vector3(0.0, 0.0, 5.0), Math.vector3(0.0, 0.0, 0.0), Math.vector3(0.0, 1.0, 0.0))
Math.Matrix4 projection = Math.mat4_perspective(Math.pi / 2.0, 1.0, 1.0, 100.0)
Math.Matrix4 view_projection = Math.mat4_multiply(projection, view)
Math.Vector3 screen = Math.mat4_transform_point(view_projection, Math.vector3(0.0, 0.0, 0.0))
```

`Math.Quaternion { w, x, y, z }` (all `number`) is the gimbal-lock-free 3D-rotation companion to the vectors and matrices, for composing and applying rotations without hand-building rotation matrices. `Math.quaternion(w, x, y, z)` builds one from raw components, but the everyday constructor is `Math.quat_from_axis_angle(axis, angle)`, which produces a unit rotation of `angle` radians about `axis` (a `Math.Vector3`, normalised for you). `Math.quat_multiply(a, b)` composes two rotations (the Hamilton product — order matters), `Math.quat_normalize(q)` renormalises a drifted quaternion (a zero quaternion is returned unchanged, mirroring `vec3_normalize`), and `Math.quat_rotate_vector(q, v)` rotates a `Math.Vector3` by `q` (normalising `q` first, so a slightly denormalised rotation still behaves). Data plus pipe-first free functions, the same philosophy as `Math.Vector3` / `Math.Matrix3`.

```luma
# Rotate the unit-x vector 90° about the Z axis → the unit-y vector.
Math.Quaternion spin = Math.quat_from_axis_angle(Math.vector3(0.0, 0.0, 1.0), Math.pi / 2.0)
Math.Vector3 rotated = Math.quat_rotate_vector(spin, Math.vector3(1.0, 0.0, 0.0))
assert(Math.approximately_equal(rotated.y, 1.0, 0.001))
```

`Math.Interval { min: number, max: number }` is a closed numeric range — the general-purpose sibling of `DateTime.Interval`. `Math.interval(min, max)` is a validating constructor that fails when `max < min`, so an `Interval` is always well-formed. It replaces hand-rolled `x >= lo && x <= hi` with the teachable `Math.interval_contains` (closed, so both endpoints count), and adds `Math.interval_clamp` (bound `x` into the range), `Math.interval_length` (`max - min`), and `Math.intervals_overlap` (closed, so touching endpoints count).

`Math.Rect { x: number, y: number, width: number, height: number }` is an axis-aligned rectangle — the 2D analogue of `Math.Interval` for layout, hit-testing, collision, and cropping, where named `.x` / `.y` / `.width` / `.height` are far more teachable than four loose numbers and hand-written overlap arithmetic. `Math.rect(x, y, width, height)` is a total constructor that clamps negative extents to 0 (so a degenerate rectangle is empty rather than inside-out). Edges are half-open (`[x, x+width)`), consistent with DOM hit-testing: `Math.rect_contains(r, x, y)` counts the min edges as inside and the max edges as outside, and `Math.rect_intersects(a, b)` treats merely-touching edges as non-overlapping. The one fallible operation, `Math.rect_intersection(a, b)`, returns `optional<Math.Rect>` — `none` when the rectangles are disjoint. `Math.rect_union` returns the bounding rectangle of both, `Math.rect_center` returns the centre as a `Math.Vector2`, and `Math.rect_area` returns `width × height`. Pure data plus free functions, mirroring `Math.Vector2`.

```luma
Math.Rect a = Math.rect(0.0, 0.0, 10.0, 10.0)
Math.Rect b = Math.rect(5.0, 5.0, 10.0, 10.0)
match Math.rect_intersection(a, b) {
    case some(overlap) { print("overlap area is ${Math.rect_area(overlap)}") }   # 25.0
    case none { print("disjoint") }
}
```

`Math.Circle { center: Math.Vector2, radius: number }` is a 2D circle — the disk companion to `Math.Rect`, reusing `Math.Vector2` for its centre. `Math.circle(center, radius)` is a total constructor that clamps a negative radius to 0 (so a degenerate circle is a point rather than inside-out). All predicates use inclusive (closed-disk) boundaries: `Math.circle_contains(c, point)` is true when a point lies within or exactly on the boundary, `Math.circle_intersects(a, b)` is true when two circles overlap or merely touch, and `Math.circle_rect_intersects(c, rect)` is true when a circle overlaps an axis-aligned `Math.Rect` (comparing the circle centre against the closest point on the rectangle). They give a beginner the two most common collision tests after rectangles without hand-writing the distance-squared comparison. Pure data plus total boolean predicates, mirroring `Math.Rect`. Note that the circle predicates are inclusive whereas the `Math.Rect` predicates are half-open, so the two families disagree on an exactly-touching edge (a point on a circle's boundary is contained, but a point on a rectangle's max edge is not).

```luma
Math.Circle c = Math.circle(Math.vector2(0.0, 0.0), 5.0)
print(Math.circle_contains(c, Math.vector2(3.0, 4.0)))                    # true (on the boundary)
print(Math.circle_intersects(c, Math.circle(Math.vector2(9.0, 0.0), 5.0)))   # true
print(Math.circle_rect_intersects(c, Math.rect(4.0, 4.0, 2.0, 2.0)))     # false
```

`Math.Fraction` is a record of exact rational numbers — `numerator: integer` and `denominator: integer` — always stored in lowest terms with a strictly positive denominator (the sign lives in the numerator, and zero is stored as `0/1`). Unlike `number`, a fraction never loses precision, so `1/3 + 1/6` is exactly `1/2`; unlike `Decimal` (base-10), it represents thirds exactly. Like `Decimal`, the type avoids operator overloading: build values with `Math.fraction(numerator, denominator)` (a validating constructor that fails on a zero denominator) and combine them with the `Math.fraction_*` free functions. `add`/`subtract`/`multiply` return a `Math.Fraction` directly and raise a catchable runtime error on int64 overflow (mirroring native integer `+`), while `divide` returns `result<Math.Fraction>` and fails on division by a zero fraction. `Math.fraction_compare` returns the top-level `Ordering` choice for an exhaustive `match`.

```luma
Math.Fraction a = Result.unwrap(Math.fraction(1, 3))
Math.Fraction b = Result.unwrap(Math.fraction(1, 6))
Math.Fraction sum = Math.fraction_add(a, b)   # exactly 1/2
assert(sum.numerator == 1 && sum.denominator == 2)
```

`Math.Complex` is a record of complex numbers — `real: number` and `imaginary: number` — for the quadratic formula with a negative discriminant, signal work, and Argand-plane maths. Like `Decimal` and `Math.Fraction`, it avoids operator overloading: build values with `Math.complex(real, imaginary)` and combine them with the `Math.complex_*` free functions. `add`/`subtract`/`multiply`/`conjugate` return a `Math.Complex`; `divide` returns `result<Math.Complex>` and fails when the divisor is `0 + 0i`. `Math.complex_magnitude` and `Math.complex_argument` return the modulus and phase angle (radians). `Math.complex_to_polar` / `Math.complex_from_polar` bridge to the `Math.Polar` record below.

`Math.Polar { radius: number, angle: number }` (`angle` in radians) is the polar-coordinate counterpart of `Math.Vector2` and `Math.Complex` — the natural representation for rotations, orbits, and anything phrased as "how far, which way" rather than "how far right, how far up". `Math.to_polar(v)` / `Math.from_polar(p)` convert to and from `Math.Vector2`, and `Math.complex_to_polar(c)` / `Math.complex_from_polar(p)` do the same for `Math.Complex` — all four are **total conversions with no error case**: every Cartesian value has a polar form (the origin maps to `radius: 0.0, angle: 0.0`) and every polar value has a Cartesian one, so none of them return a `result`.

```luma
Math.Polar p = Math.to_polar(Math.vector2(3.0, 4.0))   # radius: 5.0, angle: atan2(4, 3)
Math.Vector2 v = Math.from_polar(p)                    # back to (3.0, 4.0)
```

`Math.LineFit` is the ordinary least-squares regression result — `slope: number`, `intercept: number`, `r_squared: number` — returned by `Math.linear_fit(xs, ys)` for the trend line `y = slope · x + intercept`. It fails on mismatched array lengths, fewer than two points, or a zero x-variance (a vertical line). Mirrors `Math.Summary`: a plain returned record built by a single call.

`Sign` is a **top-level** choice type (not namespaced, like `Ordering`) with three variants — `Sign.Negative`, `Sign.Zero`, `Sign.Positive` — the self-documenting answer to "which way does this number point?". `Math.sign_of(x)` returns it, so a `match` is exhaustive and autocompleted instead of comparing against the magic `-1` / `0` / `1` that `Math.sign` returns (where `-1` could be misread as an error sentinel). `Math.sign` is unchanged for callers that want the integer.

`Math.Angle` is an **optional** unit-safe angle: a payload-carrying choice with two variants — `Math.Angle.Radians(number)` and `Math.Angle.Degrees(number)` — that makes the radians-versus-degrees distinction explicit at the call site, so mixing the two becomes a visible choice rather than a silent bug. `Math.to_radians(angle)` and `Math.to_degrees(angle)` convert to a bare `number` in either unit, and `Math.sin_of(angle)` takes the sine of an angle regardless of how it was expressed. This is a **convenience, not a replacement**: the primary trig APIs (`Math.sine`, `Math.cosine`, `Math.radians`, `Math.degrees`) still take and return bare number radians, so reach for `Math.Angle` only where the extra clarity is worth the wrapper.

```luma
string direction = match Math.sign_of(-4.2) {
case Sign.Negative { "falling" }
case Sign.Zero     { "flat" }
case Sign.Positive { "rising" }
}                                    # "falling" — exhaustive, no else needed
```

**Constants:**

| Constant        | Type      | Value                      |
| --------------- | --------- | -------------------------- |
| `Math.e`        | `number`  | 2.718281828459045          |
| `Math.pi`       | `number`  | 3.141592653589793          |
| `Math.tau`      | `number`  | 6.283185307179586          |
| `Math.infinity` | `number`  | ∞                          |
| `Math.nan`      | `number`  | quiet NaN (test with `Math.is_not_a_number`, never `==`) |
| `Math.epsilon`  | `number`  | 2.220446049250313e-16 (machine epsilon; the default tolerance to reach for with `Math.approximately_equal`) |
| `Math.max_integer` | `integer` | 9223372036854775807 (2⁶³−1) |
| `Math.min_integer` | `integer` | −9223372036854775808 (−2⁶³) |

## 26 — Optional

Functions for working with `optional<T>` values. All functions are available as `Optional.function_name(...)` without a `use` declaration.

| Function                         | Parameter Types                             | Return Type        | Description                                                    |
| -------------------------------- | ------------------------------------------- | ------------------ | -------------------------------------------------------------- |
| `Optional.filter(o, fn)`         | `(optional<T>, function(T) -> boolean)`     | `optional<T>`      | Keep the value only if `fn(value)` is `true`; otherwise `none` |
| `Optional.flat_map(o, fn)`       | `(optional<T>, function(T) -> optional<U>)` | `optional<U>`      | Apply `fn` returning `optional<U>`; propagate `none`           |
| `Optional.flatten(o)`            | `(optional<optional<T>>)`                   | `optional<T>`      | Unwrap nested optional; `none` if outer or inner is `none`     |
| `Optional.is_none(o)`            | `(optional<T>)`                             | `boolean`          | Whether the optional is `none`                                 |
| `Optional.is_some(o)`            | `(optional<T>)`                             | `boolean`          | Whether the optional holds a value                             |
| `Optional.map(o, fn)`            | `(optional<T>, function(T) -> U)`           | `optional<U>`      | Apply `fn` to the value; propagate `none`                      |
| `Optional.or(o, fallback)`       | `(optional<T>, optional<T>)`                | `optional<T>`      | Return `o` if it holds a value, otherwise `fallback`           |
| `Optional.tap(o, fn)`            | `(optional<T>, function(T) -> none)`        | `optional<T>`      | Call `fn(value)` as side effect if some; return `o` unchanged  |
| `Optional.to_result(o, msg)`     | `(optional<T>, string)`                     | `result<T>`        | Convert to `success(value)` or `failure(msg)` if `none`        |
| `Optional.unwrap(o)`             | `(optional<T>)`                             | `T`                | Return inner value; runtime error if `none`                    |
| `Optional.unwrap_or(o, default)` | `(optional<T>, T)`                          | `T`                | Return inner value or `default` if `none`                      |
| `Optional.zip(a, b)`             | `(optional<A>, optional<B>)`                | `optional<(A, B)>` | Both some → tuple; either none → `none`                        |
| `Optional.and_then(o, fn)`       | `(optional<T>, function(T) -> optional<U>)` | `optional<U>`      | Apply `fn` returning `optional<U>` if some; alias for `flat_map` |
| `Optional.contains(o, value)`    | `(optional<T>, T)`                          | `boolean`          | Whether the optional holds a value equal to `value`            |

```luma
optional<integer> x = some(42)
optional<integer> n = none

boolean a = Optional.is_some(x)      # true
boolean b = Optional.is_none(n)      # true

integer v = Optional.unwrap(x)       # 42
integer d = Optional.unwrap_or(n, 0) # 0

optional<string> s = Optional.map(x, (integer i) -> "value: ${i}") # some("value: 42")

optional<integer> f = Optional.filter(x, (integer i) -> i > 100) # none — 42 is not > 100

optional<optional<integer>> nested = some(some(10))
optional<integer> flat = Optional.flatten(nested) # some(10)

result<integer> r = Optional.to_result(n, "missing value") # failure("missing value")

(integer a, integer b) = Optional.zip(some(1), some(2)) # (1, 2) — both some → tuple
```

The pipe operator works naturally with Optional functions:

```luma
string label = some(42)
    |> Optional.filter((integer i) -> i > 0)
    |> Optional.map((integer i) -> "positive: ${i}")
    |> Optional.unwrap_or("not positive")

print(label) # "positive: 42"
```

## 27 — Order

Comparison utilities built around the `Ordering` choice type, a self-documenting
alternative to raw `-1` / `0` / `1` comparison numbers. All functions are available
as `Order.function_name(...)` without a `use` declaration.

The `Ordering` choice type is **top-level** (not namespaced) and provides three
variants: `Ordering.Less`, `Ordering.Equal`, `Ordering.Greater`. A `match` over an
`Ordering` is exhaustive and autocompleted, so a mistyped or forgotten case becomes
a compile error rather than a sign-convention bug ("does negative mean first?").

| Function                | Parameter Types      | Return Type | Description                                                       |
| ----------------------- | -------------------- | ----------- | ---------------------------------------------------------------- |
| `Order.of(a, b)`        | `(any, any)`         | `Ordering`  | Compare two comparable primitives; runtime error if incomparable |
| `Order.reverse(o)`      | `(Ordering)`         | `Ordering`  | Flip `Less` ↔ `Greater`; `Equal` unchanged                       |
| `Order.then(first, second)` | `(Ordering, Ordering)` | `Ordering` | Return `first` unless it is `Equal`, then `second` (tie-break) |
| `Order.to_number(o)`    | `(Ordering)`         | `number`    | Bridge to the numeric comparator: `-1.0` / `0.0` / `1.0`         |
| `Order.from_number(n)`  | `(number)`           | `Ordering`  | Bridge from a negative / zero / positive comparator result       |

`Order.of` compares strings, integers, numbers (with integer→number promotion), and
booleans (`false` < `true`). Comparing values of incompatible types — or a `NaN` — is
a programmer error and raises a runtime error, exactly like the numeric comparators it
wraps.

```luma
Ordering c = Order.of(1, 2)          # Ordering.Less
Ordering r = Order.reverse(c)        # Ordering.Greater

string label = match Order.of(3, 3) {
case Ordering.Less    { "before" }
case Ordering.Equal   { "same" }
case Ordering.Greater { "after" }
}                                    # "same" — exhaustive, no else needed
```

The `to_number` / `from_number` bridges let the typed `Ordering` interoperate with the
existing numeric comparator that `Array.sort` expects, which shines for multi-key sorts.
`Order.then` chains comparisons so "sort by last name, then first name" reads directly:

```luma
record Person { string last, string first }

array<Person> people = [
    Person { last = "Smith", first = "Zoe" },
    Person { last = "Jones", first = "Alice" },
    Person { last = "Smith", first = "Adam" }
]

array<Person> sorted = Result.unwrap(
    Array.sort(people, (Person a, Person b) -> Order.to_number(
        Order.then(Order.of(a.last, b.last), Order.of(a.first, b.first))
    ))
)
# Jones/Alice, Smith/Adam, Smith/Zoe
```

The existing numeric comparator convention (a `function(T, T) -> number` returning a
negative, zero, or positive value) still works unchanged; `Order` is purely additive.

## 28 — Process

| Function                                        | Parameter Types    | Return Type                     | Description                                                              |
| ----------------------------------------------- | ------------------ | ------------------------------- | ------------------------------------------------------------------------ |
| `Process.current_directory()`                   | `()`               | `result<string>`                | Current working directory                                                |
| `Process.execute(cmd)`                          | `(string)`         | `result<Process.CommandOutput>` | Execute shell command, capturing stdout **and** stderr separately        |
| `Process.exit(code)`                            | `(integer)`        | `none`                          | Terminate the program with exit code                                     |
| `Process.exit_status(output)`                   | `(Process.CommandOutput)` | `Process.ExitStatus`     | Classify a `CommandOutput`'s `exit_code` sign convention                 |
| `Process.get_all_environment_variables()`       | `()`               | `dictionary<string>`            | All environment variables as a dictionary                                |
| `Process.command(program, arguments)`           | `(string, array<string>)` | `Process.Command`        | Build a shell-free command (explicit program + argument vector)          |
| `Process.run_command(cmd)`                      | `(Process.Command)` | `result<Process.CommandOutput>` | Run a `Process.Command` directly (no shell); metacharacters are inert    |
| `Process.run_command_typed(cmd)`                | `(Process.Command)` | `result<Process.CommandOutput, Process.Error>` | Like `run_command`; on a launch failure the error is a typed `Process.Error` |
| `Process.get_arguments()`                       | `()`               | `array<string>`                 | Command-line arguments after the file name                               |
| `Process.get_environment_variable(name)`        | `(string)`         | `result<string>`                | Environment variable value; fail if not set                              |
| `Process.get_process_id()`                      | `()`               | `integer`                       | Current process ID                                                       |
| `Process.has_environment_variable(name)`        | `(string)`         | `boolean`                       | Whether the environment variable is set                                  |
| `Process.run(cmd)`                              | `(string)`         | `result<Process.ProcessResult>` | Execute shell command and capture stdout                                 |
| `Process.set_environment_variable(name, value)` | `(string, string)` | `result<none>`                  | Set environment variable; fail if name/value exceeds 32 KB or OS rejects |
| `Process.signal(pid, signal)`                   | `(integer, Process.Signal)` | `result<boolean>`      | Send a `Process.Signal` to another process by pid; fail if the OS rejects the request |

> **Security warning:** `Process.run` passes its argument to the system shell (`cmd.exe` on Windows, `/bin/sh` on Unix). If any part of the string comes from user input, an attacker can inject shell commands using characters such as `;`, `&&`, `|`, or `$(...)`. **Never pass unsanitised user input to `Process.run`.** Validate and whitelist all inputs before use, or construct the command from a fixed set of known-safe values only. **When any part of a command comes from untrusted input, prefer `Process.run_command` (below), which bypasses the shell entirely.**

`Process.Command` record fields: `program` (`string`), `arguments` (`array<string>`). Built by `Process.command(program, arguments)` and executed by `Process.run_command`, which launches the program **directly** — `execvp` on POSIX, `CreateProcess` on Windows — with **no shell involved**, so shell metacharacters (`;`, `&&`, `|`, `$(...)`) in any argument are passed through literally rather than interpreted. This is the safe alternative to building a `Process.run` string from untrusted input: `Process.run_command(Process.command("git", ["log", user_branch]))` cannot be turned into a command-injection hole the way `Process.run("git log " + user_branch)` can. It returns the same `result<Process.CommandOutput>` as `Process.execute` (a negative exit code — surfaced as `failure` — signals a launch failure such as an unknown program or an empty `program` name).

```luma
# Safe: `user_branch` is a single argument, never re-parsed by a shell.
Process.Command cmd = Process.command("git", ["log", "--oneline", user_branch])
match Process.run_command(cmd) {
    success(out) { print(out.standard_output) }
    failure(err) { print("could not run git: ${err}") }
}
```

`Process.ProcessResult` record fields: `exit_code` (`integer`), `output` (`string`).

`Process.CommandOutput` record fields: `exit_code` (`integer`), `standard_output` (`string`), `standard_error` (`string`), `success` (`boolean`, true when `exit_code` is `0`). It is returned by `Process.execute`, the richer sibling of `Process.run`: where `ProcessResult` captures only stdout, `CommandOutput` keeps stdout and stderr in separate fields — essential for diagnosing a failed command, whose error text `run` discards — and adds a derived `success` convenience so callers avoid re-checking `exit_code == 0`. `Process.run` and `ProcessResult` are unchanged for the common case. The same shell-injection **security warning** above applies verbatim to `Process.execute`.

```luma
match Process.execute("git status") {
success(out) {
    if out.success {
        print(out.standard_output)
    } else {
        print("git failed (${out.exit_code}): ${out.standard_error}")
    }
}
failure(_e) { print("could not launch command") }
}
```

`Process.ExitStatus` is a choice type — `Success`, `Failed(code: integer)`, `LaunchFailed` — that turns a `CommandOutput`'s `exit_code` sign convention into an exhaustive, match-able type, mirroring `Http.StatusClass` and `Sign` above: `0` classifies as `Success`, a positive code classifies as `Failed` (carrying the code), and a negative code classifies as `LaunchFailed` (the process never ran — see the launch-failure note under `Process.Command`). `Process.exit_status(output)` is a pure classifier over an already-captured `Process.CommandOutput` or `Process.ProcessResult`-shaped record; it does not launch or capture anything itself.

```luma
match Process.execute("git status") {
success(out) {
    match Process.exit_status(out) {
    case Process.ExitStatus.Success        { print(out.standard_output) }
    case Process.ExitStatus.Failed(code)   { print("git exited with ${code}: ${out.standard_error}") }
    case Process.ExitStatus.LaunchFailed   { print("git could not be launched") }
    }
}
failure(_e) { print("could not launch command") }
}
```

`Process.Error` is a choice type classifying _why_ a command failed to launch — `NotFound` (the program is not installed / not on `PATH`), `PermissionDenied` (the file exists but is not executable), `InvalidCommand` (an empty program name, or a file that is not a valid executable), and `LaunchFailed` (any other spawn failure). Where `Process.ExitStatus` classifies a command that _ran_ (its exit code), `Process.Error` classifies why a launch _failed_ — the two axes are otherwise conflated in the negative-exit-code convention. It is surfaced by `Process.run_command_typed(cmd)`, which returns `result<Process.CommandOutput, Process.Error>`: a command that runs (even one that exits non-zero) is a `success` carrying the `CommandOutput` (classify its `exit_code` with `Process.exit_status`), while a launch failure is a typed `Process.Error`. This distinguishes "`git` isn't installed" (`NotFound`) from "`git` ran and exited 1" (`ExitStatus.Failed`). This is an opt-in, additive companion (mirroring `FileSystem.read_file_typed` / `FileSystem.IoError`): the string-error `Process.run_command` is left untouched.

```luma
Process.Command cmd = Process.command("git", ["status"])
match Process.run_command_typed(cmd) {
success(out) { print(out.standard_output) }
failure(Process.Error.NotFound) { print("git is not installed") }
failure(Process.Error.PermissionDenied) { print("git is not executable") }
failure(_other) { print("git could not be launched") }
}
```

`Process.Signal` is a choice type with four variants — `Terminate`, `Kill`, `Interrupt`, `Hangup` — a portable, match-able request to end another process, consumed by `Process.signal(pid, signal)` (which returns `result<boolean>`: `success(true)` when the OS accepted the request, and a `failure` when it did not — for example no such process, or insufficient permission). On POSIX the four variants map directly to `SIGTERM`, `SIGKILL`, `SIGINT`, and `SIGHUP`. On Windows there are no POSIX signals, so the mapping is deliberately lossy: `Terminate` and `Kill` both call `TerminateProcess`, `Interrupt` sends a `CTRL_C_EVENT` to the target's console group where possible, and `Hangup` degrades to `TerminateProcess`. Because it uses no magic signal numbers and a `match` over it is exhaustive, calling code stays portable and readable. Like the rest of `Process`, `signal` is OS-only (unavailable in `--box` sandbox mode).

```luma
match Process.signal(child_pid, Process.Signal.Terminate) {
success(_ok) { print("asked the process to stop") }
failure(_e)  { print("could not signal that process") }
}
```

## 29 — Queue

Immutable FIFO (first-in, first-out) queue. All mutating operations return a new queue, leaving the original unchanged.

| Function                    | Parameter Types                   | Return Type              | Description                                            |
| --------------------------- | --------------------------------- | ------------------------ | ------------------------------------------------------ |
| `Queue.concat(a, b)`        | `(queue, queue)`                  | `queue`                  | Concatenate two queues                                 |
| `Queue.dequeue(q)`          | `(queue)`                         | `result<(T, queue)>`     | Remove front element; fail if empty                    |
| `Queue.each(q, fn)`         | `(queue, function(T) -> none)`    | `result<none>`           | Iterate for side effects; fail if callback throws      |
| `Queue.enqueue(q, v)`       | `(queue, T)`                      | `queue`                  | Add element to back                                    |
| `Queue.filter(q, fn)`       | `(queue, function(T) -> boolean)` | `result<queue>`          | Keep matching elements; fail if callback throws        |
| `Queue.from_array(arr)`     | `(array<T>)`                      | `queue`                  | Create queue from array                                |
| `Queue.is_empty(q)`         | `(queue)`                         | `boolean`                | Whether the queue is empty                             |
| `Queue.length(q)`           | `(queue)`                         | `integer`                | Number of elements                                     |
| `Queue.map(q, fn)`          | `(queue, function(T) -> U)`       | `result<queue>`          | Transform each element; fail if callback throws        |
| `Queue.new()`               | `()`                              | `queue`                  | Empty queue                                            |
| `Queue.partition(q, fn)`    | `(queue, function(T) -> boolean)` | `result<(queue, queue)>` | Split into `(matches, rest)`; fail if predicate throws |
| `Queue.peek(q)`             | `(queue)`                         | `result<T>`              | View front element; fail if empty                      |
| `Queue.reduce(q, init, fn)` | `(queue, U, function(U, T) -> U)` | `result<U>`              | Fold elements; fail if callback throws                 |
| `Queue.to_array(q)`         | `(queue)`                         | `array<T>`               | Convert to array                                       |

## 30 — Random

| Function                          | Parameter Types       | Return Type        | Description                                                                     |
| --------------------------------- | --------------------- | ------------------ | ------------------------------------------------------------------------------- |
| `Random.choice(arr)`              | `(array<T>)`          | `result<T>`        | Random element; fail if array is empty                                          |
| `Random.generate_boolean()`       | `()`                  | `boolean`          | Random `true` or `false`                                                        |
| `Random.generate_integer(lo, hi)` | `(integer, integer)`  | `result<integer>`  | Random integer in `[lo, hi]`; fail if `lo > hi`                                 |
| `Random.generate_number()`        | `()`                  | `number`           | Random number in `[0, 1)`                                                       |
| `Random.generate_string(len)`     | `(integer)`           | `result<string>`   | Random alphanumeric string; fail if `len < 0`. **Not** cryptographically secure |
| `Random.sample(arr, k)`           | `(array<T>, integer)` | `result<array<T>>` | `k` unique random elements; fail if `k > length`                                |
| `Random.sample_from(distribution)` | `(Random.Distribution)` | `result<number>` | Draw a number from a `Random.Distribution`; see below for validation           |
| `Random.shuffle(arr)`             | `(array<T>)`          | `array<T>`         | Shuffled copy                                                                   |
| `Random.generate_uuid()`          | `()`                  | `string`           | UUID v4 (random). **Not** cryptographically secure                              |
| `Random.parse_uuid(s)`            | `(string)`            | `result<Random.Uuid>` | Validate a canonical UUID string into a typed `Random.Uuid`; fail if malformed |
| `Random.uuid_typed()`             | `()`                  | `Random.Uuid`      | UUID v4 (random) as a typed `Random.Uuid`. **Not** cryptographically secure     |
| `Random.uuid_to_string(u)`        | `(Random.Uuid)`       | `string`           | The canonical string held by a `Random.Uuid`                                    |
| `Random.secure_boolean()`         | `()`                  | `result<boolean>`  | Cryptographically secure random `true` or `false`                               |
| `Random.secure_integer(lo, hi)`   | `(integer, integer)`  | `result<integer>`  | Cryptographically secure uniform integer in `[lo, hi]`; fail if `lo > hi`       |
| `Random.secure_number()`          | `()`                  | `result<number>`   | Cryptographically secure random number in `[0, 1)`                              |
| `Random.secure_string(len)`       | `(integer)`           | `result<string>`   | Cryptographically secure alphanumeric string; fail if `len < 0`                 |
| `Random.secure_uuid()`            | `()`                  | `result<string>`   | UUID v4 from cryptographically secure random bytes                              |

**Cryptographically secure functions.** The `secure_*` variants use AES-CTR-DRBG (via Mbed TLS) seeded from platform entropy. They are suitable for generating tokens, secrets, and session identifiers. Requires TLS support (`LUMA_FEATURE_TLS=ON`, enabled by default).

**`Random.Distribution`.** A closed choice of probability distributions consumed by `Random.sample_from`, so callers state intent ("draw from a `Normal(0, 1)`") instead of composing raw uniform draws by hand:

- `Random.Distribution.Uniform(low: number, high: number)` — a uniform draw in `[low, high]`; fails if `high < low`.
- `Random.Distribution.Normal(mean: number, standard_deviation: number)` — a normal (Gaussian) draw via the Box–Muller transform; fails if `standard_deviation <= 0`.
- `Random.Distribution.Exponential(rate: number)` — an exponential draw with rate (λ) `rate`, via inverse-transform sampling; fails if `rate <= 0`.

```luma
result<number> uniform = Random.sample_from(Random.Distribution.Uniform(0.0, 10.0))
result<number> normal = Random.sample_from(Random.Distribution.Normal(0.0, 1.0))
result<number> exponential = Random.sample_from(Random.Distribution.Exponential(0.5))
```

**`Random.Uuid`.** A record — `value` (`string`) — wrapping a validated canonical UUID (the `8-4-4-4-12` hex form), so a UUID is a distinct, validated type rather than an anonymous string. `Random.uuid_typed()` generates one (like `generate_uuid`, but typed), `Random.parse_uuid(s)` validates an incoming string and fails for any non-canonical input (the stored value is lower-cased so equal UUIDs compare equal regardless of input case), and `Random.uuid_to_string(u)` reads the canonical string back out. The bare-string `Random.generate_uuid` / `secure_uuid` are unchanged. Mirrors `Socket.IpAddress`, a typed wrapper over an otherwise-stringly address.

```luma
Random.Uuid id = Random.uuid_typed()
result<Random.Uuid> parsed = Random.parse_uuid("550e8400-e29b-41d4-a716-446655440000")
```

## 31 — Reference

Mutable reference cells — shared mutable containers that preserve identity across closure capture boundaries. All functions are available as `Reference.function_name(...)` without a `use` declaration.

A reference cell wraps a value in a heap-allocated, thread-safe container. When a closure captures a reference cell, both the closure and the outer scope share the **same** underlying cell, so mutations made inside the closure are visible outside and vice versa.

| Function                     | Parameter Types                    | Return Type    | Description                                          |
| ---------------------------- | ---------------------------------- | -------------- | ---------------------------------------------------- |
| `Reference.get(ref)`         | `(reference<T>)`                   | `T`            | Return the current value stored in the cell          |
| `Reference.new(value)`       | `(T)`                              | `reference<T>` | Create a new reference cell containing `value`       |
| `Reference.set(ref, value)`  | `(reference<T>, T)`                | `none`         | Replace the stored value with `value`                |
| `Reference.swap(ref1, ref2)` | `(reference<T>, reference<T>)`     | `none`         | Swap the values stored in two reference cells        |
| `Reference.inspect(ref)`     | `(reference<T>)`                   | `string`       | String representation, e.g. `"ref(42)"`              |
| `Reference.update(ref, fn)`  | `(reference<T>, function(T) -> T)` | `none`         | Apply `fn` to the current value and store the result |

```luma
reference<integer> count = Reference.new(0)

# Closures share the same reference cell
function() -> none increment = () -> {
    Reference.update(count, (integer n) -> n + 1)
}

increment()
increment()
increment()

integer value = Reference.get(count) # 3
```

Reference cells enable callback-style patterns:

```luma
reference<string> state = Reference.new("idle")

function() -> none start_fn = () -> { Reference.set(state, "running") }
function() -> none stop_fn  = () -> { Reference.set(state, "stopped") }

start_fn()
print(Reference.get(state)) # "running"

stop_fn()
print(Reference.get(state)) # "stopped"
```

The pipe operator works with Reference functions:

```luma
integer value = Reference.new(42) |> Reference.get()       # 42
string  text  = Reference.new(7)  |> Reference.inspect()  # "ref(7)"
```

## 32 — RegularExpression

| Function                                          | Parameter Types            | Return Type                              | Description                                                   |
| ------------------------------------------------- | -------------------------- | ---------------------------------------- | ------------------------------------------------------------- |
| `RegularExpression.find(s, pattern)`              | `(string, string)`         | `result<RegularExpression.Match>`        | First match; fail if pattern is invalid                       |
| `RegularExpression.find_all(s, pattern)`          | `(string, string)`         | `result<array<RegularExpression.Match>>` | All matches; fail if pattern is invalid                       |
| `RegularExpression.is_valid(pattern)`             | `(string)`                 | `boolean`                                | Whether `pattern` is a valid regex                            |
| `RegularExpression.compile_typed(pattern)`        | `(string)`                 | `result<string, RegularExpression.Error>` | Validate a pattern; on failure the error is a typed `RegularExpression.Error` |
| `RegularExpression.matches(s, pattern)`           | `(string, string)`         | `result<boolean>`                        | Whether `pattern` is found in `s`; fail if pattern is invalid |
| `RegularExpression.replace(s, pattern, repl)`     | `(string, string, string)` | `result<string>`                         | Replace first match; fail if pattern is invalid               |
| `RegularExpression.replace_all(s, pattern, repl)` | `(string, string, string)` | `result<string>`                         | Replace all matches; fail if pattern is invalid               |
| `RegularExpression.split(s, pattern)`             | `(string, string)`         | `result<array<string>>`                  | Split by regex; fail if pattern is invalid                    |

`find` and `find_all` return `RegularExpression.Match` records with fields `text` (the matched substring), `position` (zero-based index), `length` (character count of the match), `groups` (an `array<RegularExpression.Match>` of capture-group matches), and `named_groups` (a `dictionary<RegularExpression.Capture>` of named capture-group matches, keyed by group name). Each element of `groups` is itself a `Match` record with the same `text`, `position`, and `length` fields. When the pattern contains no capture groups, `groups` is an empty array and `named_groups` is an empty dictionary.

```luma
RegularExpression.Match m =
    RegularExpression.find("alice@example.com", "([a-z]+)@([a-z]+)\\.([a-z]+)")
    |> Result.unwrap()

print(m.text)            # "alice@example.com"
print(m.groups[0].text)  # "alice"
print(m.groups[1].text)  # "example"
print(m.groups[2].text)  # "com"
```

### Named capture groups

A capturing group can be given a name using either `(?<name>...)` (.NET/PCRE2 style) or `(?P<name>...)` (Python style). A named group's match is exposed two ways: positionally, as an ordinary entry in `groups` (exactly as an unnamed group would be), and by name, as a `RegularExpression.Capture` entry in `named_groups`. An unnamed group never appears in `named_groups`.

`RegularExpression.Capture` is a record with fields `name` (the group's declared name), `text` (the matched substring), `position` (zero-based index), and `length` (character count of the match). A capture that is part of `groups` but was never named is not represented as a `Capture` at all — it simply has no corresponding `named_groups` key (the `name = ""` case described in the type only ever arises if a `Capture` value is constructed directly; every `Capture` reachable through `named_groups` has a non-empty `name`, since it is keyed by that name).

```luma
RegularExpression.Match m =
    RegularExpression.find("2024-01-15", "(?<year>[0-9]+)-(?<month>[0-9]+)-(?<day>[0-9]+)")
    |> Result.unwrap()

RegularExpression.Capture year = Dictionary.get(m.named_groups, "year") |> Result.unwrap()

print(year.text)  # "2024"
```

> **Engine limitation (medium risk)** — The C++ standard library's `std::regex` ECMAScript engine has **no native support** for named capture groups: neither `(?<name>...)` nor `(?P<name>...)` parses, and both throw a compile error from `std::regex` directly. Named-group support is therefore implemented entirely client-side: before compiling the pattern, Luma strips the `<name>`/`P<name>` annotation down to a plain `(` (preserving the group's ordinal position exactly, so nothing about the underlying match semantics changes) and separately records a group-index-to-name map, which is used after matching to populate `named_groups`. Once compiled, a named group behaves exactly like an ordinary capturing group — there is no way to distinguish "named" at the regex-engine level, only in Luma's bookkeeping around it. A malformed or unterminated named-group annotation (e.g. a missing `>`) is left untouched and surfaces as an ordinary invalid-pattern failure (`is_valid` returns `false`, and the fallible functions return `failure`) rather than a crash. `(?<=...)` and `(?<!...)` (lookbehind assertions) are recognized as _not_ named-group syntax and are left unchanged — though `std::regex`'s ECMAScript grammar does not support lookbehind at all (only lookahead, `(?=...)`/`(?!...)`), so a pattern using it will fail to compile regardless of named-group handling.

> **Resource limits** — Regular expression patterns are capped at a maximum byte size (see the [resource-limit table](Luma_Performance_Guide.md#6--resource-limits), `LUMA_LIMIT_MAX_REGEX_PATTERN_SIZE`). Patterns exceeding the limit return `failure` (or `false` from `is_valid`). The regex engine uses the ECMAScript dialect provided by the C++ standard library. There is no built-in protection against catastrophic backtracking — patterns with nested quantifiers such as `(a+)+b` can take exponential time on non-matching input. When processing untrusted patterns, keep them simple and avoid nested repetition operators (`*`, `+`, `{n,m}` inside groups that are themselves repeated).

`RegularExpression.Error` is a choice type classifying _why_ a pattern was rejected — `InvalidSyntax(message)` (a typo the engine could not parse, carrying its diagnostic message), `Unsafe` (rejected by the ReDoS guard as catastrophically slow, e.g. the nested quantifier `(a+)+`), and `TooLarge` (past the pattern size limit). It is surfaced by `RegularExpression.compile_typed(pattern)`, which returns `result<string, RegularExpression.Error>`: on success the payload is the validated pattern (reusable directly in `matches`/`find`/`replace`), and on failure the typed category tells a beginner apart a typo from a pattern that was refused for being dangerously slow or too big — a distinction the module already computes internally but that `is_valid`'s bare `boolean` and the other functions' opaque string error hide. This is opt-in and additive (mirroring `Http.get_typed` / `Http.Error` and `FileSystem.read_file_typed` / `FileSystem.IoError`): `is_valid` and every string-error function are unchanged.

```luma
match RegularExpression.compile_typed(user_pattern) {
    success(pattern) { search_with(pattern) }
    failure(e) {
        match e {
            case RegularExpression.Error.InvalidSyntax(message) { print("bad pattern: ${message}") }
            case RegularExpression.Error.Unsafe { print("pattern rejected as too slow") }
            case RegularExpression.Error.TooLarge { print("pattern too large") }
        }
    }
}
```

## 33 — Resource

`Resource.with` guarantees that a cleanup function is called after a body function runs, regardless of whether the body throws a runtime error. It is the Luma equivalent of a `finally`-based cleanup block, expressed as a library function.

`Resource.using` goes one step further: it also handles resource _acquisition_. If `acquire_fn` throws, no release is performed.

| Function                                          | Parameter Types                                           | Return Type | Description                                                                                                 |
| ------------------------------------------------- | --------------------------------------------------------- | ----------- | ----------------------------------------------------------------------------------------------------------- |
| `Resource.using(acquire_fn, body_fn, release_fn)` | `(function() -> T, function(T) -> U, function(T) -> any)` | `U`         | Acquire, use, release. If `acquire_fn` throws, `release_fn` is not called                                   |
| `Resource.with(resource, body_fn, cleanup_fn)`    | `(T, function(T) -> U, function(T) -> any)`               | `U`         | Run `body_fn(resource)`, then always call `cleanup_fn(resource)`. Return value of `cleanup_fn` is discarded |

### Examples

```luma
# Read a file then delete it regardless of success or failure.
string content = Resource.with(
    "data.txt",
    (string path) -> Result.unwrap(FileSystem.read_file(path)),
    (string path) -> FileSystem.delete(path)
)

# Wrap a socket connection.
result<socket> conn = Socket.connect("127.0.0.1", 8080) # 30 s connect timeout

match conn {
    failure(msg) { print("connect failed: ${msg}") }
    success(s)   {
        Resource.with(
            s,
            (socket sock) -> Socket.send(sock, "GET / HTTP/1.0\r\n"),
            (socket sock) -> Socket.close(sock)
        )
    }
}
```

If `body_fn` raises a runtime error, `cleanup_fn` is called first and the error propagates normally.

`Resource.using` combines acquisition and cleanup in a single call:

```luma
# Acquire a temp file, write to it, then delete it.
string content = Resource.using(
    () -> {
        result<boolean> _w = FileSystem.write_file("_tmp.txt", "data")
        return "_tmp.txt"
    },
    (string path) -> Result.unwrap(FileSystem.read_file(path)),
    (string path) -> FileSystem.delete(path)
)
```

## 34 — Result

See the [User Manual — §14 Result and Optional](Luma_User_Manual.md#14--result-and-optional).

## 35 — Set

`Set` values are a distinct type (not arrays). Use `Set.from_array` to create a set and `Set.to_array` to convert back.

> **Set vs HashSet** — `Set` stores elements in an ordered array using linear scans, so it works with **any value type** (records, tuples, etc.) but membership tests are **O(n)**. Choose `Set` when you need ordering guarantees or non-primitive elements. For large collections of primitive values (`boolean`, `integer`, `number`, `string`) where fast lookups matter, use `HashSet` instead — it provides **O(1)** average-case membership testing via hashing.

| Function                             | Parameter Types                 | Return Type          | Description                                                    |
| ------------------------------------ | ------------------------------- | -------------------- | -------------------------------------------------------------- |
| `Set.add(s, v)`                      | `(set, T)`                      | `set`                | Set with `v` included                                          |
| `Set.concat(a, b)`                   | `(set, set)`                    | `set`                | Append the elements of `b` to `a`, keeping unique values       |
| `Set.contains(s, v)`                 | `(set, T)`                      | `boolean`            | Whether `v` is in the set                                      |
| `Set.difference(s, other)`           | `(set, set)`                    | `set`                | Elements in `s` but not `other`                                |
| `Set.each(s, fn)`                    | `(set, function(T) -> none)`    | `result<none>`       | Apply `fn` to each element; fail if `fn` throws                |
| `Set.equals(s, other)`               | `(set, set)`                    | `boolean`            | Order-insensitive equality                                     |
| `Set.filter(s, fn)`                  | `(set, function(T) -> boolean)` | `result<set>`        | Elements for which `fn` returns true; fail if predicate throws |
| `Set.from_array(arr)`                | `(array<T>)`                    | `set`                | Deduplicated set from array                                    |
| `Set.intersection(s, other)`         | `(set, set)`                    | `set`                | Elements in both sets                                          |
| `Set.is_disjoint(s, other)`          | `(set, set)`                    | `boolean`            | Whether the sets share no elements                             |
| `Set.is_empty(s)`                    | `(set)`                         | `boolean`            | Whether the set is empty                                       |
| `Set.is_subset(s, other)`            | `(set, set)`                    | `boolean`            | Whether `s` ⊆ `other`                                          |
| `Set.is_superset(s, other)`          | `(set, set)`                    | `boolean`            | Whether `s` ⊇ `other`                                          |
| `Set.length(s)`                      | `(set)`                         | `integer`            | Number of elements                                             |
| `Set.map(s, fn)`                     | `(set, function(T) -> U)`       | `result<set>`        | Apply `fn` to each element, collecting into a new set          |
| `Set.new()`                          | `()`                            | `set`                | Empty set                                                      |
| `Set.partition(s, fn)`               | `(set, function(T) -> boolean)` | `result<(set, set)>` | Split into `(matches, rest)`; fail if predicate throws         |
| `Set.reduce(s, initial, fn)`         | `(set, U, function(U, T) -> U)` | `result<U>`          | Fold elements with accumulator; fail if `fn` throws            |
| `Set.remove(s, v)`                   | `(set, T)`                      | `set`                | Set without `v`                                                |
| `Set.symmetric_difference(s, other)` | `(set, set)`                    | `set`                | Elements in one but not both                                   |
| `Set.to_array(s)`                    | `(set)`                         | `array<T>`           | Convert to array                                               |
| `Set.union(s, other)`                | `(set, set)`                    | `set`                | Elements in `s` or `other`                                     |

## 36 — Socket

Cross-platform TCP and UDP networking.

| Function                               | Parameter Types                     | Return Type                | Description                                                  |
| -------------------------------------- | ----------------------------------- | -------------------------- | ------------------------------------------------------------ |
| `Socket.accept(srv)`                   | `(socket)`                          | `result<socket>`           | Accept incoming connection                                   |
| `Socket.close(s)`                      | `(socket)`                          | `none`                     | Close the socket                                             |
| `Socket.connect(host, port)`           | `(string, integer)`                 | `result<socket>`           | TCP connect (30 s timeout)                                   |
| `Socket.connect_typed(host, port)`     | `(string, integer)`                 | `result<socket, Socket.Error>` | TCP connect; on failure the error is a typed `Socket.Error` instead of a string |
| `Socket.is_connected(s)`               | `(socket)`                          | `boolean`                  | Whether the socket handle is valid                           |
| `Socket.ip_to_string(ip)`              | `(Socket.IpAddress)`                | `string`                   | Canonical text of a parsed IP address                        |
| `Socket.listen(host, port)`            | `(string, integer)`                 | `result<socket>`           | Bind and listen for TCP connections                          |
| `Socket.listen_typed(host, port)`      | `(string, integer)`                 | `result<socket, Socket.Error>` | Bind and listen; on failure the error is a typed `Socket.Error` (e.g. `AddressInUse`) |
| `Socket.local_address(s)`              | `(socket)`                          | `result<string>`           | Local `"host:port"`                                          |
| `Socket.local_address_parts(s)`        | `(socket)`                          | `result<Socket.Address>`   | Local address as a `{ host, port }` record (IPv6-safe; no string parsing) |
| `Socket.parse_ip(text)`                | `(string)`                          | `result<Socket.IpAddress>` | Validate and classify an IPv4/IPv6 literal (no OS call)      |
| `Socket.receive(s, max)`               | `(socket, integer)`                 | `result<string>`           | Receive up to `max` bytes                                    |
| `Socket.receive_typed(s, max)`         | `(socket, integer)`                 | `result<string, Socket.Error>` | Receive up to `max` bytes; on failure the error is a typed `Socket.Error` |
| `Socket.remote_address(s)`             | `(socket)`                          | `result<string>`           | Remote `"host:port"`                                         |
| `Socket.remote_address_parts(s)`       | `(socket)`                          | `result<Socket.Address>`   | Remote address as a `{ host, port }` record (IPv6-safe; no string parsing) |
| `Socket.send(s, data)`                 | `(socket, string)`                  | `result<integer>`          | Send data; returns bytes sent                                |
| `Socket.send_typed(s, data)`           | `(socket, string)`                  | `result<integer, Socket.Error>` | Send data; on failure the error is a typed `Socket.Error` |
| `Socket.set_timeout(s, ms)`            | `(socket, integer)`                 | `result<boolean>`          | Set send/recv timeout (does not affect connect)              |
| `Socket.udp_bind(s, host, port)`       | `(socket, string, integer)`         | `result<boolean>`          | Bind UDP socket to address                                   |
| `Socket.udp_create()`                  | `()`                                | `result<socket>`           | Create UDP socket                                            |
| `Socket.udp_receive(s, max)`           | `(socket, integer)`                 | `result<Socket.UdpPacket>` | Receive UDP packet; record has `data`, `host`, `port` fields |
| `Socket.udp_send(s, data, host, port)` | `(socket, string, string, integer)` | `result<integer>`          | Send UDP datagram                                            |

> **Resource limit** — A program may hold only a bounded number of open sockets at the same time (see the [resource-limit table](Luma_Performance_Guide.md#6--resource-limits), `LUMA_LIMIT_MAX_OPEN_SOCKETS`). Attempting to create more returns `failure("socket limit reached — too many open sockets")`. Close sockets that are no longer needed to stay within the limit.

`Socket.Address` record fields: `host: string`, `port: integer`. Returned by `Socket.local_address_parts` and `Socket.remote_address_parts` so the caller reads the host and port directly instead of splitting a `"host:port"` string (which is fragile for IPv6). Mirrors the `host`/`port` fields of `Socket.UdpPacket`.

`Socket.IpAddress` is a choice type with two payload-carrying variants — `Socket.IpAddress.V4(address: string)` and `Socket.IpAddress.V6(address: string)` — so the IPv4/IPv6 distinction is `match`-exhaustive and autocompleted, unlike the unvalidated `host` string in `Socket.Address`. `Socket.parse_ip(text)` validates a literal purely in memory (no DNS, no OS call) and returns `failure` on malformed input; IPv4 is canonicalised (leading zeros stripped) and IPv6 lowercased. `Socket.ip_to_string(ip)` renders either variant back to its canonical text.

```luma
Socket.IpAddress ip = Result.unwrap(Socket.parse_ip("2001:DB8::1"))
string family = match ip {
    case Socket.IpAddress.V4(_a) { "IPv4" }
    case Socket.IpAddress.V6(_a) { "IPv6" }
}
```

`Socket.Error` is a choice type classifying _why_ a transport operation failed — `ConnectionRefused` (nothing is listening on the target port), `Timeout` (the connect or receive did not complete in time), `HostUnreachable` (the host could not be resolved or reached), `AddressInUse` (a `listen` bind clashed with a port already in use), `ConnectionReset` (the peer reset or closed the connection mid-transfer), `NotConnected` (the socket handle is closed or not connected), and `Other` (any other failure). It is surfaced by the opt-in `*_typed` companions — `Socket.connect_typed`, `Socket.listen_typed`, `Socket.send_typed`, and `Socket.receive_typed` — which return `result<T, Socket.Error>`: the value on success is exactly what the string-error function returns, and the error on failure is the typed category, so a program can retry only on `Timeout`, fall back only on `ConnectionRefused`, or report a `HostUnreachable` target — instead of substring-matching an opaque message. This is additive (mirroring `Http.get_typed` / `Http.Error` and `FileSystem.read_file_typed` / `FileSystem.IoError`): the plain `Socket.connect`, `Socket.listen`, `Socket.send`, and `Socket.receive` keep their string-error `result<T>`.

```luma
match Socket.connect_typed("127.0.0.1", 8080) {
    success(conn) { use_connection(conn) }
    failure(e) {
        match e {
            case Socket.Error.ConnectionRefused { print("nothing is listening — will retry") }
            case Socket.Error.Timeout { print("connect timed out") }
            case Socket.Error.HostUnreachable { print("cannot reach host") }
            else { print("connection failed") }
        }
    }
}
```

---

## 37 — Stack

Immutable LIFO (last-in, first-out) stack. All mutating operations return a new stack.

| Function                    | Parameter Types                   | Return Type              | Description                                                     |
| --------------------------- | --------------------------------- | ------------------------ | --------------------------------------------------------------- |
| `Stack.concat(a, b)`        | `(stack, stack)`                  | `stack`                  | Concatenate two stacks                                          |
| `Stack.each(s, fn)`         | `(stack, function(T) -> none)`    | `result<none>`           | Iterate top to bottom for side effects; fail if callback throws |
| `Stack.filter(s, fn)`       | `(stack, function(T) -> boolean)` | `result<stack>`          | Keep matching elements; fail if callback throws                 |
| `Stack.from_array(arr)`     | `(array<T>)`                      | `stack`                  | Create stack from array (bottom → top)                          |
| `Stack.is_empty(s)`         | `(stack)`                         | `boolean`                | Whether the stack is empty                                      |
| `Stack.length(s)`           | `(stack)`                         | `integer`                | Number of elements                                              |
| `Stack.map(s, fn)`          | `(stack, function(T) -> U)`       | `result<stack>`          | Transform each element; fail if callback throws                 |
| `Stack.new()`               | `()`                              | `stack`                  | Empty stack                                                     |
| `Stack.partition(s, fn)`    | `(stack, function(T) -> boolean)` | `result<(stack, stack)>` | Split into `(matches, rest)`; fail if predicate throws          |
| `Stack.peek(s)`             | `(stack)`                         | `result<T>`              | View top element; fail if empty                                 |
| `Stack.pop(s)`              | `(stack)`                         | `result<(T, stack)>`     | Pop from top; fail if empty                                     |
| `Stack.push(s, v)`          | `(stack, T)`                      | `stack`                  | Push to top                                                     |
| `Stack.reduce(s, init, fn)` | `(stack, U, function(U, T) -> U)` | `result<U>`              | Fold elements; fail if callback throws                          |
| `Stack.to_array(s)`         | `(stack)`                         | `array<T>`               | Convert to array                                                |

## 38 — String

| Function                            | Parameter Types                | Return Type       | Description                                                                     |
| ----------------------------------- | ------------------------------ | ----------------- | ------------------------------------------------------------------------------- |
| `String.byte_length(s)`             | `(string)`                     | `integer`         | Byte count of the string                                                        |
| `String.capitalize(s)`              | `(string)`                     | `string`          | Uppercase the first character                                                   |
| `String.center(s, width, fill)`     | `(string, integer, string)`    | `result<string>`  | Centre `s` in a field of `width` using `fill`; fail if `width` exceeds the maximum |
| `String.character_at(s, i)`         | `(string, integer)`            | `result<string>`  | Character at codepoint index `i`; fail if out of bounds                         |
| `String.characters(s)`              | `(string)`                     | `array<string>`   | Split into individual Unicode characters                                        |
| `String.chunk(s, n)`                | `(string, integer)`            | `array<string>`   | Split into chunks of `n` characters; throws if `n` <= 0                         |
| `String.common_prefix(a, b)`        | `(string, string)`             | `string`          | Longest common prefix of two strings                                            |
| `String.common_suffix(a, b)`        | `(string, string)`             | `string`          | Longest common suffix of two strings                                            |
| `String.contains(s, sub)`           | `(string, string)`             | `boolean`         | Whether `s` contains `sub`                                                      |
| `String.contains_ignore_case(s, sub)` | `(string, string)`           | `boolean`         | Whether `s` contains `sub`, ignoring ASCII case                                 |
| `String.count(s, sub)`              | `(string, string)`             | `integer`         | Number of non-overlapping occurrences of `sub`                                  |
| `String.dedent(s)`                  | `(string)`                     | `string`          | Remove common leading whitespace                                                |
| `String.ends_with(s, suffix)`       | `(string, string)`             | `boolean`         | Whether `s` ends with `suffix`                                                  |
| `String.equals_ignore_case(a, b)`   | `(string, string)`             | `boolean`         | Whether `a` and `b` are equal, ignoring ASCII case                              |
| `String.format_number(n, decimals)` | `(number, integer)`            | `string`          | Format number with fixed decimal places                                         |
| `String.from_bytes(bytes)`          | `(array<integer>)`             | `result<string>`  | Build string from byte values                                                   |
| `String.from_codepoints(cps)`       | `(array<integer>)`             | `result<string>`  | Build string from Unicode codepoints                                            |
| `String.indent(s, prefix)`          | `(string, string)`             | `string`          | Prepend `prefix` to every line                                                  |
| `String.index_of(s, sub)`           | `(string, string)`             | `result<integer>` | First index of `sub`; fail if not found                                         |
| `String.is_alpha(s)`                | `(string)`                     | `boolean`         | Whether all characters are alphabetic                                           |
| `String.is_alphanumeric(s)`         | `(string)`                     | `boolean`         | Whether all characters are alphanumeric                                         |
| `String.is_ascii(s)`                | `(string)`                     | `boolean`         | Whether all bytes are in the ASCII range (0x00–0x7F)                            |
| `String.is_digit(s)`                | `(string)`                     | `boolean`         | Whether all characters are digits                                               |
| `String.is_empty(s)`                | `(string)`                     | `boolean`         | Whether the string is empty                                                     |
| `String.is_lowercase(s)`            | `(string)`                     | `boolean`         | Whether all characters are lowercase                                            |
| `String.is_numeric(s)`              | `(string)`                     | `boolean`         | Whether the string represents a numeric value                                   |
| `String.is_palindrome(s)`           | `(string)`                     | `boolean`         | Whether `s` reads the same forwards and backwards                               |
| `String.is_uppercase(s)`            | `(string)`                     | `boolean`         | Whether all characters are uppercase                                            |
| `String.is_whitespace(s)`           | `(string)`                     | `boolean`         | Whether all characters are whitespace                                           |
| `String.is_blank(s)`                | `(string)`                     | `boolean`         | Whether the string is empty or all whitespace                                   |
| `String.join(arr, sep)`             | `(array<string>, string)`      | `string`          | Join array elements with separator                                              |
| `String.last_index_of(s, sub)`      | `(string, string)`             | `result<integer>` | Last index of `sub`; fail if not found                                          |
| `String.length(s)`                  | `(string)`                     | `integer`         | Number of Unicode codepoints (not bytes)                                        |
| `String.levenshtein_distance(a, b)` | `(string, string)`             | `integer`         | Edit distance between two strings                                               |
| `String.lines(s)`                   | `(string)`                     | `array<string>`   | Split into lines on `\n`, `\r\n`, `\r`; no trailing empty after a final newline |
| `String.lowercase(s)`               | `(string)`                     | `string`          | Convert to lowercase                                                            |
| `String.matches(s, glob)`           | `(string, string)`             | `result<boolean>` | Glob match (`*` = any chars, `?` = one char)                                    |
| `String.pad_left(s, width, fill)`   | `(string, integer, string)`    | `result<string>`  | Left-pad to `width`; fail if `width` exceeds the maximum                        |
| `String.pad_right(s, width, fill)`  | `(string, integer, string)`    | `result<string>`  | Right-pad to `width`; fail if `width` exceeds the maximum                       |
| `String.parse_integer(s)`           | `(string)`                     | `result<integer>` | Parse string as integer                                                         |
| `String.parse_number(s)`            | `(string)`                     | `result<number>`  | Parse string as number                                                          |
| `String.remove_prefix(s, prefix)`   | `(string, string)`             | `string`          | Remove leading prefix                                                           |
| `String.remove_suffix(s, suffix)`   | `(string, string)`             | `string`          | Remove trailing suffix                                                          |
| `String.repeat(s, n)`               | `(string, integer)`            | `result<string>`  | Repeat `s` `n` times; fail if the count or result size exceeds the maximum      |
| `String.replace(s, old, new)`       | `(string, string, string)`     | `string`          | Replace first occurrence                                                        |
| `String.replace_all(s, old, new)`   | `(string, string, string)`     | `string`          | Replace all occurrences                                                         |
| `String.reverse(s)`                 | `(string)`                     | `string`          | Reverse the string                                                              |
| `String.slug(s)`                    | `(string)`                     | `string`          | Convert to a URL-friendly slug                                                  |
| `String.split(s, sep)`              | `(string, string)`             | `array<string>`   | Split by separator                                                              |
| `String.split_n(s, sep, n)`         | `(string, string, integer)`    | `array<string>`   | Split into at most `n` parts                                                    |
| `String.starts_with(s, prefix)`     | `(string, string)`             | `boolean`         | Whether `s` starts with `prefix`                                                |
| `String.substring(s, start, end)`   | `(string, integer, integer)`   | `string`          | Substring by codepoint indices                                                  |
| `String.template(tmpl, vars)`       | `(string, dictionary<string>)` | `string`          | Replace `{key}` placeholders from dictionary                                    |
| `String.title_case(s)`              | `(string)`                     | `string`          | Capitalise first letter of each word                                            |
| `String.to_bytes(s)`                | `(string)`                     | `array<integer>`  | UTF-8 byte values                                                               |
| `String.to_camel_case(s)`           | `(string)`                     | `string`          | Convert to `camelCase`                                                          |
| `String.to_codepoints(s)`           | `(string)`                     | `array<integer>`  | Unicode codepoint values                                                        |
| `String.to_kebab_case(s)`           | `(string)`                     | `string`          | Convert to `kebab-case`                                                         |
| `String.to_pascal_case(s)`          | `(string)`                     | `string`          | Convert to `PascalCase`                                                         |
| `String.to_snake_case(s)`           | `(string)`                     | `string`          | Convert to `snake_case`                                                         |
| `String.trim(s)`                    | `(string)`                     | `string`          | Remove leading and trailing whitespace                                          |
| `String.trim_end(s)`                | `(string)`                     | `string`          | Remove trailing whitespace                                                      |
| `String.trim_start(s)`              | `(string)`                     | `string`          | Remove leading whitespace                                                       |
| `String.truncate(s, max)`           | `(string, integer)`            | `string`          | Truncate to `max` characters, appending `"..."` if needed                       |
| `String.uppercase(s)`               | `(string)`                     | `string`          | Convert to uppercase                                                            |
| `String.wrap(s, width)`             | `(string, integer)`            | `string`          | Word-wrap at `width` columns                                                    |

> **Note:** `String.length` returns an `integer` — the number of Unicode codepoints (not bytes). Use `String.byte_length` to get the byte count of a string.

> **Note:** `String.uppercase()`, `String.lowercase()`, `String.equals_ignore_case()`, and `String.contains_ignore_case()` only fold ASCII characters (a–z, A–Z). Non-ASCII UTF-8 code points (e.g., `ü`, `é`, `ñ`) are compared or passed through unchanged. Use these functions only when working with ASCII text.

> **Resource limits** — `String.center`, `String.pad_left`, and `String.pad_right` cap their target `width`, and `String.repeat` caps its repeat count and result size. See the [resource-limit table](Luma_Performance_Guide.md#6--resource-limits) for Luma's resource limits and their `LUMA_LIMIT_*` overrides.

## 39 — Task

Concurrency combinators for `spawn`/`await` tasks.

`spawn` requires a direct function call expression — passing a lambda or function stored in a variable is not supported:

```luma
task<integer> t = spawn compute(42) # ok — direct call
# task<integer> t = spawn fn        # error: spawn requires a function call expression
```

Each `task` value may be awaited only once. Awaiting the same task a second time produces a runtime error (`await called on an already-consumed task`).

### Structured Concurrency with `task_scope`

A `task_scope` block provides structured concurrency — all tasks spawned inside the block are automatically awaited before the scope exits. The scope returns an `array` of the results in spawn order. If any child task fails, the remaining siblings are cancelled cooperatively and the error propagates out of the scope.

```luma
array<integer> results = task_scope {
    spawn compute(1)
    spawn compute(2)
    spawn compute(3)
}
# results == [1, 4, 9] — all three tasks completed
```

Scopes can be nested. Each nested `task_scope` completes before the outer scope continues:

```luma
array<integer> outer = task_scope {
    spawn compute(2)

    array<integer> inner = task_scope {
        spawn compute(10)
        spawn compute(11)
    }
    # inner is available here

    spawn compute(3)
}
```

Using `spawn` outside a `task_scope` still works (fire-and-forget) but produces a type-checker warning. Wrap spawns in a `task_scope` for structured lifetime management.

| Function                | Parameter Types                      | Return Type        | Description                                      |
| ----------------------- | ------------------------------------ | ------------------ | ------------------------------------------------ |
| `await t`               | `(task<T>)`                          | `T`                | Block until task completes and return result     |
| `spawn func(args...)`   | _(direct call)_                      | `task<T>`          | Spawn a concurrent task                          |
| `task_scope { ... }`    | _(block)_                            | `array<T>`         | Run all spawned tasks; collect results in order  |
| `Task.all(tasks)`       | `(array<task<T>>)`                   | `result<array<T>>` | Wait for all; return results in order            |
| `Task.any(tasks)`       | `(array<task<T>>)`                   | `result<T>`        | First successful result; ignore failures         |
| `Task.cancel(t)`        | `(task<T>)`                          | `boolean`          | Cancel a task cooperatively; `true` if token set |
| `Task.delay(ms)`        | `(integer)`                          | `none`             | Sleep for `ms` milliseconds                      |
| `Task.flat_map(t, fn)`  | `(task<T>, function(T) -> task<U>)`  | `result<U>`        | Chain with another spawn                         |
| `Task.is_cancelled(t)`  | `(task<T>)`                          | `boolean`          | Whether the task's cancellation token is set     |
| `Task.is_done(t)`       | `(task<T>)`                          | `result<boolean>`  | Whether the task has completed                   |
| `Task.map(t, fn)`       | `(task<T>, function(T) -> U)`        | `result<U>`        | Transform completed value                        |
| `Task.map_n(tasks, fn)` | `(array<task<T>>, function(T) -> U)` | `result<array<U>>` | Map over all task results                        |
| `Task.race(tasks)`      | `(array<task<T>>)`                   | `result<T>`        | Return first completed result                    |
| `Task.retry(n, fn)`     | `(integer, function() -> T)`         | `result<T>`        | Retry up to `n` times; `n` must be > 0           |
| `Task.sequence(tasks)`  | `(array<task<T>>)`                   | `result<array<T>>` | Await each in order; collect results             |
| `Task.timeout(t, ms)`   | `(task<T>, integer)`                 | `result<T>`        | Await with timeout; fail on timeout              |

> **Resource limit** — The internal task queue holds a bounded number of pending tasks (see the [resource-limit table](Luma_Performance_Guide.md#6--resource-limits), `LUMA_LIMIT_MAX_TASK_QUEUE_SIZE`). Spawning beyond this limit throws a runtime error (`task queue is full — too many pending tasks`). Design your program to await tasks before spawning more to stay within this limit.

## 40 — Terminal

Terminal UI control — cursor movement, colors, styling, screen management, and mouse input.

| Function                                       | Parameter Types                       | Return Type                       | Description                                                             |
| ---------------------------------------------- | ------------------------------------- | --------------------------------- | ----------------------------------------------------------------------- |
| `Terminal.background_color(color, text)`        | `(Terminal.Color \| string, string)`  | `result<string>`                  | Named background color; accepts a `Terminal.Color` variant or string name |
| `Terminal.bell()`                              | `()`                                  | `none`                            | Audible bell                                                            |
| `Terminal.bold(text)`                          | `(string)`                            | `string`                          | Bold styled text                                                        |
| `Terminal.clear_line()`                        | `()`                                  | `none`                            | Clear entire current line                                               |
| `Terminal.clear_screen()`                      | `()`                                  | `none`                            | Clear screen and move to top-left                                       |
| `Terminal.clear_to_end_of_line()`              | `()`                                  | `none`                            | Clear from cursor to end of line                                        |
| `Terminal.clear_to_end_of_screen()`            | `()`                                  | `none`                            | Clear from cursor to end of screen                                      |
| `Terminal.color(color, text)`                   | `(Terminal.Color \| string, string)`  | `result<string>`                  | Named foreground color; accepts a `Terminal.Color` variant or string name |
| `Terminal.columns()`                           | `()`                                  | `integer`                         | Terminal width in columns                                               |
| `Terminal.dim(text)`                           | `(string)`                            | `string`                          | Dim styled text                                                         |
| `Terminal.disable_mouse()`                     | `()`                                  | `none`                            | Disable mouse event reporting                                           |
| `Terminal.disable_raw_mode()`                  | `()`                                  | `none`                            | Restore normal terminal mode                                            |
| `Terminal.enable_mouse()`                      | `()`                                  | `result<none>`                    | Enable mouse events; fail if raw mode is not active                     |
| `Terminal.enable_raw_mode()`                   | `()`                                  | `result<none>`                    | Enter raw mode; fail if OS rejects                                      |
| `Terminal.enter_alternate_screen()`            | `()`                                  | `none`                            | Switch to alternate screen buffer                                       |
| `Terminal.get_cursor_position()`               | `()`                                  | `result<Terminal.CursorPosition>` | Current cursor position; fail if raw mode is disabled                   |
| `Terminal.get_escape_timeout()`                | `()`                                  | `integer`                         | Current escape-sequence timeout in milliseconds                         |
| `Terminal.get_input()`                         | `()`                                  | `result<Terminal.InputEvent>`     | Blocking structured key read; requires raw mode                         |
| `Terminal.hide_cursor()`                       | `()`                                  | `none`                            | Hide the cursor                                                         |
| `Terminal.inverse(text)`                       | `(string)`                            | `string`                          | Inverted-color styled text                                              |
| `Terminal.is_in_raw_mode()`                    | `()`                                  | `boolean`                         | Whether raw mode is active                                              |
| `Terminal.is_mouse_enabled()`                  | `()`                                  | `boolean`                         | Whether mouse mode is active                                            |
| `Terminal.is_terminal()`                       | `()`                                  | `boolean`                         | Whether stdout is connected to a TTY                                    |
| `Terminal.italic(text)`                        | `(string)`                            | `string`                          | Italic styled text                                                      |
| `Terminal.leave_alternate_screen()`            | `()`                                  | `none`                            | Restore original screen buffer                                          |
| `Terminal.link(url, text)`                     | `(string, string)`                    | `string`                          | Clickable hyperlink                                                     |
| `Terminal.move_down(n)`                        | `(integer)`                           | `none`                            | Move cursor down `n` lines                                              |
| `Terminal.move_left(n)`                        | `(integer)`                           | `none`                            | Move cursor left `n` columns                                            |
| `Terminal.move_right(n)`                       | `(integer)`                           | `none`                            | Move cursor right `n` columns                                           |
| `Terminal.move_to_column(col)`                 | `(integer)`                           | `result<none>`                    | Move to column; fail if `col < 1`                                       |
| `Terminal.move_to_row(row)`                    | `(integer)`                           | `result<none>`                    | Move to row; fail if `row < 1`                                          |
| `Terminal.move_to(row, col)`                   | `(integer, integer)`                  | `none`                            | Move cursor to row and column (1-based)                                 |
| `Terminal.move_up(n)`                          | `(integer)`                           | `none`                            | Move cursor up `n` lines                                                |
| `Terminal.overwrite_line(text)`                | `(string)`                            | `none`                            | Clear current line and write                                            |
| `Terminal.parse_key(key)`                      | `(string)`                            | `Terminal.Key`                    | Decode a key name into a typed `Terminal.Key` choice                    |
| `Terminal.parse_mouse_event(key)`              | `(string)`                            | `optional<Terminal.MouseEvent>`   | Decode a `"mouse:<kind>:ROW:COL"` string into a typed event; `none` if malformed |
| `Terminal.plain_style()`                       | `()`                                  | `Terminal.Style`                  | A default `Terminal.Style` (no colours, no attributes) to override with `with` |
| `Terminal.read_key_timeout(ms)`                | `(integer)`                           | `result<string>`                  | Non-blocking key read with timeout in ms                                |
| `Terminal.read_key()`                          | `()`                                  | `result<string>`                  | Blocking key read; requires raw mode                                    |
| `Terminal.reset_scroll_region()`               | `()`                                  | `none`                            | Reset to full-screen scrolling                                          |
| `Terminal.reset_style()`                       | `()`                                  | `none`                            | Reset all text attributes                                               |
| `Terminal.restore_cursor()`                    | `()`                                  | `none`                            | Restore saved cursor position                                           |
| `Terminal.rgb_background_color(r, g, b, text)` | `(integer, integer, integer, string)` | `result<string>`                  | RGB background; fail if any value outside 0–255                         |
| `Terminal.rgb_color(r, g, b, text)`            | `(integer, integer, integer, string)` | `result<string>`                  | RGB foreground; fail if any value outside 0–255                         |
| `Terminal.rows()`                              | `()`                                  | `integer`                         | Terminal height in rows                                                 |
| `Terminal.save_cursor()`                       | `()`                                  | `none`                            | Save current cursor position                                            |
| `Terminal.scroll_down(n)`                      | `(integer)`                           | `none`                            | Scroll content down `n` lines                                           |
| `Terminal.scroll_up(n)`                        | `(integer)`                           | `none`                            | Scroll content up `n` lines                                             |
| `Terminal.set_escape_timeout(ms)`              | `(integer)`                           | `none`                            | Set the escape-sequence timeout in milliseconds                         |
| `Terminal.set_scroll_region(top, bottom)`      | `(integer, integer)`                  | `result<none>`                    | Set scrollable region; fail if `top` or `bottom < 1` or `top >= bottom` |
| `Terminal.set_title(title)`                    | `(string)`                            | `none`                            | Set the terminal window title                                           |
| `Terminal.show_cursor()`                       | `()`                                  | `none`                            | Show the cursor                                                         |
| `Terminal.size()`                              | `()`                                  | `Terminal.Size`                   | Record with `columns` and `rows` fields                                 |
| `Terminal.strikethrough(text)`                 | `(string)`                            | `string`                          | Strikethrough styled text                                               |
| `Terminal.styled(text, style)`                 | `(string, Terminal.Style)`            | `string`                          | Render `text` with a `Terminal.Style` as one combined ANSI sequence with a single reset |
| `Terminal.supports_color()`                    | `()`                                  | `boolean`                         | Whether the terminal supports ANSI colour                               |
| `Terminal.supports_true_color()`               | `()`                                  | `boolean`                         | Whether the terminal supports 24-bit true colour                        |
| `Terminal.test_feed(keys)`                     | `(array<string>)`                     | `none`                            | Append scripted keys to the active headless test session                |
| `Terminal.test_output()`                       | `()`                                  | `string`                          | Output captured so far in the headless test session                     |
| `Terminal.test_remaining()`                    | `()`                                  | `integer`                         | Count of scripted keys not yet consumed                                 |
| `Terminal.test_start(keys)`                    | `(array<string>)`                     | `none`                            | Begin a headless test session driven by scripted `keys`                 |
| `Terminal.test_stop()`                         | `()`                                  | `string`                          | End the headless test session; return the final captured output         |
| `Terminal.underline(text)`                     | `(string)`                            | `string`                          | Underlined styled text                                                  |
| `Terminal.write(text)`                         | `(string)`                            | `none`                            | Write without trailing newline                                          |

### Key and Mouse Event Names

Key names returned by `read_key` / `read_key_timeout` / `get_input`: printable characters (`"a"`, `"Z"`, `"1"`), `"backspace"`, `"delete"`, `"down"`, `"end"`, `"enter"`, `"escape"`, `"f1"`..`"f12"`, `"home"`, `"insert"`, `"left"`, `"page_down"`, `"page_up"`, `"right"`, `"space"`, `"tab"`, `"up"`. Modifiers: `"alt+x"`, `"ctrl+c"`, `"ctrl+shift+left"`, `"shift+tab"`.

`Terminal.CursorPosition` record fields: `row` (`integer`), `column` (`integer`).

`Terminal.Size` record fields: `columns` (`integer`), `rows` (`integer`).

`Terminal.InputEvent` record fields: `key` (`string`), `shift` (`boolean`), `ctrl` (`boolean`), `alt` (`boolean`). The `key` field contains the base key name without modifier prefixes. Use `get_input()` instead of `read_key()` when you need to inspect modifiers individually.

Mouse events arrive from `read_key` / `read_key_timeout` / `get_input` as strings of the form `"mouse:<kind>:ROW:COL"` (ROW and COL are 1-based integers). Decode one into a typed record with `Terminal.parse_mouse_event(key)`, which returns `optional<Terminal.MouseEvent>` — `none` for any string that is not a well-formed, recognised mouse event. This replaces hand-rolled `String.split(key, ":")` parsing, mirroring what `Terminal.InputEvent` does for keys.

`Terminal.MouseEvent` record fields: `kind` (`Terminal.MouseEventKind`), `row` (`integer`), `column` (`integer`).

`Terminal.MouseEventKind` is a closed choice with 13 variants, so a `match` over it is exhaustively checked by the type checker: `LeftPress`, `LeftRelease`, `LeftDrag`, `MiddlePress`, `MiddleRelease`, `MiddleDrag`, `RightPress`, `RightRelease`, `RightDrag`, `WheelUp`, `WheelDown`, `WheelLeft`, `WheelRight`.

```luma
match Terminal.parse_mouse_event(key) {
case some(event) {
    match event.kind {
    case Terminal.MouseEventKind.LeftPress { draw_at(event.row, event.column) }
    case Terminal.MouseEventKind.WheelUp   { scroll_up() }
    else                                   { }
    }
}
case none {
    # Not a mouse event — handle it as a key instead.
}
}
```

`Terminal.parse_key(key)` decodes a raw key name (as returned by `read_key` / `read_key_timeout`, or the `key` field of a `Terminal.InputEvent`) into a typed `Terminal.Key` choice, so key handling can be an exhaustive, typo-proof `match` instead of a chain of string comparisons. It is total — every input maps to a variant, so it returns `Terminal.Key` directly rather than an `optional`.

`Terminal.Key` has 18 variants: two carry a payload — `Character(value: string)` for a printable key and `Function(number: integer)` for `F1`–`F12` (`"f1"` decodes to `Function(1)`) — and the rest are unit variants `Enter`, `Escape`, `Tab`, `Backspace`, `Space`, `Up`, `Down`, `Left`, `Right`, `Home`, `End`, `PageUp`, `PageDown`, `Insert`, `Delete`, and `Unknown`. A name that is neither a recognised special key nor a valid `f<n>` (for example `"a"`, or a bare `"f"`) decodes to `Character(name)`. Modifier prefixes such as `"ctrl+"` are not stripped; use `get_input()` / `Terminal.InputEvent` when you need modifier flags.

```luma
match Terminal.parse_key(key) {
case Terminal.Key.Up             { move_up() }
case Terminal.Key.Down           { move_down() }
case Terminal.Key.Function(n)    { run_macro(n) }
case Terminal.Key.Character(c)   { insert(c) }
else                             { }
}
```

Available named colors: `black`, `blue`, `bright_black` .. `bright_white`, `cyan`, `default`, `green`, `magenta`, `red`, `white`, `yellow`.

`Terminal.color` and `Terminal.background_color` also accept a `Terminal.Color` choice variant in place of the string name. Because the choice is a closed set, the typed form is caught by the type checker (a mistyped colour is a compile error, not a runtime `result` failure) and is autocompleted. `Terminal.Color` has 17 variants: `Black`, `Red`, `Green`, `Yellow`, `Blue`, `Magenta`, `Cyan`, `White`, `BrightBlack`, `BrightRed`, `BrightGreen`, `BrightYellow`, `BrightBlue`, `BrightMagenta`, `BrightCyan`, `BrightWhite`, `Default` — each maps to the identically named colour string. Both the string and choice forms return `result<string>` so the two call styles stay interchangeable.

```luma
# String form (unchanged) — a bad name fails at runtime:
string a = Result.unwrap(Terminal.color("red", "error"))

# Choice form — type-checked and autocompleted:
string b = Result.unwrap(Terminal.color(Terminal.Color.Red, "error"))
string c = Result.unwrap(Terminal.background_color(Terminal.Color.Yellow, "warning"))
```

`Terminal.Style` is a record that declares a reusable text style once instead of nesting per-attribute wrappers. Fields: `foreground` (`Terminal.Color`), `background` (`Terminal.Color`), `bold`, `dim`, `italic`, `underline`, `inverse`, `strikethrough` (all `boolean`). Build a default with `Terminal.plain_style()` (both colours `Terminal.Color.Default` — meaning "leave unchanged" — and every attribute off), then override the fields you want with a record-update (`with`). `Terminal.styled(text, style)` renders the text as one combined ANSI sequence closed by a single reset (`\033[0m`), replacing the order-sensitive, deeply nested `Terminal.bold(Result.unwrap(Terminal.color("red", ...)))`. A fully-default style returns the text unchanged. Like the per-attribute `Terminal.bold` / `italic` / `color` helpers (which stay for one-off styling), `styled` emits ANSI directly; guard on `Terminal.supports_color()` if you want to skip codes when output is not a terminal.

```luma
# Declare "bold red on white" once, reuse it for any text:
Terminal.Style alert = Terminal.plain_style() with {
    bold = true,
    foreground = Terminal.Color.Red,
    background = Terminal.Color.White
}

print(Terminal.styled("disk almost full", alert))
print("done" |> Terminal.styled(alert))
```

`Terminal.is_terminal()` returns `true` when stdout is connected to an interactive terminal device (a TTY) — for example, when a program is run directly in a console or terminal emulator. It returns `false` when stdout is **piped** to another program (`luma app.luma | grep foo`), **redirected** to a file (`luma app.luma > out.txt`), or when the process is spawned without an attached terminal (for example by a CI runner or background job). Use this to conditionally enable ANSI escape codes, colors, or interactive UI only when output goes to a real terminal.

### Interaction Testing

The `Terminal.test_*` functions drive an imperative Terminal/TUI program without a real terminal, so its input loop and rendering can be verified by feeding scripted input and asserting on captured output. They are the imperative counterpart to the `GraphicalUi.test_*` API: instead of threading a model through pure view/update functions, they intercept the console I/O primitives — a scripted-input queue replaces `read_key` / `read_key_timeout` / `get_input`, and a capture buffer replaces `write` / `overwrite_line` / `bell` (and any `emit`-based ANSI output).

| Function                    | Parameter Types   | Return Type | Description                                                              |
| --------------------------- | ----------------- | ----------- | ----------------------------------------------------------------------- |
| `Terminal.test_start(keys)` | `(array<string>)` | `none`      | Begin a session: queue `keys`, start capturing output, report raw mode  |
| `Terminal.test_feed(keys)`  | `(array<string>)` | `none`      | Append more scripted keys mid-session                                   |
| `Terminal.test_output()`    | `()`              | `string`    | Everything captured since `test_start`                                  |
| `Terminal.test_remaining()` | `()`              | `integer`   | Number of scripted keys not yet consumed                                |
| `Terminal.test_stop()`      | `()`              | `string`    | End the session, restore normal I/O, return the final captured output   |

A session is bracketed by `test_start(keys)` … `test_stop()`. Inside it:

- `read_key`, `read_key_timeout`, and `get_input` consume `keys` in order, then report end-of-input once the queue drains (`read_key` fails, so an `unwrap_or("")` loop exits) — exactly how the raw-mode examples self-terminate on EOF.
- `enable_raw_mode`, `enable_mouse`, and `is_terminal()` succeed/report `true` as if a real terminal were attached, so the program runs its real loop; `query`-style calls that need a live terminal (`get_cursor_position`) return a failure result.
- every byte written through `write` / `overwrite_line` / `bell` is appended to the capture buffer instead of the real terminal.

Each element of `keys` is a key name (`"a"`, `"enter"`, `"up"`, `"ctrl+c"`, `"shift+tab"`) or a mouse event string (`"mouse:left_press:ROW:COL"`).

```luma
# A counter TUI driven by "+" / "-" keys, quitting on "q".
function integer run_counter(integer start) {
    mutable integer count = start
    mutable boolean running = true

    while running {
        string key = Result.unwrap_or(Terminal.read_key(), "")

        if key == "" {
            running = false
        } else if key == "+" {
            count = count + 1
        } else if key == "-" {
            count = count - 1
        } else if key == "q" {
            running = false
        }
    }

    return count
}

@test
function void test_counter_responds_to_keys() {
    Terminal.test_start(["+", "+", "+", "-", "q"])
    integer total = run_counter(10)
    string output = Terminal.test_stop()

    assert(total == 12)
    assert(output == "")
}
```

The same machinery is reachable without Luma code via the `LUMA_TERMINAL_INPUT` environment variable (one key per line), which the example runner (`scripts/run_examples.py`) uses to drive the raw-mode example programs unattended.

## 41 — Xml

Parse, build, query, and serialise XML documents. XML nodes are opaque `xml` values; decode one into a typed `Xml.Node` choice with `Xml.to_node` when you need to `match` over its structure.

**Building:**

| Function                             | Parameter Types                | Return Type | Description                   |
| ------------------------------------ | ------------------------------ | ----------- | ----------------------------- |
| `Xml.add_child(el, child)`           | `(xml, xml)`                   | `xml`       | Add child element             |
| `Xml.add_comment(el, text)`          | `(xml, string)`                | `xml`       | Add XML comment               |
| `Xml.element(tag)`                   | `(string)`                     | `xml`       | Create empty element          |
| `Xml.from_dictionary(tag, d)`        | `(string, dictionary<string>)` | `xml`       | Build element from dictionary |
| `Xml.remove_attribute(el, name)`     | `(xml, string)`                | `xml`       | Remove attribute              |
| `Xml.set_attribute(el, name, value)` | `(xml, string, string)`        | `xml`       | Add/set attribute             |
| `Xml.set_cdata(el, data)`            | `(xml, string)`                | `xml`       | Add CDATA section             |
| `Xml.set_tag(el, tag)`               | `(xml, string)`                | `xml`       | Rename element tag            |
| `Xml.set_text(el, text)`             | `(xml, string)`                | `xml`       | Set text content              |

**Querying:**

| Function                                 | Parameter Types         | Return Type          | Description                     |
| ---------------------------------------- | ----------------------- | -------------------- | ------------------------------- |
| `Xml.at(el, path)`                       | `(xml, string)`         | `result<xml>`        | Navigate slash-separated path   |
| `Xml.attribute(el, name)`                | `(xml, string)`         | `result<string>`     | Attribute value                 |
| `Xml.attributes(el)`                     | `(xml)`                 | `dictionary<string>` | All attributes                  |
| `Xml.child_count(el)`                    | `(xml)`                 | `integer`            | Number of children              |
| `Xml.children_by_tag(el, tag)`           | `(xml, string)`         | `array<xml>`         | Children filtered by tag        |
| `Xml.children(el)`                       | `(xml)`                 | `array<xml>`         | Child elements                  |
| `Xml.find(el, tag)`                      | `(xml, string)`         | `result<xml>`        | First child by tag              |
| `Xml.find_all(el, tag)`                  | `(xml, string)`         | `array<xml>`         | All children by tag             |
| `Xml.find_by_attribute(el, name, value)` | `(xml, string, string)` | `result<xml>`        | First child by attribute value  |
| `Xml.has_attribute(el, name)`            | `(xml, string)`         | `boolean`            | Whether attribute exists        |
| `Xml.has_child(el, tag)`                 | `(xml, string)`         | `boolean`            | Whether a child with tag exists |
| `Xml.is_leaf(el)`                        | `(xml)`                 | `boolean`            | Whether element has no children |
| `Xml.tag(el)`                            | `(xml)`                 | `string`             | Element tag name                |
| `Xml.text(el)`                           | `(xml)`                 | `result<string>`     | Text content                    |
| `Xml.text_at(el, path)`                  | `(xml, string)`         | `result<string>`     | Text content at path            |
| `Xml.to_dictionary(el)`                  | `(xml)`                 | `dictionary<string>` | Child-tag → text map            |
| `Xml.to_node(el)`                        | `(xml)`                 | `Xml.Node`           | Decode an `xml` value into a typed `Xml.Node` choice |

**Parsing and serialising:**

| Function                     | Parameter Types | Return Type      | Description              |
| ---------------------------- | --------------- | ---------------- | ------------------------ |
| `Xml.is_valid(s)`            | `(string)`      | `boolean`        | Whether `s` is valid XML |
| `Xml.deserialize(s)`         | `(string)`      | `result<xml>`    | Parse XML string         |
| `Xml.deserialize_detailed(s)` | `(string)`     | `result<Xml.Node, Xml.ParseError>` | Parse into a typed `Xml.Node`; a failure carries the located `Xml.ParseError` |
| `Xml.deserialize_file(path)` | `(string)`      | `result<xml>`    | Parse XML file           |
| `Xml.serialize(el)`          | `(xml)`         | `string`         | Compact XML string       |
| `Xml.serialize_pretty(el)`   | `(xml)`         | `string`         | Indented XML string      |
| `Xml.write_file(path, doc)`  | `(string, xml)` | `result<none>`   | Write XML to file        |

`Xml.to_node(el)` converts an opaque `xml` value into an `Xml.Node` choice, giving XML the same typed-ADT treatment `Json.Value` gives JSON. It is recursive and preserves every child node — unlike `Xml.children`, which returns only element children — so text, comments, and CDATA sections are visible in the tree. `Xml.Node` has four variants: `Element(tag: string, attributes: dictionary<string>, children: array<Xml.Node>)`, `Text(content: string)`, `Comment(content: string)`, and `CData(content: string)`. (Namespaces and processing instructions are out of scope for this first version.)

```luma
Xml.Node node = Xml.to_node(Result.unwrap(Xml.deserialize("<p>hi</p>")))

match node {
case Xml.Node.Element(tag, attrs, kids) { print(tag) }
case Xml.Node.Text(content)             { print(content) }
case Xml.Node.Comment(content)          { }
case Xml.Node.CData(content)            { print(content) }
}
```

`Xml.deserialize_detailed(s)` is an additive companion to `Xml.deserialize`: on success it returns the typed `Xml.Node` tree directly (no separate `Xml.to_node` step), and on failure it returns an **`Xml.ParseError`** record — `message: string`, `line: integer`, `column: integer` (both 1-based) — so malformed markup can be diagnosed at the offending byte rather than with a bare string. It returns `result<Xml.Node, Xml.ParseError>` and mirrors `Json.parse_detailed` / `Csv.deserialize_detailed`.

---

## 42 — Color

A typed RGBA colour value with validating constructors and derivations. Every value serialises to a CSS string the GraphicalUi web-view already accepts, so `Solaris` themes can be _computed_ rather than hand-written. Like `Decimal` and `Math.Fraction`, `Color` is data plus free functions with no operator overloading. The record is `Color.Color { red: integer, green: integer, blue: integer, alpha: number }` — channels are 0–255 integers and `alpha` is a 0–1 number.

> **Color vs Terminal.Color** — `Color` is a general RGBA value for GUI/CSS work. `Terminal.Color` is a fixed choice of 16 named ANSI terminal colours, used only by the `Terminal` module.

| Function                        | Parameter Types                             | Return Type          | Description                                                              |
| ------------------------------- | ------------------------------------------- | -------------------- | ------------------------------------------------------------------------ |
| `Color.contrast_ratio(a, b)`    | `(Color.Color, Color.Color)`                | `number`             | WCAG contrast ratio (1:1 to 21:1)                                        |
| `Color.darken(c, amount)`       | `(Color.Color, number)`                     | `Color.Color`        | Blend toward black by `amount` (clamped to [0, 1])                       |
| `Color.from_cmyk(c)`            | `(Color.Cmyk)`                              | `Color.Color`        | Convert a CMYK colour to RGBA (alpha 1.0)                                |
| `Color.from_hex(hex)`           | `(string)`                                  | `result<Color.Color>` | Parse `#rgb`, `#rgba`, `#rrggbb`, or `#rrggbbaa` (leading `#` optional) |
| `Color.from_hsl(h)`             | `(Color.Hsl)`                               | `Color.Color`        | Convert an HSL colour to RGBA (alpha 1.0)                                |
| `Color.from_hsv(h)`             | `(Color.Hsv)`                               | `Color.Color`        | Convert an HSV (HSB) colour to RGBA (alpha 1.0)                          |
| `Color.from_name(name)`         | `(Color.Name)`                              | `Color.Color`        | Build an opaque colour from a curated named colour (`Color.Name`)       |
| `Color.gradient(angle, stops)`  | `(number, array<Color.Stop>)`               | `Color.Gradient`     | Build a multi-stop linear gradient at `angle` degrees                   |
| `Color.gradient_at(g, position)` | `(Color.Gradient, number)`                 | `Color.Color`        | Sample the interpolated colour at `position` (0–1, clamped)             |
| `Color.gradient_to_css(g)`      | `(Color.Gradient)`                          | `string`             | Serialise to a CSS `linear-gradient(...)` string                        |
| `Color.lighten(c, amount)`      | `(Color.Color, number)`                     | `Color.Color`        | Blend toward white by `amount` (clamped to [0, 1])                       |
| `Color.mix(a, b, t)`            | `(Color.Color, Color.Color, number)`        | `Color.Color`        | Linear blend of `a` and `b` at `t` (clamped to [0, 1])                   |
| `Color.rgb(r, g, b)`            | `(integer, integer, integer)`               | `result<Color.Color>` | Construct an opaque colour; fail if a channel is outside 0–255         |
| `Color.rgba(r, g, b, a)`        | `(integer, integer, integer, number)`       | `result<Color.Color>` | Construct with alpha; fail if a channel is out of range or `a` ∉ [0, 1] |
| `Color.rotate_hue(c, degrees)`  | `(Color.Color, number)`                     | `Color.Color`        | Rotate the hue by `degrees`, preserving saturation, lightness, and alpha |
| `Color.stop(color, position)`   | `(Color.Color, number)`                     | `Color.Stop`         | Build a gradient colour stop at `position` (0–1, clamped)               |
| `Color.to_cmyk(c)`              | `(Color.Color)`                             | `Color.Cmyk`         | Convert an RGBA colour to CMYK (alpha dropped)                           |
| `Color.to_css(c)`               | `(Color.Color)`                             | `string`             | CSS string: `rgb(r, g, b)`, or `rgba(...)` when not fully opaque         |
| `Color.to_hex(c)`               | `(Color.Color)`                             | `string`             | `#rrggbb`, or `#rrggbbaa` when the colour is not fully opaque            |
| `Color.to_hsl(c)`               | `(Color.Color)`                             | `Color.Hsl`          | Convert an RGBA colour to HSL (alpha dropped)                            |
| `Color.to_hsv(c)`               | `(Color.Color)`                             | `Color.Hsv`          | Convert an RGBA colour to HSV/HSB (alpha dropped)                        |

`rgb` / `rgba` / `from_hex` are validating constructors returning `result<Color.Color>`; the derivations (`lighten` / `darken` / `mix`) take already-validated colours and clamp their `amount` / `t` argument, so they return a `Color.Color` directly. `contrast_ratio` computes the WCAG 2.x relative-luminance ratio (alpha ignored) — black on white is 21:1, a colour against itself is 1:1 — feeding accessibility checks. The `to_css` output drops straight into the theme and per-widget style dictionaries the webview already consumes.

**`Color.Hsl`** is the hue/saturation/lightness sibling of `Color.Color` — `hue: number` (degrees, 0–360), `saturation: number` and `lightness: number` (0–1 ratios). `Color.to_hsl` / `Color.from_hsl` convert between the two spaces, and `Color.rotate_hue(c, degrees)` shifts the hue (wrapping at 360°) while preserving saturation, lightness, and the original alpha — the natural way to build a rainbow, pastel, or complementary colour that is awkward in RGB. HSL drops alpha (so `to_hsl` discards it and `from_hsl` produces an opaque colour); everything still serialises through the same RGBA `to_css` path the webview consumes.

**`Color.Hsv`** is the hue/saturation/**value** (HSB) sibling of `Color.Color` — `hue: number` (degrees, 0–360), `saturation: number` and `value: number` (0–1 ratios). It is the model most colour pickers and palette generators use, so `Color.to_hsv` / `Color.from_hsv` are the natural pair for building tints and shades by "value". Like HSL it drops alpha (`from_hsv` produces an opaque colour), and both spaces serialise through the same RGBA `to_css` path.

**`Color.Cmyk`** is the cyan/magenta/yellow/**key** (black) sibling of `Color.Color` — `cyan: number`, `magenta: number`, `yellow: number`, and `key: number` (all 0–1 ratios). It is the subtractive model used by print production, so `Color.to_cmyk` / `Color.from_cmyk` are the natural pair for previewing how an on-screen colour will separate to ink. Like HSL/HSV it drops alpha (`from_cmyk` produces an opaque colour), and it serialises through the same RGBA `to_css` path.

**`Color.Name`** is a curated palette of common named colours as an exhaustive choice — `Black`, `White`, `Red`, `Green`, `Lime`, `Blue`, `Yellow`, `Cyan`, `Magenta`, `Gray`, `Silver`, `Orange`, `Purple`, `Pink`, `Brown` (a subset of the CSS named colours, not all 140). `Color.from_name(name)` maps a variant to its opaque `Color.Color`, giving beginners a typo-proof, autocompleted alternative to remembering hex strings — a misspelled colour is a compile error, not a runtime surprise. The values are CSS-canonical, so `Color.Name.Green` is `0,128,0` and `Color.Name.Lime` is `0,255,0` (matching the web platform). It parallels the exhaustive `Terminal.Color` palette; for any colour outside the curated set, `Color.rgb` / `Color.from_hex` remain.

**`Color.Stop`** (`color: Color.Color`, `position: number`) and **`Color.Gradient`** (`angle: number`, `stops: array<Color.Stop>`) add a multi-stop linear gradient built from the existing `Color.Color` record. `Color.stop(color, position)` pairs a colour with its 0–1 position along the gradient axis (position clamped to [0, 1]), and `Color.gradient(angle, stops)` assembles them at an `angle` in degrees. `Color.gradient_to_css(g)` serialises to a CSS `linear-gradient(...)` string the GraphicalUi web-view already draws — so a gradient background, chart fill, or header can be _computed_ instead of hand-written — and `Color.gradient_at(g, position)` samples the interpolated colour at any 0–1 position (clamping to the first/last stop outside their range, and linearly interpolating each channel and alpha between the two surrounding stops). Pure data plus free functions, reusing `Color.Color` and its CSS-serialisation convention.

```luma
Color.Color red = Result.unwrap(Color.rgb(255, 0, 0))
Color.Color blue = Result.unwrap(Color.rgb(0, 0, 255))
Color.Gradient g = Color.gradient(90.0, [Color.stop(red, 0.0), Color.stop(blue, 1.0)])
string css = Color.gradient_to_css(g)   # "linear-gradient(90.0deg, rgb(255, 0, 0) 0.0%, rgb(0, 0, 255) 100.0%)"
Color.Color mid = Color.gradient_at(g, 0.5)   # rgb(128, 0, 128)
```

```luma
Color.Color base = Result.unwrap(Color.from_hex("#0172ad"))
string css = base |> Color.darken(0.1) |> Color.to_css()   # "rgb(1, 102, 155)"

# Pick a readable text colour against a background.
number ratio = Color.contrast_ratio(base, Result.unwrap(Color.rgb(255, 255, 255)))
```

---

## See Also

- [Tutorial](Luma_Tutorial.md) — a beginner-friendly introduction that uses these modules step by step
- [User Manual](Luma_User_Manual.md) — language syntax and semantics
- [Error Handling](Luma_Error_Handling.md) — `result` / `optional` conventions used throughout the library
- [Solaris Guide](Luma_Solaris_Guide.md) — the beginner-first `Solaris` GUI surface
- [GraphicalUi Guide](Luma_GraphicalUi_Guide.md) — the low-level webview engine beneath the surface
- [Performance Guide](Luma_Performance_Guide.md) — runtime costs of standard library operations
- [Coding Guidelines](Luma_Coding_Guidelines.md) — idiomatic use of the standard library
- [Concurrent Debugging Guide](Luma_Concurrent_Debugging_Guide.md) — debugging tasks and channels that use these modules
- [REPL Guide](Luma_REPL_Guide.md) — explore these functions interactively
