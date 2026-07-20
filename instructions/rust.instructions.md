---
description: "Use when writing, reviewing, or modifying Rust source code (.rs files). Covers naming, style, ownership, error handling, unsafe code, and idiomatic Rust patterns."
applyTo: "**/*.rs"
---

# Working with Rust

These instructions govern how you write Rust source code. Every function, type, and module you produce must follow these principles. They are aligned with the official [Rust API Guidelines](https://rust-lang.github.io/api-guidelines/), [The Rust Programming Language](https://doc.rust-lang.org/book/), and the [Clippy lint collection](https://rust-lang.github.io/rust-clippy/).

---

## Table of Contents

1. [Simplicity First](#1--simplicity-first)
2. [Ownership and Borrowing](#2--ownership-and-borrowing)
3. [Naming Conventions](#3--naming-conventions)
4. [Consistent Style](#4--consistent-style)
5. [Error Handling](#5--error-handling)
6. [Types and Traits](#6--types-and-traits)
7. [Functions](#7--functions)
8. [Pattern Matching](#8--pattern-matching)
9. [Iterators and Closures](#9--iterators-and-closures)
10. [Concurrency](#10--concurrency)
11. [Unsafe Code](#11--unsafe-code)
12. [Modules and Crates](#12--modules-and-crates)
13. [Lifetimes](#13--lifetimes)
14. [Self-Documenting Code](#14--self-documenting-code)
15. [Whitespace as Structure](#15--whitespace-as-structure)
16. [Security Essentials](#16--security-essentials)
17. [Testing](#17--testing)
18. [Performance](#18--performance)
19. [Anti-Patterns](#19--anti-patterns)
20. [Checklist](#20--checklist)

---

## 1 — Simplicity First

Write the simplest code that solves the problem correctly.

- Prefer straightforward control flow over clever trait gymnastics or macro sorcery.
- If the standard library provides a solution, use it. Do not reinvent `Vec`, `HashMap`, or `Iterator`.
- Use the type system to encode invariants, but do not over-abstract with excessive generics.
- Lean on the compiler — let Rust's ownership and type system prevent bugs rather than writing defensive runtime checks.

**Test:** Before committing to an approach, ask — _is there a simpler way?_

---

## 2 — Ownership and Borrowing

Rust's ownership system is its defining feature. Work with it, not against it.

- Prefer borrowing (`&T`, `&mut T`) over owning (`T`) in function parameters when the function does not need ownership.
- Use `Clone` explicitly when ownership transfer is impractical. Never clone to silence the borrow checker without understanding why ownership is needed.
- Prefer `&str` over `&String` and `&[T]` over `&Vec<T>` for function parameters.
- Return owned types (`String`, `Vec<T>`) from functions that create new data.
- Use `Cow<'_, str>` when a function sometimes borrows and sometimes owns.

```rust
// Good — borrows what it reads, returns what it creates.
fn greet(name: &str) -> String {
    format!("Hello, {name}!")
}

// Good — takes a slice, not a Vec reference.
fn sum(values: &[i64]) -> i64 {
    values.iter().sum()
}

// Good — Cow for conditional ownership.
fn normalize(input: &str) -> Cow<'_, str> {
    if input.contains(' ') {
        Cow::Owned(input.replace(' ', "_"))
    } else {
        Cow::Borrowed(input)
    }
}
```

---

## 3 — Naming Conventions

| Entity          | Convention    | Examples                                  |
| --------------- | ------------- | ----------------------------------------- |
| Variables       | `snake_case`  | `total_count`, `user_name`, `is_valid`    |
| Functions       | `snake_case`  | `calculate_area`, `fetch_user`            |
| Types (structs) | `PascalCase`  | `HttpClient`, `UserProfile`               |
| Traits          | `PascalCase`  | `Serialize`, `Iterator`, `Display`        |
| Enum variants   | `PascalCase`  | `Direction::North`, `Option::Some`        |
| Constants       | `UPPER_CASE`  | `MAX_RETRIES`, `DEFAULT_TIMEOUT`          |
| Modules         | `snake_case`  | `user_service`, `http_client`             |
| Crates          | `snake_case`  | `my_crate`, `serde_json`                  |
| Type parameters | single letter | `T`, `E`, `K`, `V`                        |
| Lifetimes       | short, `'a`   | `'a`, `'input`, `'ctx`                    |
| Macros          | `snake_case!` | `vec!`, `println!`, `assert_eq!`          |
| Feature flags   | `kebab-case`  | `serde-support`, `async-runtime`          |
| Booleans        | question      | `is_empty`, `has_children`, `should_stop` |

- Conversion methods: `as_` (cheap, borrowed), `to_` (expensive, owned), `into_` (consuming).
- Getter methods: use the field name without `get_` prefix (e.g., `fn name(&self) -> &str`).
- Fallible constructors: `new` for infallible, `try_new` or `from_*` for fallible.
- Iterator-producing methods: `iter()` for `&T`, `iter_mut()` for `&mut T`, `into_iter()` for owned `T`.

---

## 4 — Consistent Style

- **Indentation:** 4 spaces. No tabs.
- **Line length:** 100 characters maximum. Break long expressions at logical boundaries.
- **Braces:** Opening brace on the same line. Always use braces for `if`/`else` bodies.
- **Trailing commas:** Use trailing commas in multiline structs, enums, function calls, and match arms.
- **Imports:** Group `use` statements: `std` → external crates → crate-internal, separated by blank lines. Sort alphabetically within groups.
- **Run `cargo fmt`** before every commit. These rules supplement `rustfmt`, not replace it.

```rust
use std::collections::HashMap;
use std::io::{self, Read};

use serde::{Deserialize, Serialize};
use tokio::fs;

use crate::config::Config;
use crate::error::AppError;
```

---

## 5 — Error Handling

- Use `Result<T, E>` for all fallible operations. Never `panic!` in library code for recoverable errors.
- Define domain-specific error types using an enum. Implement `std::error::Error` and `Display`.
- Use `?` for propagation. Avoid verbose `match` on `Result` when `?` suffices.
- Use `thiserror` for library error types and `anyhow` for application-level error handling (when appropriate).
- Reserve `unwrap()` and `expect()` for cases where failure is logically impossible or constitutes a bug. Always provide a descriptive message with `expect()`.
- Use `Option<T>` for values that may be absent. Avoid sentinel values.

```rust
// Good — domain error type.
#[derive(Debug, thiserror::Error)]
pub enum ConfigError {
    #[error("file not found: {path}")]
    NotFound { path: PathBuf },

    #[error("parse error at line {line}: {message}")]
    ParseError { line: usize, message: String },

    #[error(transparent)]
    Io(#[from] io::Error),
}

// Good — propagation with ?.
fn load_config(path: &Path) -> Result<Config, ConfigError> {
    let content = fs::read_to_string(path).map_err(|_| ConfigError::NotFound {
        path: path.to_owned(),
    })?;

    parse_config(&content)
}
```

---

## 6 — Types and Traits

- Use the newtype pattern to give domain meaning to primitive types: `struct UserId(u64)`.
- A struct models one concept — every field should belong to that concept. If a struct accumulates unrelated state, split it into smaller types or use composition.
- An enum represents one decision or one dimension of variation. Do not merge unrelated alternatives into a single enum.
- Derive standard traits where applicable: `Debug`, `Clone`, `PartialEq`, `Eq`, `Hash`.
- Implement `Display` for types that have a natural human-readable representation.
- Prefer trait bounds over trait objects (`dyn Trait`) for static dispatch when the concrete type is known at compile time.
- Use `impl Trait` in return position for functions that return a single concrete type without exposing it.
- Use `dyn Trait` when you need runtime polymorphism (heterogeneous collections, plugin systems).
- Keep trait definitions focused — prefer many small traits over one large trait.

```rust
// Good — newtype for domain meaning.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct UserId(pub u64);

// Good — impl Trait in return position.
fn create_handler(config: &Config) -> impl Fn(Request) -> Response + '_ {
    move |req| handle_request(config, req)
}
```

---

## 7 — Functions

- Keep functions small — one logical operation per function.
- Prefer `&self` methods over free functions when operating on a type.
- Accept the most general input type: `&str` over `&String`, `impl AsRef<Path>` over `&Path`.
- Return concrete types. Use `impl Trait` when the concrete type is complex but singular.
- Use builder patterns for types with many optional configuration parameters.

```rust
// Good — accepts broad input, returns concrete output.
pub fn word_count(text: &str) -> usize {
    text.split_whitespace().count()
}

// Good — builder pattern for complex construction.
let client = HttpClient::builder()
    .timeout(Duration::from_secs(30))
    .max_retries(3)
    .build()?;
```

---

## 8 — Pattern Matching

- Use `match` for exhaustive pattern matching. The compiler enforces coverage.
- Prefer `if let` for single-variant checks when other variants are irrelevant.
- Use `matches!` macro for boolean pattern checks.
- Destructure in match arms to access inner data directly.
- Avoid wildcard (`_`) catch-all arms when new variants should trigger a compile error.

```rust
// Good — exhaustive matching with destructuring.
match event {
    Event::Click { x, y } => handle_click(x, y),
    Event::KeyPress(key) => handle_key(key),
    Event::Resize { width, height } => handle_resize(width, height),
}

// Good — if let for single variant.
if let Some(user) = find_user(id) {
    greet(&user);
}

// Good — matches! for boolean check.
let is_success = matches!(result, Ok(_));
```

---

## 9 — Iterators and Closures

- Prefer iterator chains over manual loops for transformations and filtering.
- Use `collect()` with a type annotation to materialise iterators.
- Prefer `for` loops when the body has side effects or complex control flow.
- Avoid `collect()`-ing into a `Vec` only to iterate again — chain instead.
- Use `map`, `filter`, `flat_map`, `filter_map`, `fold`, and `any`/`all` idiomatically.

```rust
// Good — iterator chain.
let active_emails: Vec<&str> = users
    .iter()
    .filter(|u| u.is_active)
    .map(|u| u.email.as_str())
    .collect();

// Good — filter_map for combined filter + transform.
let valid_ids: Vec<u64> = inputs
    .iter()
    .filter_map(|s| s.parse::<u64>().ok())
    .collect();
```

---

## 10 — Concurrency

- Use `Arc<T>` for shared ownership across threads. Use `Arc<Mutex<T>>` or `Arc<RwLock<T>>` for shared mutable state.
- Prefer channels (`mpsc`, `crossbeam`) for communication between threads over shared state.
- Use `tokio` or `async-std` for async I/O. Do not block the async runtime with synchronous operations.
- Use `Send` and `Sync` bounds explicitly when designing concurrent APIs.
- Prefer scoped threads (`std::thread::scope`) when threads do not outlive their data.
- Avoid `Mutex` poisoning panics — use `lock().expect("msg")` with a clear message or handle the `PoisonError`.

```rust
// Good — scoped threads for borrowed data.
let mut results = vec![0; data.len()];

std::thread::scope(|s| {
    for (chunk, result) in data.chunks(64).zip(results.chunks_mut(64)) {
        s.spawn(move || {
            for (item, out) in chunk.iter().zip(result.iter_mut()) {
                *out = process(item);
            }
        });
    }
});
```

---

## 11 — Unsafe Code

`unsafe` is a contract between you and the compiler. Use it only when necessary and with extreme care.

- Minimise `unsafe` blocks. Isolate them in small, well-documented functions with a safe public API.
- Document the safety invariants that the caller must uphold with a `// SAFETY:` comment.
- Prefer safe abstractions from `std` or well-audited crates over raw `unsafe`.
- Never use `unsafe` to bypass the borrow checker out of convenience.
- Common valid uses: FFI, performance-critical low-level code, implementing safe abstractions over raw pointers.

```rust
// Good — isolated unsafe with safety documentation.
/// Returns a reference to the element at `index` without bounds checking.
///
/// # Safety
/// The caller must ensure `index < self.len()`.
pub unsafe fn get_unchecked(&self, index: usize) -> &T {
    // SAFETY: caller guarantees index is in bounds.
    unsafe { &*self.ptr.add(index) }
}
```

---

## 12 — Modules and Crates

- One module per file as the default. Use `mod.rs` or `module_name.rs` consistently within a project (prefer `module_name.rs`).
- Keep `pub` surfaces minimal. Only expose what external consumers need.
- Use `pub(crate)` for items shared within the crate but not exported.
- Re-export important types at the crate root for ergonomic access.
- Avoid circular dependencies between crates. Use workspace features to share code.

```rust
// lib.rs — re-export public API.
pub mod config;
pub mod error;

pub use config::Config;
pub use error::AppError;
```

---

## 13 — Lifetimes

- Let the compiler elide lifetimes when possible. Only annotate when the compiler requires it.
- Use descriptive lifetime names (`'input`, `'ctx`) for complex signatures with multiple lifetimes.
- Prefer owned types in struct fields unless the struct is clearly a short-lived view/reference into other data.
- Avoid `'static` bounds unless genuinely needed (e.g., spawning threads, storing in long-lived collections).

```rust
// Good — elision handles this fine. No annotation needed.
fn first_word(s: &str) -> &str {
    s.split_whitespace().next().unwrap_or("")
}

// Good — descriptive lifetime for clarity.
struct Parser<'input> {
    source: &'input str,
    position: usize,
}
```

---

## 14 — Self-Documenting Code

Write code that explains itself. Reserve comments for _why_, not _what_.

- Use doc comments (`///`) on all public items. Include examples in `# Examples` sections.
- Use `//` comments for non-obvious implementation details.
- Module-level documentation (`//!`) should describe the module's purpose and usage.
- Run `cargo doc --no-deps` to verify documentation renders correctly.

````rust
/// Computes the Levenshtein distance between two strings.
///
/// # Examples
///
/// ```
/// assert_eq!(levenshtein("kitten", "sitting"), 3);
/// ```
pub fn levenshtein(a: &str, b: &str) -> usize {
    // ...
}
````

---

## 15 — Whitespace as Structure

Use blank lines to reveal logical structure — like paragraphs in prose.

- **One blank line** between functions, impl blocks, and module-level items.
- **One blank line** between logical blocks within a function.
- **No** multiple consecutive blank lines.
- **No** trailing whitespace. One trailing newline at end of file.
- Let `cargo fmt` handle spacing within expressions and statements.

---

## 16 — Security Essentials

- Validate all external input (files, network, user input, environment variables) before use.
- Use `checked_add`, `checked_mul`, etc. for arithmetic on untrusted values to prevent overflow.
- Use `secrecy` crate or similar for sensitive data that should not appear in logs or debug output.
- Avoid `unsafe` for parsing untrusted input. Use safe parsers (`nom`, `serde`).
- Pin dependency versions in `Cargo.lock`. Audit dependencies with `cargo audit`.
- Use constant-time comparison for secrets (e.g., `subtle::ConstantTimeEq`).
- Limit resource consumption: set timeouts, cap buffer sizes, and bound loop iterations for untrusted input.

---

## 17 — Testing

- Place unit tests in a `#[cfg(test)] mod tests` block at the bottom of the source file.
- Place integration tests in a `tests/` directory at the crate root.
- Use descriptive test names: `fn parses_valid_input()`, `fn rejects_empty_string()`.
- Use `#[should_panic(expected = "...")]` for expected panics.
- Use `proptest` or `quickcheck` for property-based testing where applicable.
- Run `cargo clippy` and `cargo test` before every commit.

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sum_of_empty_slice_is_zero() {
        assert_eq!(sum(&[]), 0);
    }

    #[test]
    fn sum_of_positive_values() {
        assert_eq!(sum(&[1, 2, 3, 4, 5]), 15);
    }
}
```

---

## 18 — Performance

- Profile before optimising. Use `cargo bench` with `criterion` for benchmarks.
- Avoid premature allocation: use `&str` and `&[T]` where ownership is not needed.
- Use `Vec::with_capacity` when the size is known or can be estimated.
- Prefer stack allocation (`[T; N]`, `arrayvec`) for small, fixed-size data.
- Use `#[inline]` sparingly — only on small, hot functions in library code after measuring.
- Avoid unnecessary `clone()` and `to_string()` — borrow instead.

---

## 19 — Anti-Patterns

- **`unwrap()` on fallible operations** without justification. Use `?` or `expect("reason")`.
- **`clone()` to satisfy the borrow checker** without understanding why. Restructure ownership instead.
- **Stringly-typed data.** Use enums, newtypes, or structs to encode meaning.
- **Giant `match` arms with duplicated logic.** Extract common behaviour into functions.
- **`Box<dyn Any>` for heterogeneous data.** Use proper enums or trait objects.
- **Ignoring Clippy lints.** Fix the lint or add `#[allow(...)]` with a comment explaining why.
- **Excessive `pub`.** Default to private. Expose only what consumers need.
- **`String` in struct fields when `&str` would suffice** for short-lived views.

---

## 20 — Checklist

- [ ] Code compiles with no warnings (`cargo build`).
- [ ] `cargo clippy` passes with no lints.
- [ ] `cargo fmt --check` reports no formatting issues.
- [ ] All `Result` and `Option` values are handled — no unguarded `unwrap()`.
- [ ] `unsafe` blocks are isolated, minimal, and documented with `// SAFETY:` comments.
- [ ] Public items have doc comments (`///`) with examples where useful.
- [ ] Names follow Rust conventions (§3).
- [ ] Ownership is clear — no unnecessary `clone()`.
- [ ] Tests cover the primary success and failure paths.
- [ ] Files end with a single trailing newline.
