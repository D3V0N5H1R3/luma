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
10. [Dictionary](#10--dictionary)
11. [Encoder](#11--encoder)
12. [FileSystem](#12--filesystem)
13. [Graph](#13--graph)
14. [Solaris and GraphicalUi](#14--solaris-and-graphicalui)
15. [Hash](#15--hash)
16. [HashSet](#16--hashset)
17. [Http](#17--http)
18. [Console](#18--console)
19. [Json](#19--json)
20. [KeyValueStore](#20--keyvaluestore)
21. [LinearAlgebra](#21--linearalgebra)
22. [LinkedList](#22--linkedlist)
23. [Log](#23--log)
24. [Math](#24--math)
25. [Optional](#25--optional)
26. [Process](#26--process)
27. [Queue](#27--queue)
28. [Random](#28--random)
29. [Reference](#29--reference)
30. [RegularExpression](#30--regularexpression)
31. [Resource](#31--resource)
32. [Result](#32--result)
33. [Set](#33--set)
34. [Socket](#34--socket)
35. [Stack](#35--stack)
36. [String](#36--string)
37. [Task](#37--task)
38. [Terminal](#38--terminal)
39. [Xml](#39--xml)

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
| `Array.join(arr, sep)`           | `(array<T>, string)`                   | `string`                       | Concatenate elements as strings separated by `sep`                         |
| `Array.last(arr)`                | `(array<T>)`                           | `result<T>`                    | Last element; fail if empty                                                |
| `Array.length(arr)`              | `(array<T>)`                           | `integer`                      | Number of elements                                                         |
| `Array.map(arr, fn)`             | `(array<T>, function(T) -> U)`         | `result<array<U>>`             | Transform each element; fail if callback throws                            |
| `Array.max(arr)`                 | `(array<T>)`                           | `result<T>`                    | Maximum value; fail if empty                                               |
| `Array.min(arr)`                 | `(array<T>)`                           | `result<T>`                    | Minimum value; fail if empty                                               |
| `Array.partition(arr, fn)`       | `(array<T>, function(T) -> boolean)`   | `result<(array<T>, array<T>)>` | Split into `(matches, rest)`; fail if predicate throws                     |
| `Array.pop(arr)`                 | `(array<T>)`                           | `result<(array<T>, T)>`        | Remove last element; fail if empty                                         |
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
| `Array.sum(arr)`                 | `(array<T>)`                           | `result<integer \| number>`    | Sum numeric elements; fail if non-numeric element found                    |
| `Array.take(arr, n)`             | `(array<T>, integer)`                  | `array<T>`                     | Take the first `n` elements                                                |
| `Array.take_while(arr, fn)`      | `(array<T>, function(T) -> boolean)`   | `result<array<T>>`             | Take elements while predicate is true; fail if predicate throws            |
| `Array.unique(arr)`              | `(array<T>)`                           | `array<T>`                     | Deduplicate elements                                                       |
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
| `BinaryTree.ceiling_value(t, v)`    | `(binary_tree, T)`                      | `result<T>`                          | Smallest value ≥ `v`; fail if none exists                      |
| `BinaryTree.contains(t, v)`         | `(binary_tree, T)`                      | `boolean`                            | Whether `v` is in the tree                                     |
| `BinaryTree.filter(t, fn)`          | `(binary_tree, function(T) -> boolean)` | `result<binary_tree>`                | Elements for which `fn` returns true; fail if predicate throws |
| `BinaryTree.floor_value(t, v)`      | `(binary_tree, T)`                      | `result<T>`                          | Largest value ≤ `v`; fail if none exists                       |
| `BinaryTree.from_array(arr)`        | `(array<T>)`                            | `binary_tree`                        | Build tree from array                                          |
| `BinaryTree.height(t)`              | `(binary_tree)`                         | `integer`                            | Tree height                                                    |
| `BinaryTree.inorder(t)`             | `(binary_tree)`                         | `array<T>`                           | In-order traversal (sorted)                                    |
| `BinaryTree.insert(t, v)`           | `(binary_tree, T)`                      | `binary_tree`                        | Insert value; returns new tree                                 |
| `BinaryTree.is_empty(t)`            | `(binary_tree)`                         | `boolean`                            | Whether the tree is empty                                      |
| `BinaryTree.length(t)`              | `(binary_tree)`                         | `integer`                            | Number of nodes                                                |
| `BinaryTree.level_order(t)`         | `(binary_tree)`                         | `array<T>`                           | Level-order (breadth-first) traversal                          |
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
| `Calculus.curl(point, fields)`                  | `(array<number>, array<function(array<number>) -> number>)`                        | `result<array<number>>` | Curl of a vector field at `point`; fail if dimensions mismatch       |
| `Calculus.derivative(x, fn)`                    | `(number, function(number) -> number)`                                             | `number`                | Numerical first derivative at `x`                                    |
| `Calculus.derivative_with(x, h, fn)`            | `(number, number, function(number) -> number)`                                     | `number`                | First derivative with custom step `h`                                |
| `Calculus.divergence(point, fields)`            | `(array<number>, array<function(array<number>) -> number>)`                        | `number`                | Divergence of a vector field at `point`                              |
| `Calculus.gradient(point, fn)`                  | `(array<number>, function(array<number>) -> number)`                               | `array<number>`         | Numerical partial derivatives at a point                             |
| `Calculus.hessian(point, fn)`                   | `(array<number>, function(array<number>) -> number)`                               | `array<array<number>>`  | Hessian matrix of second partial derivatives at `point`              |
| `Calculus.integrate(a, b, fn)`                  | `(number, number, function(number) -> number)`                                     | `number`                | Definite integral (Simpson's rule)                                   |
| `Calculus.integrate_with(a, b, n, fn)`          | `(number, number, integer, function(number) -> number)`                            | `number`                | Definite integral with `n` subdivisions                              |
| `Calculus.limit(x, fn)`                         | `(number, function(number) -> number)`                                             | `result<number>`        | Numerical limit (Richardson extrapolation)                           |
| `Calculus.maximize(a, b, fn)`                   | `(number, number, function(number) -> number)`                                     | `(number, number)`      | Maximise over `[a, b]`; returns `(x_max, f(x_max))`                  |
| `Calculus.minimize(a, b, fn)`                   | `(number, number, function(number) -> number)`                                     | `(number, number)`      | Minimise over `[a, b]` (golden section); returns `(x_min, f(x_min))` |
| `Calculus.newton(x0, fn)`                       | `(number, function(number) -> number)`                                             | `result<number>`        | Root finding (Newton's method)                                       |
| `Calculus.partial_derivative(point, index, fn)` | `(array<number>, integer, function(array<number>) -> number)`                      | `number`                | Partial derivative along axis `index` at `point`                     |
| `Calculus.root(a, b, fn)`                       | `(number, number, function(number) -> number)`                                     | `result<number>`        | Root finding (bisection method)                                      |
| `Calculus.second_derivative(x, fn)`             | `(number, function(number) -> number)`                                             | `number`                | Numerical second derivative at `x`                                   |
| `Calculus.sum_series(start, n, fn)`             | `(integer, integer, function(number) -> number)`                                   | `number`                | Sum `fn(start)` + ... + `fn(start + n - 1)`                          |
| `Calculus.taylor(centre, n, fn)`                | `(number, integer, function(number) -> number)`                                    | `array<number>`         | Taylor series coefficients (1–20 terms)                              |

Callbacks that return `result<number>` (such as `Math.sine`) are automatically unwrapped.

## 5 — Channel

Thread-safe FIFO queues for passing values between tasks.

| Function                          | Parameter Types            | Return Type       | Description                                                                               |
| --------------------------------- | -------------------------- | ----------------- | ----------------------------------------------------------------------------------------- |
| `Channel.close(ch)`               | `(channel<T>)`             | `none`            | Close the channel                                                                         |
| `Channel.is_closed(ch)`           | `(channel<T>)`             | `boolean`         | Whether the channel is closed                                                             |
| `Channel.is_empty(ch)`            | `(channel<T>)`             | `boolean`         | Whether no values are buffered                                                            |
| `Channel.length(ch)`              | `(channel<T>)`             | `integer`         | Number of buffered values                                                                 |
| `Channel.new()`                   | `()`                       | `channel<T>`      | Create unbounded channel (no capacity limit)                                              |
| `Channel.new_buffered(cap)`       | `(integer)`                | `channel<T>`      | Create buffered channel; throws if `cap ≤ 0`                                              |
| `Channel.receive(ch)`             | `(channel<T>)`             | `T`               | Blocking receive; throws `ChannelClosedError` if closed and drained                       |
| `Channel.receive_all(ch)`         | `(channel<T>)`             | `array<T>`        | Drain all buffered values                                                                 |
| `Channel.receive_timeout(ch, ms)` | `(channel<T>, integer)`    | `result<T>`       | Timed receive; fail on timeout, throws `ChannelClosedError` if closed                     |
| `Channel.select(channels)`        | `(array<channel<T>>)`      | `result<T>`       | Wait for the first ready channel; returns `(index, value)`; fail if all are closed        |
| `Channel.send(ch, v)`             | `(channel<T>, T)`          | `boolean`         | Blocking send; returns `false` if the channel is closed                                      |
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

| Function                                     | Parameter Types             | Return Type      | Description                                             |
| -------------------------------------------- | --------------------------- | ---------------- | ------------------------------------------------------- |
| `Compression.compressed_size(data)`          | `(string)`                  | `integer`        | Compressed size in bytes                                |
| `Compression.decode_rle(s)`                  | `(string)`                  | `result<string>` | Run-length decode                                       |
| `Compression.deflate(data)`                  | `(string)`                  | `string`         | Deflate-compress data                                   |
| `Compression.deflate_with(data, level)`      | `(string, integer)`         | `result<string>` | Deflate-compress with explicit level (0–9)              |
| `Compression.encode_rle(s)`                  | `(string)`                  | `string`         | Run-length encode (e.g. `"aaabbbcc"` → `"3a3b2c"`)      |
| `Compression.gunzip(data)`                   | `(string)`                  | `result<string>` | Gunzip-decompress data                                  |
| `Compression.gunzip_file(path)`              | `(string)`                  | `result<string>` | Gunzip-decompress file contents                         |
| `Compression.gzip(data)`                     | `(string)`                  | `string`         | Gzip-compress data                                      |
| `Compression.gzip_file(in, out)`             | `(string, string)`          | `result<string>` | Gzip-compress file to output path                       |
| `Compression.gzip_file_with(in, out, level)` | `(string, string, integer)` | `result<string>` | Gzip-compress a file to `out` with explicit level (0–9) |
| `Compression.gzip_with(data, level)`         | `(string, integer)`         | `result<string>` | Gzip-compress with explicit level (0–9)                 |
| `Compression.inflate(data)`                  | `(string)`                  | `result<string>` | Inflate-decompress data                                 |

## 7 — Converter

Convert values between different types (e.g. string → integer, integer → string).

> **Converter vs Encoder** — `Converter` changes the **type** of a value (e.g. `"255"` → `255`). For transforming the **representation** of a string while keeping it a string (e.g. Base64 or URL percent-encoding), use `Encoder` instead.

| Function                               | Parameter Types | Return Type       | Description                                                  |
| -------------------------------------- | --------------- | ----------------- | ------------------------------------------------------------ |
| `Converter.character_to_codepoint(ch)` | `(string)`      | `result<integer>` | Unicode codepoint of character; fail on empty string         |
| `Converter.codepoint_to_character(cp)` | `(integer)`     | `result<string>`  | Character from codepoint; fail on invalid codepoint          |
| `Converter.from_binary(s)`             | `(string)`      | `result<integer>` | Parse binary string (e.g. `"1010"` → 10)                     |
| `Converter.from_hexadecimal(s)`        | `(string)`      | `result<integer>` | Parse hex string (e.g. `"ff"` → 255)                         |
| `Converter.from_roman(s)`              | `(string)`      | `result<integer>` | Parse Roman numeral (e.g. `"XIV"` → 14)                      |
| `Converter.number_to_words(n)`         | `(integer)`     | `string`          | English words (e.g. `42` → `"forty two"`)                    |
| `Converter.ordinal(n)`                 | `(integer)`     | `string`          | Ordinal suffix (e.g. `3` → `"3rd"`)                          |
| `Converter.to_binary(n)`               | `(integer)`     | `string`          | Binary representation (e.g. `10` → `"1010"`)                 |
| `Converter.to_boolean(s)`              | `(string)`      | `result<boolean>` | Parse `"true"`/`"false"`; throws if argument is not a string |
| `Converter.to_hexadecimal(n)`          | `(integer)`     | `string`          | Hex representation (e.g. `255` → `"ff"`)                     |
| `Converter.to_integer(v)`              | `(number\       | string)`          | `result<integer>`                                            |
| `Converter.to_number(v)`               | `(integer\      | string)`          | `result<number>`                                             |
| `Converter.to_roman(n)`                | `(integer)`     | `result<string>`  | Roman numeral; fail if value outside [1, 3999]               |
| `Converter.to_string(v)`               | `(T)`           | `string`          | String representation of any value                           |

## 8 — Csv

Parse and serialise comma-separated values.

| Function                         | Parameter Types                              | Return Type                         | Description                                                       |
| -------------------------------- | -------------------------------------------- | ----------------------------------- | ----------------------------------------------------------------- |
| `Csv.count_rows(s)`              | `(string)`                                   | `result<integer>`                   | Number of data rows (excludes header)                             |
| `Csv.deserialize_records(s)`     | `(string)`                                   | `result<array<dictionary<string>>>` | Parse CSV with header row into records                            |
| `Csv.deserialize(s)`             | `(string)`                                   | `result<array<array<string>>>`      | Parse CSV string into rows of fields                              |
| `Csv.header(s)`                  | `(string)`                                   | `result<array<string>>`             | Extract header row                                                |
| `Csv.deserialize_with(s, opts)`  | `(string, dictionary<string>)`               | `result<array<array<string>>>`      | Parse with custom delimiter/quoting                               |
| `Csv.read_file(path)`            | `(string)`                                   | `result<array<dictionary<string>>>` | Read and parse CSV file                                           |
| `Csv.serialize(rows)`            | `(array<array<string>>)`                     | `result<string>`                    | Serialise rows to CSV string; fail if row is not array            |
| `Csv.serialize_records(records)` | `(array<dictionary<string>>)`                | `string`                            | Serialise records to CSV with header                              |
| `Csv.serialize_with(rows, opts)` | `(array<array<string>>, dictionary<string>)` | `result<string>`                    | Serialise with custom delimiter/quoting; fail if row is not array |
| `Csv.write_file(path, records)`  | `(string, array<dictionary<string>>)`        | `result<boolean>`                   | Write records to CSV file                                         |

Quoted fields, embedded commas, and escaped quotes are handled. Supported option keys for `parse_with`/`serialize_with`: `"delimiter"` (single char), `"quote"` (single char).

## 9 — DateTime

| Function                                   | Parameter Types                                          | Return Type                  | Description                                                              |
| ------------------------------------------ | -------------------------------------------------------- | ---------------------------- | ------------------------------------------------------------------------ |
| `DateTime.add_days(ts, n)`                 | `(number, number)`                                       | `number`                     | Add `n` days to a Unix timestamp                                         |
| `DateTime.add_hours(ts, n)`                | `(number, number)`                                       | `number`                     | Add `n` hours to a Unix timestamp                                        |
| `DateTime.add_months(ts, n)`               | `(number, integer)`                                      | `result<number>`             | Add `n` calendar months (clamps day); fail if out of range               |
| `DateTime.add_milliseconds(ts, n)`         | `(number, integer)`                                      | `number`                     | Add `n` milliseconds to a Unix timestamp                                 |
| `DateTime.add_seconds(ts, n)`              | `(number, number)`                                       | `number`                     | Add `n` seconds to a Unix timestamp                                      |
| `DateTime.add_years(ts, n)`                | `(number, integer)`                                      | `result<number>`             | Add `n` calendar years (clamps Feb 29); fail if out of range             |
| `DateTime.day_of_month(ts)`                | `(number)`                                               | `result<integer>`            | Day of month (1–31); fail if out of supported range (year 0001–9999)     |
| `DateTime.day_of_week(ts)`                 | `(number)`                                               | `result<integer>`            | 1 (Monday) to 7 (Sunday); fail if out of range                           |
| `DateTime.days_in_month(year, month)`      | `(integer, integer)`                                     | `result<integer>`            | Days in given month; fail if month not in [1, 12]                        |
| `DateTime.difference_days(t1, t2)`         | `(number, number)`                                       | `number`                     | Absolute difference in days                                              |
| `DateTime.difference_hours(t1, t2)`        | `(number, number)`                                       | `number`                     | Absolute difference in hours                                             |
| `DateTime.difference_months(t1, t2)`       | `(number, number)`                                       | `result<integer>`            | Absolute difference in calendar months; fail if out of range             |
| `DateTime.difference_milliseconds(t1, t2)` | `(number, number)`                                       | `number`                     | Difference in milliseconds                                               |
| `DateTime.difference_seconds(t1, t2)`      | `(number, number)`                                       | `number`                     | Absolute difference in seconds                                           |
| `DateTime.difference_years(t1, t2)`        | `(number, number)`                                       | `result<integer>`            | Absolute difference in calendar years; fail if out of range              |
| `DateTime.from_iso_string(s)`              | `(string)`                                               | `result<number>`             | Parse ISO 8601 string to Unix timestamp                                  |
| `DateTime.from_parts(y, m, d, h, min, s)`  | `(integer, integer, integer, integer, integer, integer)` | `result<number>`             | Build timestamp from components; fail if out of range                    |
| `DateTime.format(ts, pattern)`             | `(number, string)`                                       | `result<string>`             | Format timestamp; placeholders: YYYY, MM, DD, hh, mm, ss                 |
| `DateTime.hour(ts)`                        | `(number)`                                               | `result<integer>`            | Hour (0–23); fail if out of range                                        |
| `DateTime.is_after(a, b)`                  | `(number, number)`                                       | `boolean`                    | Whether timestamp `a` is after `b`                                       |
| `DateTime.is_before(a, b)`                 | `(number, number)`                                       | `boolean`                    | Whether timestamp `a` is before `b`                                      |
| `DateTime.is_leap_year(year)`              | `(integer)`                                              | `boolean`                    | Whether `year` is a leap year                                            |
| `DateTime.minute(ts)`                      | `(number)`                                               | `result<integer>`            | Minute (0–59); fail if out of range                                      |
| `DateTime.month(ts)`                       | `(number)`                                               | `result<integer>`            | Month (1–12); fail if out of range                                       |
| `DateTime.milliseconds_since_start()`      | `()`                                                     | `number`                     | Milliseconds since program start                                         |
| `DateTime.now_iso_string()`                | `()`                                                     | `result<string>`             | Current time as `"YYYY-MM-DDTHH:MM:SSZ"`                                 |
| `DateTime.now_unix()`                      | `()`                                                     | `number`                     | Current Unix timestamp                                                   |
| `DateTime.second(ts)`                      | `(number)`                                               | `result<integer>`            | Second (0–59); fail if out of range                                      |
| `DateTime.to_iso_string(ts)`               | `(number)`                                               | `result<string>`             | Format as `"YYYY-MM-DDTHH:MM:SSZ"`; fail if out of range                 |
| `DateTime.to_parts(ts)`                    | `(number)`                                               | `result<DateTime.TimeParts>` | Record with year, month, day, hour, minute, second; fail if out of range |
| `DateTime.year(ts)`                        | `(number)`                                               | `result<integer>`            | Four-digit year; fail if out of range                                    |

### Timezone Support (Fixed UTC Offsets)

All `DateTime` timestamps are in UTC. The following functions convert between UTC and a fixed UTC offset expressed in **minutes** (e.g. `330` for UTC+05:30, `-300` for UTC−05:00). Use `DateTime.offset_hours` to convert hours to minutes.

| Function                                                 | Parameter Types                                                  | Return Type      | Description                                            |
| -------------------------------------------------------- | ---------------------------------------------------------------- | ---------------- | ------------------------------------------------------ |
| `DateTime.from_offset(ts, offset)`                       | `(number, number)`                                               | `result<number>` | Local timestamp → UTC                                  |
| `DateTime.from_parts_offset(y, m, d, h, min, s, offset)` | `(integer, integer, integer, integer, integer, integer, number)` | `result<number>` | Build local date/time and return UTC timestamp         |
| `DateTime.offset_hours(h)`                               | `(number)`                                                       | `number`         | Convert hours to offset minutes (e.g. `5.5` → `330.0`) |
| `DateTime.to_iso_string_offset(ts, offset)`              | `(number, number)`                                               | `result<string>` | Format with UTC offset suffix                          |
| `DateTime.to_offset(ts, offset)`                         | `(number, number)`                                               | `result<number>` | UTC → local timestamp                                  |

Valid offsets range from −720 (UTC−12:00) to +840 (UTC+14:00) minutes. Out-of-range offsets return `failure`. A zero offset produces the `"Z"` suffix in ISO strings.

## 10 — Dictionary

Dictionaries preserve insertion order. All reads and writes use string keys.

| Function                          | Parameter Types                                   | Return Type                              | Description                                                                                     |
| --------------------------------- | ------------------------------------------------- | ---------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `Dictionary.has_value(d, v)`      | `(dictionary<T>, T)`                              | `boolean`                                | Whether any value equals `v`                                                                    |
| `Dictionary.count(d, fn)`         | `(dictionary<T>, function(string, T) -> boolean)` | `result<integer>`                        | Count entries matching predicate                                                                |
| `Dictionary.deep_merge(a, b)`     | `(dictionary<T>, dictionary<T>)`                  | `dictionary<T>`                          | Recursive merge; `b` wins on conflicts                                                          |
| `Dictionary.each(d, fn)`          | `(dictionary<T>, function(string, T) -> none)`    | `result<none>`                           | Iterate key–value pairs; fail if callback throws                                                |
| `Dictionary.filter(d, fn)`        | `(dictionary<T>, function(string, T) -> boolean)` | `result<dictionary<T>>`                  | Keep entries where `fn` returns `true`; fail if callback throws                                 |
| `Dictionary.find(d, fn)`          | `(dictionary<T>, function(string, T) -> boolean)` | `result<(string, T)>`                    | First entry where `fn` returns `true`; fail if not found                                        |
| `Dictionary.flip(d)`              | `(dictionary<V>)`                                 | `result<dictionary<string>>`             | Swap keys and values; fail if any value is not a string                                         |
| `Dictionary.from_entries(arr)`    | `(array<(string, T)>)`                            | `dictionary<T>`                          | Create from array of `(key, value)` tuples                                                      |
| `Dictionary.from_keys(keys, def)` | `(array<string>, T)`                              | `dictionary<T>`                          | Create from key list with default value                                                         |
| `Dictionary.get(d, k)`            | `(dictionary<T>, string)`                         | `result<T>`                              | Safe lookup; fail if key not found                                                              |
| `Dictionary.get_or(d, k, def)`    | `(dictionary<T>, string, T)`                      | `T`                                      | Lookup with default                                                                             |
| `Dictionary.has(d, k)`            | `(dictionary<T>, string)`                         | `boolean`                                | Key membership test                                                                             |
| `Dictionary.invert(d)`            | `(dictionary<string>)`                            | `dictionary<string>`                     | Swap keys and values                                                                            |
| `Dictionary.is_empty(d)`          | `(dictionary<T>)`                                 | `boolean`                                | Whether the dictionary is empty                                                                 |
| `Dictionary.keys(d)`              | `(dictionary<T>)`                                 | `array<string>`                          | Array of keys                                                                                   |
| `Dictionary.length(d)`            | `(dictionary<T>)`                                 | `integer`                                | Number of entries                                                                               |
| `Dictionary.map(d, fn)`           | `(dictionary<T>, function(string, T) -> U)`       | `result<dictionary<U>>`                  | Transform every entry; `fn` receives `(key, value)`, returns new value; fail if callback throws |
| `Dictionary.map_values(d, fn)`    | `(dictionary<T>, function(T) -> U)`               | `result<dictionary<U>>`                  | Transform every value; fail if callback throws                                                  |
| `Dictionary.merge(a, b)`          | `(dictionary<T>, dictionary<T>)`                  | `dictionary<T>`                          | Merge; `b` wins on conflicts                                                                    |
| `Dictionary.omit(d, keys)`        | `(dictionary<T>, array<string>)`                  | `dictionary<T>`                          | New dictionary excluding entries whose keys are in `keys`                                       |
| `Dictionary.partition(d, fn)`     | `(dictionary<T>, function(string, T) -> boolean)` | `result<(dictionary<T>, dictionary<T>)>` | Split into `(matches, rest)`; `fn` receives `(key, value)`; fail if predicate throws            |
| `Dictionary.pick(d, keys)`        | `(dictionary<T>, array<string>)`                  | `dictionary<T>`                          | New dictionary containing only entries whose keys are in `keys`                                 |
| `Dictionary.reduce(d, init, fn)`  | `(dictionary<T>, U, function(U, string, T) -> U)` | `result<U>`                              | Fold entries; `fn` receives `(accumulator, key, value)`; fail if callback throws                |
| `Dictionary.remove(d, k)`         | `(dictionary<T>, string)`                         | `dictionary<T>`                          | New dictionary without key                                                                      |
| `Dictionary.set(d, k, v)`         | `(dictionary<T>, string, T)`                      | `dictionary<T>`                          | New dictionary with key set                                                                     |
| `Dictionary.to_array(d)`          | `(dictionary<T>)`                                 | `array<KeyValue>`                        | Each element is a record with `.key` (`string`) and `.value` fields                             |
| `Dictionary.to_entries(d)`        | `(dictionary<T>)`                                 | `array<(string, T)>`                     | Each element is a `(key, value)` tuple                                                          |
| `Dictionary.values(d)`            | `(dictionary<T>)`                                 | `array<T>`                               | Array of values                                                                                 |

## 11 — Encoder

Transform the representation of a string without changing its type (e.g. Base64, URL percent-encoding).

> **Encoder vs Converter** — `Encoder` transforms **string representations** (e.g. binary data → Base64 text). For changing the **type** of a value (e.g. string → integer), use `Converter` instead.

| Function                      | Parameter Types | Return Type      | Description                            |
| ----------------------------- | --------------- | ---------------- | -------------------------------------- |
| `Encoder.decode_base64(s)`    | `(string)`      | `result<string>` | Decode Base64 string                   |
| `Encoder.decode_base64url(s)` | `(string)`      | `result<string>` | Decode URL-safe Base64 string          |
| `Encoder.decode_url(s)`       | `(string)`      | `result<string>` | Decode percent-encoded string          |
| `Encoder.encode_base64(s)`    | `(string)`      | `result<string>` | Encode to Base64                       |
| `Encoder.encode_base64url(s)` | `(string)`      | `result<string>` | Encode to URL-safe Base64 (no padding) |
| `Encoder.encode_url(s)`       | `(string)`      | `result<string>` | RFC 3986 percent-encoding              |

## 12 — FileSystem

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
| `FileSystem.list_directories(path)`       | `(string)`                | `result<array<string>>` | List subdirectories                                   |
| `FileSystem.list_files(path)`             | `(string)`                | `result<array<string>>` | List files in a directory                             |
| `FileSystem.list_recursively(path)`       | `(string)`                | `result<array<string>>` | Recursively list all files beneath a directory        |
| `FileSystem.name(path)`                   | `(string)`                | `string`                | File name (e.g. `"file.txt"`)                         |
| `FileSystem.normalize(path)`              | `(string)`                | `string`                | Normalise path (e.g. `"a/b/../c"` → `"a/c"`)          |
| `FileSystem.parent(path)`                 | `(string)`                | `string`                | Parent directory                                      |
| `FileSystem.read_file(path)`              | `(string)`                | `result<string>`        | Read entire file as string                            |
| `FileSystem.read_file_limited(path, max)` | `(string, integer)`       | `result<string>`        | Read file; fail if it exceeds `max` bytes             |
| `FileSystem.read_lines(path)`             | `(string)`                | `result<array<string>>` | Read file as array of lines                           |
| `FileSystem.relative(path, base)`         | `(string, string)`        | `string`                | Relative path from `base`                             |
| `FileSystem.rename(old, new)`             | `(string, string)`        | `result<boolean>`       | Rename a file                                         |
| `FileSystem.rename_directory(old, new)`   | `(string, string)`        | `result<boolean>`       | Rename a directory; fail if path is not a directory   |
| `FileSystem.size(path)`                   | `(string)`                | `result<integer>`       | File size in bytes                                    |
| `FileSystem.stem(path)`                   | `(string)`                | `string`                | File name without extension (e.g. `"hello"`)          |
| `FileSystem.write_file(path, data)`       | `(string, string)`        | `result<boolean>`       | Write string to file                                  |
| `FileSystem.write_lines(path, lines)`     | `(string, array<string>)` | `result<boolean>`       | Write array of lines to file                          |

`copy`, `delete`, `delete_directory`, `list_directories`, and `list_files` reject symbolic links and return `failure` to prevent symlink-following attacks.

> **Security note** — `append_file`, `read_file`, `read_lines`, `write_file`, and `write_lines` validate that the resolved path stays within the current working directory, which blocks cross-directory symlink traversal (e.g. a symlink pointing to `/etc/passwd` is rejected). However, a symbolic link that points to another file **within** the working directory is followed transparently. If your program accepts a user-supplied file path, validate that the resolved path refers to the expected file before reading or writing.

## 13 — Graph

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
| `Graph.has_cycle(g)`                     | `(graph)`                         | `boolean`                                | Whether the graph contains a cycle                          |
| `Graph.has_edge(g, from, to)`            | `(graph, string, string)`         | `boolean`                                | Whether edge exists                                         |
| `Graph.has_vertex(g, v)`                 | `(graph, string)`                 | `boolean`                                | Whether vertex exists                                       |
| `Graph.is_directed(g)`                   | `(graph)`                         | `boolean`                                | Whether the graph is directed                               |
| `Graph.minimum_spanning_tree(g)`         | `(graph)`                         | `result<graph>`                          | Minimum spanning tree; undirected graphs only               |
| `Graph.neighbors(g, v)`                  | `(graph, string)`                 | `result<array<string>>`                  | Adjacent vertices; fail if not found                        |
| `Graph.remove_edge(g, from, to)`         | `(graph, string, string)`         | `graph`                                  | Remove edge                                                 |
| `Graph.remove_vertex(g, v)`              | `(graph, string)`                 | `graph`                                  | Remove vertex and its edges                                 |
| `Graph.shortest_path(g, from, to)`       | `(graph, string, string)`         | `result<array<string>>`                  | Shortest path between vertices                              |
| `Graph.strongly_connected_components(g)` | `(graph)`                         | `result<array<array<string>>>`           | Groups of mutually reachable vertices; directed graphs only |
| `Graph.to_adjacency_list(g)`             | `(graph)`                         | `dictionary<array<string>>`              | Convert to adjacency list                                   |
| `Graph.topological_sort(g)`              | `(graph)`                         | `result<array<string>>`                  | Topological ordering; directed only; fail if cycle          |
| `Graph.undirected()`                     | `()`                              | `graph`                                  | Create an empty undirected graph                            |
| `Graph.vertex_count(g)`                  | `(graph)`                         | `integer`                                | Number of vertices                                          |
| `Graph.vertices(g)`                      | `(graph)`                         | `array<string>`                          | All vertex labels                                           |

## 14 — Solaris and GraphicalUi

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

## 15 — Hash

Cryptographic and non-cryptographic hash digests, HMAC, and verification.

| Function                        | Parameter Types            | Return Type      | Description                    |
| ------------------------------- | -------------------------- | ---------------- | ------------------------------ |
| `Hash.algorithms()`             | `()`                       | `array<string>`  | List supported algorithm names |
| `Hash.crc32(s)`                 | `(string)`                 | `integer`        | CRC-32 checksum                |
| `Hash.hmac_sha256(key, msg)`    | `(string, string)`         | `string`         | HMAC-SHA-256                   |
| `Hash.hmac_sha512(key, msg)`    | `(string, string)`         | `string`         | HMAC-SHA-512                   |
| `Hash.md5(s)`                   | `(string)`                 | `string`         | MD5 digest (32-char hex)       |
| `Hash.sha1(s)`                  | `(string)`                 | `string`         | SHA-1 digest (40-char hex)     |
| `Hash.sha256_file(path)`        | `(string)`                 | `result<string>` | SHA-256 of file contents       |
| `Hash.sha256(s)`                | `(string)`                 | `string`         | SHA-256 digest (64-char hex)   |
| `Hash.sha512_file(path)`        | `(string)`                 | `result<string>` | SHA-512 of file contents       |
| `Hash.sha512(s)`                | `(string)`                 | `string`         | SHA-512 digest (128-char hex)  |
| `Hash.verify(algo, data, hash)` | `(string, string, string)` | `boolean`        | Verify hash matches data       |

## 16 — HashSet

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

## 17 — Http

Plain HTTP/1.1 client built on raw sockets. Only `http://` is supported; `https://` URLs return an error result.

| Function                              | Parameter Types                            | Return Type             | Description                                                          |
| ------------------------------------- | ------------------------------------------ | ----------------------- | -------------------------------------------------------------------- |
| `Http.basic_auth(user, pass)`         | `(string, string)`                         | `string`                | Build a Basic `Authorization` header value                           |
| `Http.bearer_auth(token)`             | `(string)`                                 | `string`                | Build a Bearer `Authorization` header value                          |
| `Http.build_query(params)`            | `(dictionary<string>)`                     | `string`                | Build query string (e.g. `"a=1&b=2"`)                                |
| `Http.delete(url)`                    | `(string)`                                 | `result<Http.Response>` | DELETE request                                                       |
| `Http.delete_with(url, headers)`      | `(string, dictionary<string>)`             | `result<Http.Response>` | DELETE with custom headers                                           |
| `Http.download(url, path)`            | `(string, string)`                         | `result<string>`        | Download file to local path                                          |
| `Http.get(url)`                       | `(string)`                                 | `result<Http.Response>` | GET request                                                          |
| `Http.get_with(url, headers)`         | `(string, dictionary<string>)`             | `result<Http.Response>` | GET with custom headers                                              |
| `Http.head(url)`                      | `(string)`                                 | `result<Http.Response>` | HEAD request                                                         |
| `Http.parse_query(qs)`                | `(string)`                                 | `dictionary<string>`    | Parse query string into dictionary                                   |
| `Http.parse_url(url)`                 | `(string)`                                 | `Http.UrlParts`         | Parse URL into record with `scheme`, `host`, `port`, `path`, `query` |
| `Http.patch(url, body)`               | `(string, string)`                         | `result<Http.Response>` | PATCH request                                                        |
| `Http.patch_with(url, body, headers)` | `(string, string, dictionary<string>)`     | `result<Http.Response>` | PATCH with body and custom headers                                   |
| `Http.post(url, body)`                | `(string, string)`                         | `result<Http.Response>` | POST request                                                         |
| `Http.post_with(url, body, headers)`  | `(string, string, dictionary<string>)`     | `result<Http.Response>` | POST with custom headers                                             |
| `Http.put(url, body)`                 | `(string, string)`                         | `result<Http.Response>` | PUT request                                                          |
| `Http.put_with(url, body, headers)`   | `(string, string, dictionary<string>)`     | `result<Http.Response>` | PUT with body and custom headers                                     |
| `Http.request(opts, headers)`         | `(dictionary<string>, dictionary<string>)` | `result<Http.Response>` | Generic request (`opts` has `"method"` and `"url"` keys)             |

`Http.Response` record fields: `status` (`integer`), `reason` (`string`), `body` (`string`), `headers` (`dictionary<string>`).

> **Security note** — HTTP header names and values are validated to reject carriage-return (`\r`) and line-feed (`\n`) characters. Supplying headers that contain these characters returns a `failure` result to prevent CRLF header injection.

> **Proxy support** — When the `HTTPS_PROXY`, `HTTP_PROXY`, or `ALL_PROXY` environment variables are set (lower-case variants are also honoured), requests are routed through the named HTTP proxy: `https` URLs use a `CONNECT` tunnel (TLS remains end-to-end with the origin server, so certificate verification is unaffected), and plain `http` URLs are forwarded with an absolute-form request line. `NO_PROXY` (comma-separated host or domain suffixes) bypasses the proxy for matching hosts. Proxy credentials supplied in the proxy URL's userinfo are sent via `Proxy-Authorization`. SSRF protection still applies to the request target: requests resolving to private, loopback, or otherwise reserved addresses are rejected even when a proxy is configured.

## 18 — Console

| Function                       | Parameter Types | Return Type       | Description                                                               |
| ------------------------------ | --------------- | ----------------- | ------------------------------------------------------------------------- |
| `Console.prompt(msg)`          | `(string)`      | `result<string>`  | Print prompt, read line from stdin; fail on EOF or if it exceeds the maximum string size |
| `Console.read_from_stdin()`    | `()`            | `result<string>`  | Read all of stdin; fail if it exceeds the maximum string size            |
| `Console.write_to_stderr(msg)` | `(string)`      | `result<boolean>` | Write to stderr                                                           |
| `Console.write_to_stdout(msg)` | `(string)`      | `result<boolean>` | Write to stdout                                                           |

> **Resource limit** — Console input is bounded by the maximum string size (see the [resource-limit table](Luma_Performance_Guide.md#6--resource-limits), `LUMA_LIMIT_MAX_STRING_SIZE`). `Console.prompt` and `Console.read_from_stdin` return `failure` if the input would exceed it.

> **Console vs FileSystem:** `Console` handles console I/O — reading from stdin and writing to stdout/stderr. `FileSystem` handles file content — reading, writing, and appending data — as well as file metadata and paths (checking existence, querying size, listing directories, copying, renaming, and manipulating path components). Use `Console` for interactive console I/O; use `FileSystem` to read, write, and manage files and directories.

## 19 — Json

Serialise and deserialise Luma values as JSON.

| Function                       | Parameter Types       | Return Type                  | Description                                                                      |
| ------------------------------ | --------------------- | ---------------------------- | -------------------------------------------------------------------------------- |
| `Json.deserialize(s)`          | `(string)`            | `result<T>`                  | Parse JSON string                                                                |
| `Json.get(json, path)`         | `(string, string)`    | `result<T>`                  | Navigate dot-separated path (e.g. `"user.age"`, `"items.0"`)                     |
| `Json.get_path(json, path)`    | `(string, string)`    | `result<T>`                  | Read a value using dot and `[index]` path syntax; fail if missing                |
| `Json.is_valid(s)`             | `(string)`            | `boolean`                    | Whether `s` is valid JSON                                                        |
| `Json.merge(a, b)`             | `(string, string)`    | `result<string>`             | Merge two JSON objects; `b` wins on conflicts                                    |
| `Json.serialize(v)`            | `(T)`                 | `string`                     | Serialise value to compact JSON                                                  |
| `Json.serialize_pretty(v)`     | `(T)`                 | `string`                     | Serialise value to formatted JSON                                                |
| `Json.set(json, path, v)`      | `(string, string, T)` | `result<string>`             | Replace key at path; return new JSON string                                      |
| `Json.set_path(json, path, v)` | `(string, string, T)` | `result<string>`             | Replace the value at a dot/`[index]` path; return new JSON; fail on invalid path |

Supported types: `integer`, `number`, `string`, `boolean`, `none` (→ JSON `null`), `array`, and `dictionary`. Nested structures are handled recursively.

`Json.get` navigates a dot-separated path (e.g. `"user.address.city"`) into a parsed JSON string and returns the value wrapped in a `result`. Array elements are accessed by numeric index (e.g. `"items.0"`).

`Json.set` navigates to a key inside a JSON object string and returns a new serialised JSON string with that key replaced by the given value. The path must point to an existing key inside a JSON object.

`Json.merge` merges two JSON object strings; keys from the second object overwrite those in the first. Both inputs must be JSON objects.

## 20 — KeyValueStore

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

## 21 — LinearAlgebra

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

## 22 — LinkedList

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

## 23 — Log

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
| `Log.set_output(target)`      | `(string)`         | `result<void>` | Redirect to file path, `"stderr"`, or `"stdout"`     |
| `Log.warn(msg)`               | `(string)`         | `none`         | Log at warn level                                    |

Levels are ordered: `Debug` < `Info` < `Warn` < `Error` < `Off`. The `Log.Level` choice type provides variants: `Log.Level.Debug`, `Log.Level.Info`, `Log.Level.Warn`, `Log.Level.Error`, `Log.Level.Off`. For convenience, `Log.set_level` also accepts lowercase strings (`"debug"`, `"info"`, `"warn"`, `"error"`, `"off"`).

`Log.set_output` accepts a file path or one of the special strings `"stderr"` (default) or `"stdout"`. When given a file path it appends to the file, creating it if it does not exist.

## 24 — Math

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
| `Math.clamp(x, lo, hi)`               | `(number, number, number)`       | `result<number>`  | Clamp `x` to `[lo, hi]`; fail if `lo > hi`                                       |
| `Math.correlation(xs, ys)`            | `(array<number>, array<number>)` | `result<number>`  | Pearson correlation coefficient; fail if arrays differ in length or < 2 elements |
| `Math.cosine(x)`                      | `(number)`                       | `result<number>`  | Cosine; fail if result is NaN or infinite                                        |
| `Math.cube_root(x)`                   | `(number)`                       | `number`          | Cube root of `x`                                                                 |
| `Math.degrees(rad)`                   | `(number)`                       | `number`          | Convert radians to degrees                                                       |
| `Math.exponential(x)`                 | `(number)`                       | `result<number>`  | e^x                                                                              |
| `Math.factorial(n)`                   | `(integer)`                      | `result<integer>` | n!; fail if `n < 0` or `n > 20`                                                  |
| `Math.floor(x)`                       | `(number)`                       | `result<integer>` | Round down to nearest integer; fail on overflow                                  |
| `Math.greatest_common_divisor(a, b)`  | `(integer, integer)`             | `result<integer>` | GCD of `a` and `b`                                                               |
| `Math.hyperbolic_cosine(x)`           | `(number)`                       | `result<number>`  | Hyperbolic cosine; fail if result is infinite                                    |
| `Math.hyperbolic_sine(x)`             | `(number)`                       | `result<number>`  | Hyperbolic sine; fail if result is infinite                                      |
| `Math.hyperbolic_tangent(x)`          | `(number)`                       | `number`          | Hyperbolic tangent (always bounded to [−1, 1])                                   |
| `Math.hypot(x, y)`                    | `(number, number)`               | `number`          | Hypotenuse √(x² + y²)                                                            |
| `Math.is_infinite(x)`                 | `(number)`                       | `boolean`         | Whether `x` is +∞ or −∞                                                          |
| `Math.is_not_a_number(x)`             | `(number)`                       | `boolean`         | Whether `x` is NaN                                                               |
| `Math.is_prime(n)`                    | `(integer)`                      | `boolean`         | Whether `n` is prime                                                             |
| `Math.least_common_multiple(a, b)`    | `(integer, integer)`             | `result<integer>` | LCM of `a` and `b`; fail on overflow                                             |
| `Math.lerp(a, b, t)`                  | `(number, number, number)`       | `result<number>`  | Linear interpolation; fail if `t` outside [0, 1]                                 |
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
| `Math.power(base, exp)`               | `(number, number)`               | `result<number>`  | `base` raised to `exp`; fail if result is NaN or Inf                             |
| `Math.radians(deg)`                   | `(number)`                       | `number`          | Convert degrees to radians                                                       |
| `Math.remainder(a, b)`                | `(integer \| number, integer \| number)` | `result<integer \| number>` | Remainder of `a` divided by `b`; fail if `b` is zero                             |
| `Math.remap(value, in_min, in_max, out_min, out_max)` | `(number, number, number, number, number)` | `result<number>` | Linearly re-map `value` from input range to output range; fail if `in_min == in_max` |
| `Math.round(x)`                       | `(number)`                       | `result<integer>` | Round to nearest integer; fail on overflow                                       |
| `Math.sign(x)`                        | `(number)`                       | `integer`         | −1, 0, or 1                                                                      |
| `Math.sine(x)`                        | `(number)`                       | `result<number>`  | Sine; fail if result is NaN or infinite                                          |
| `Math.smooth_step(edge0, edge1, x)`   | `(number, number, number)`       | `result<number>`  | Smoothstep interpolation between `edge0` and `edge1`; fail if `edge0 == edge1`   |
| `Math.square_root(x)`                 | `(number)`                       | `result<number>`  | Square root; fail if `x` is negative                                             |
| `Math.standard_deviation(arr)`        | `(array<number>)`                | `result<number>`  | Standard deviation; fail if empty                                                |
| `Math.sum(arr)`                       | `(array<number>)`                | `result<integer \| number>` | Sum of all elements; fail on a non-numeric element                               |
| `Math.tangent(x)`                     | `(number)`                       | `result<number>`  | Tangent; fail if result is NaN or infinite                                       |
| `Math.truncate(x)`                    | `(number)`                       | `result<integer>` | Truncate toward zero; fail on overflow                                           |
| `Math.variance(arr)`                  | `(array<number>)`                | `result<number>`  | Variance; fail if empty                                                          |

**Constants:**

| Constant        | Type     | Value             |
| --------------- | -------- | ----------------- |
| `Math.e`        | `number` | 2.718281828459045 |
| `Math.pi`       | `number` | 3.141592653589793 |
| `Math.tau`      | `number` | 6.283185307179586 |
| `Math.infinity` | `number` | ∞                 |

## 25 — Optional

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

## 26 — Process

| Function                                        | Parameter Types    | Return Type                     | Description                                                              |
| ----------------------------------------------- | ------------------ | ------------------------------- | ------------------------------------------------------------------------ |
| `Process.current_directory()`                   | `()`               | `result<string>`                | Current working directory                                                |
| `Process.exit(code)`                            | `(integer)`        | `none`                          | Terminate the program with exit code                                     |
| `Process.get_all_environment_variables()`       | `()`               | `dictionary<string>`            | All environment variables as a dictionary                                |
| `Process.get_arguments()`                       | `()`               | `array<string>`                 | Command-line arguments after the file name                               |
| `Process.get_environment_variable(name)`        | `(string)`         | `result<string>`                | Environment variable value; fail if not set                              |
| `Process.get_process_id()`                      | `()`               | `integer`                       | Current process ID                                                       |
| `Process.has_environment_variable(name)`        | `(string)`         | `boolean`                       | Whether the environment variable is set                                  |
| `Process.run(cmd)`                              | `(string)`         | `result<Process.ProcessResult>` | Execute shell command and capture stdout                                 |
| `Process.set_environment_variable(name, value)` | `(string, string)` | `result<none>`                  | Set environment variable; fail if name/value exceeds 32 KB or OS rejects |

> **Security warning:** `Process.run` passes its argument to the system shell (`cmd.exe` on Windows, `/bin/sh` on Unix). If any part of the string comes from user input, an attacker can inject shell commands using characters such as `;`, `&&`, `|`, or `$(...)`. **Never pass unsanitised user input to `Process.run`.** Validate and whitelist all inputs before use, or construct the command from a fixed set of known-safe values only.

`Process.ProcessResult` record fields: `exit_code` (`integer`), `output` (`string`).

## 27 — Queue

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

## 28 — Random

| Function                          | Parameter Types       | Return Type        | Description                                                                     |
| --------------------------------- | --------------------- | ------------------ | ------------------------------------------------------------------------------- |
| `Random.choice(arr)`              | `(array<T>)`          | `result<T>`        | Random element; fail if array is empty                                          |
| `Random.generate_boolean()`       | `()`                  | `boolean`          | Random `true` or `false`                                                        |
| `Random.generate_integer(lo, hi)` | `(integer, integer)`  | `result<integer>`  | Random integer in `[lo, hi]`; fail if `lo > hi`                                 |
| `Random.generate_number()`        | `()`                  | `number`           | Random number in `[0, 1)`                                                       |
| `Random.generate_string(len)`     | `(integer)`           | `result<string>`   | Random alphanumeric string; fail if `len < 0`. **Not** cryptographically secure |
| `Random.sample(arr, k)`           | `(array<T>, integer)` | `result<array<T>>` | `k` unique random elements; fail if `k > length`                                |
| `Random.shuffle(arr)`             | `(array<T>)`          | `array<T>`         | Shuffled copy                                                                   |
| `Random.generate_uuid()`          | `()`                  | `string`           | UUID v4 (random). **Not** cryptographically secure                              |
| `Random.secure_boolean()`         | `()`                  | `result<boolean>`  | Cryptographically secure random `true` or `false`                               |
| `Random.secure_integer(lo, hi)`   | `(integer, integer)`  | `result<integer>`  | Cryptographically secure uniform integer in `[lo, hi]`; fail if `lo > hi`       |
| `Random.secure_number()`          | `()`                  | `result<number>`   | Cryptographically secure random number in `[0, 1)`                              |
| `Random.secure_string(len)`       | `(integer)`           | `result<string>`   | Cryptographically secure alphanumeric string; fail if `len < 0`                 |
| `Random.secure_uuid()`            | `()`                  | `result<string>`   | UUID v4 from cryptographically secure random bytes                              |

**Cryptographically secure functions.** The `secure_*` variants use AES-CTR-DRBG (via Mbed TLS) seeded from platform entropy. They are suitable for generating tokens, secrets, and session identifiers. Requires TLS support (`LUMA_FEATURE_TLS=ON`, enabled by default).

## 29 — Reference

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

## 30 — RegularExpression

| Function                                          | Parameter Types            | Return Type                              | Description                                                   |
| ------------------------------------------------- | -------------------------- | ---------------------------------------- | ------------------------------------------------------------- |
| `RegularExpression.find(s, pattern)`              | `(string, string)`         | `result<RegularExpression.Match>`        | First match; fail if pattern is invalid                       |
| `RegularExpression.find_all(s, pattern)`          | `(string, string)`         | `result<array<RegularExpression.Match>>` | All matches; fail if pattern is invalid                       |
| `RegularExpression.is_valid(pattern)`             | `(string)`                 | `boolean`                                | Whether `pattern` is a valid regex                            |
| `RegularExpression.matches(s, pattern)`           | `(string, string)`         | `result<boolean>`                        | Whether `pattern` is found in `s`; fail if pattern is invalid |
| `RegularExpression.replace(s, pattern, repl)`     | `(string, string, string)` | `result<string>`                         | Replace first match; fail if pattern is invalid               |
| `RegularExpression.replace_all(s, pattern, repl)` | `(string, string, string)` | `result<string>`                         | Replace all matches; fail if pattern is invalid               |
| `RegularExpression.split(s, pattern)`             | `(string, string)`         | `result<array<string>>`                  | Split by regex; fail if pattern is invalid                    |

`find` and `find_all` return `RegularExpression.Match` records with fields `text` (the matched substring), `position` (zero-based index), `length` (character count of the match), and `groups` (an `array<RegularExpression.Match>` of capture-group matches). Each element of `groups` is itself a `Match` record with the same `text`, `position`, and `length` fields. When the pattern contains no capture groups, `groups` is an empty array.

```luma
RegularExpression.Match m =
    RegularExpression.find("alice@example.com", "([a-z]+)@([a-z]+)\\.([a-z]+)")
    |> Result.unwrap()

print(m.text)            # "alice@example.com"
print(m.groups[0].text)  # "alice"
print(m.groups[1].text)  # "example"
print(m.groups[2].text)  # "com"
```

> **Resource limits** — Regular expression patterns are capped at a maximum byte size (see the [resource-limit table](Luma_Performance_Guide.md#6--resource-limits), `LUMA_LIMIT_MAX_REGEX_PATTERN_SIZE`). Patterns exceeding the limit return `failure` (or `false` from `is_valid`). The regex engine uses the ECMAScript dialect provided by the C++ standard library. There is no built-in protection against catastrophic backtracking — patterns with nested quantifiers such as `(a+)+b` can take exponential time on non-matching input. When processing untrusted patterns, keep them simple and avoid nested repetition operators (`*`, `+`, `{n,m}` inside groups that are themselves repeated).

## 31 — Resource

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

## 32 — Result

See the [User Manual — §14 Result and Optional](Luma_User_Manual.md#14--result-and-optional).

## 33 — Set

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

## 34 — Socket

Cross-platform TCP and UDP networking.

| Function                               | Parameter Types                     | Return Type                | Description                                                  |
| -------------------------------------- | ----------------------------------- | -------------------------- | ------------------------------------------------------------ |
| `Socket.accept(srv)`                   | `(socket)`                          | `result<socket>`           | Accept incoming connection                                   |
| `Socket.close(s)`                      | `(socket)`                          | `none`                     | Close the socket                                             |
| `Socket.connect(host, port)`           | `(string, integer)`                 | `result<socket>`           | TCP connect (30 s timeout)                                   |
| `Socket.is_connected(s)`               | `(socket)`                          | `boolean`                  | Whether the socket handle is valid                           |
| `Socket.listen(host, port)`            | `(string, integer)`                 | `result<socket>`           | Bind and listen for TCP connections                          |
| `Socket.local_address(s)`              | `(socket)`                          | `result<string>`           | Local `"host:port"`                                          |
| `Socket.receive(s, max)`               | `(socket, integer)`                 | `result<string>`           | Receive up to `max` bytes                                    |
| `Socket.remote_address(s)`             | `(socket)`                          | `result<string>`           | Remote `"host:port"`                                         |
| `Socket.send(s, data)`                 | `(socket, string)`                  | `result<integer>`          | Send data; returns bytes sent                                |
| `Socket.set_timeout(s, ms)`            | `(socket, integer)`                 | `result<boolean>`          | Set send/recv timeout (does not affect connect)              |
| `Socket.udp_bind(s, host, port)`       | `(socket, string, integer)`         | `result<boolean>`          | Bind UDP socket to address                                   |
| `Socket.udp_create()`                  | `()`                                | `result<socket>`           | Create UDP socket                                            |
| `Socket.udp_receive(s, max)`           | `(socket, integer)`                 | `result<Socket.UdpPacket>` | Receive UDP packet; record has `data`, `host`, `port` fields |
| `Socket.udp_send(s, data, host, port)` | `(socket, string, string, integer)` | `result<integer>`          | Send UDP datagram                                            |

> **Resource limit** — A program may hold only a bounded number of open sockets at the same time (see the [resource-limit table](Luma_Performance_Guide.md#6--resource-limits), `LUMA_LIMIT_MAX_OPEN_SOCKETS`). Attempting to create more returns `failure("socket limit reached — too many open sockets")`. Close sockets that are no longer needed to stay within the limit.

---

## 35 — Stack

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

## 36 — String

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
| `String.count(s, sub)`              | `(string, string)`             | `integer`         | Number of non-overlapping occurrences of `sub`                                  |
| `String.dedent(s)`                  | `(string)`                     | `string`          | Remove common leading whitespace                                                |
| `String.ends_with(s, suffix)`       | `(string, string)`             | `boolean`         | Whether `s` ends with `suffix`                                                  |
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

> **Note:** `String.uppercase()` and `String.lowercase()` only transform ASCII characters (a–z, A–Z). Non-ASCII UTF-8 code points (e.g., `ü`, `é`, `ñ`) pass through unchanged. Use these functions only when working with ASCII text.

> **Resource limits** — `String.center`, `String.pad_left`, and `String.pad_right` cap their target `width`, and `String.repeat` caps its repeat count and result size. See the [resource-limit table](Luma_Performance_Guide.md#6--resource-limits) for Luma's resource limits and their `LUMA_LIMIT_*` overrides.

## 37 — Task

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

## 38 — Terminal

Terminal UI control — cursor movement, colors, styling, screen management, and mouse input.

| Function                                       | Parameter Types                       | Return Type                       | Description                                                             |
| ---------------------------------------------- | ------------------------------------- | --------------------------------- | ----------------------------------------------------------------------- |
| `Terminal.background_color(name, text)`        | `(string, string)`                    | `result<string>`                  | Named background color; fail if color name unknown                      |
| `Terminal.bell()`                              | `()`                                  | `none`                            | Audible bell                                                            |
| `Terminal.bold(text)`                          | `(string)`                            | `string`                          | Bold styled text                                                        |
| `Terminal.clear_line()`                        | `()`                                  | `none`                            | Clear entire current line                                               |
| `Terminal.clear_screen()`                      | `()`                                  | `none`                            | Clear screen and move to top-left                                       |
| `Terminal.clear_to_end_of_line()`              | `()`                                  | `none`                            | Clear from cursor to end of line                                        |
| `Terminal.clear_to_end_of_screen()`            | `()`                                  | `none`                            | Clear from cursor to end of screen                                      |
| `Terminal.color(name, text)`                   | `(string, string)`                    | `result<string>`                  | Named foreground color; fail if color name unknown                      |
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

Mouse events are returned as strings: `"mouse:left_press:ROW:COL"`, `"mouse:left_release:ROW:COL"`, `"mouse:middle_press:ROW:COL"`, `"mouse:right_press:ROW:COL"`, `"mouse:wheel_down:ROW:COL"`, `"mouse:wheel_up:ROW:COL"`. ROW and COL are 1-based integers. Parse with `String.split(key, ":")`.

Available named colors: `black`, `blue`, `bright_black` .. `bright_white`, `cyan`, `default`, `green`, `magenta`, `red`, `white`, `yellow`.

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

## 39 — Xml

Parse, build, query, and serialise XML documents. XML nodes are opaque values.

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

**Parsing and serialising:**

| Function                     | Parameter Types | Return Type      | Description              |
| ---------------------------- | --------------- | ---------------- | ------------------------ |
| `Xml.is_valid(s)`            | `(string)`      | `boolean`        | Whether `s` is valid XML |
| `Xml.deserialize(s)`         | `(string)`      | `result<xml>`    | Parse XML string         |
| `Xml.deserialize_file(path)` | `(string)`      | `result<xml>`    | Parse XML file           |
| `Xml.serialize(el)`          | `(xml)`         | `string`         | Compact XML string       |
| `Xml.serialize_pretty(el)`   | `(xml)`         | `string`         | Indented XML string      |
| `Xml.write_file(path, doc)`  | `(string, xml)` | `result<none>`   | Write XML to file        |

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
