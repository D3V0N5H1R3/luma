---
description: "Use when writing, reviewing, or modifying JavaScript source code (.js, .mjs, .cjs files). Covers naming, style, error handling, async patterns, modules, and modern JavaScript idioms."
applyTo: "**/*.{js,mjs,cjs}"
---

# Working with JavaScript

These instructions govern how you write JavaScript source code. Every function, module, and file you produce must follow these principles. They are aligned with the [MDN Web Docs](https://developer.mozilla.org/en-US/docs/Web/JavaScript), the [ECMAScript specification](https://tc39.es/ecma262/), and common community conventions.

---

## Table of Contents

1. [Simplicity First](#1--simplicity-first)
2. [Naming Conventions](#2--naming-conventions)
3. [Consistent Style](#3--consistent-style)
4. [Variables and Scope](#4--variables-and-scope)
5. [Functions](#5--functions)
6. [Error Handling](#6--error-handling)
7. [Async and Promises](#7--async-and-promises)
8. [Modules and Imports](#8--modules-and-imports)
9. [Objects and Arrays](#9--objects-and-arrays)
10. [Classes](#10--classes)
11. [Null and Undefined](#11--null-and-undefined)
12. [Self-Documenting Code](#12--self-documenting-code)
13. [Whitespace as Structure](#13--whitespace-as-structure)
14. [Security Essentials](#14--security-essentials)
15. [Testing](#15--testing)
16. [Performance](#16--performance)
17. [File Organisation](#17--file-organisation)
18. [Anti-Patterns](#18--anti-patterns)
19. [Checklist](#19--checklist)

---

## 1 — Simplicity First

Write the simplest code that solves the problem correctly.

- Prefer straightforward control flow over clever abstractions.
- Avoid over-engineering with excessive metaprogramming, proxies, or decorators.
- If a built-in method or standard API does what you need, use it.
- A developer unfamiliar with the codebase should understand your code within minutes.

**Test:** Before committing to an approach, ask — _is there a simpler way?_

---

## 2 — Naming Conventions

| Entity            | Convention    | Examples                                |
| ----------------- | ------------- | --------------------------------------- |
| Variables         | `camelCase`   | `totalCount`, `userName`, `isValid`     |
| Functions         | `camelCase`   | `calculateArea`, `fetchUser`            |
| Classes           | `PascalCase`  | `HttpClient`, `UserProfile`             |
| Constants         | `UPPER_CASE`  | `MAX_RETRIES`, `DEFAULT_TIMEOUT`        |
| File names        | `kebab-case`  | `user-service.js`, `http-client.js`     |
| Private fields    | `camelCase`   | `#connection`, `#count`                 |
| Boolean variables | question      | `isReady`, `hasChildren`, `shouldRetry` |
| Event handlers    | `on`/`handle` | `onClick`, `handleSubmit`               |

- Name what the value represents, not its type. `connectionTimeout` — good. `num` — bad.
- Avoid abbreviations unless universally understood (`id`, `url`, `http`).
- Prefix boolean variables with `is`, `has`, `should`, `can`, or `will`.
- Private class fields use a `camelCase` name marked with the `#` prefix (true private fields).

---

## 3 — Consistent Style

- **Indentation:** 4 spaces. No tabs.
- **Semicolons:** Always use semicolons.
- **Quotes:** Double quotes for strings. Template literals for interpolation.
- **Braces:** Always use braces for `if`, `else`, `for`, `while` — even single-statement bodies.
- **Line length:** 100 characters maximum.
- **Trailing commas:** Use trailing commas in multiline arrays, objects, and parameter lists.
- **Equality:** Always use `===` and `!==`. Never use `==` or `!=`.

```javascript
// Good — consistent style.
const users = [
    { name: "Alice", age: 30 },
    { name: "Bob", age: 25 },
];

if (users.length > 0) {
    processUsers(users);
}
```

---

## 4 — Variables and Scope

- Use `const` for all variables unless reassignment is required. Then use `let`.
- **Never** use `var`. It has confusing hoisting and function-scoping behaviour.
- Declare variables close to their first use.
- Minimise variable scope. Prefer tighter blocks.
- One declaration per statement — no comma-separated declarations.

```javascript
// Good — const by default, let only when needed.
const maxRetries = 5;
const timeout = 3000;

let attempts = 0;

while (attempts < maxRetries) {
    const result = tryConnect();

    if (result.ok) {
        return result.connection;
    }

    attempts++;
}
```

---

## 5 — Functions

- Keep functions small — one logical operation per function.
- Use arrow functions for callbacks and short inline expressions.
- Use function declarations for top-level named functions (they are hoisted and named in stack traces).
- Prefer default parameters over manual checks for undefined.
- Use object destructuring for functions with more than three parameters.
- Avoid arguments object — use rest parameters (`...args`) when needed.

```javascript
// Good — function declaration for top-level.
function calculateDiscount(price, rate) {
    return price * rate;
}

// Good — object parameter for many arguments.
function fetchData({ url, method = "GET", headers = {}, timeout = 5000 }) {
    return fetch(url, {
        method,
        headers,
        signal: AbortSignal.timeout(timeout),
    });
}

// Good — arrow function for callback.
const doubled = numbers.map((n) => n * 2);
```

---

## 6 — Error Handling

- Use `try`/`catch` at system boundaries (API handlers, event listeners, entry points).
- Throw `Error` instances (or subclasses), never throw strings or plain objects.
- Never swallow errors silently. Log or rethrow.
- Use custom error classes for distinct error categories.
- Provide descriptive error messages that include context.

```javascript
// Good — custom error class.
class NetworkError extends Error {
    constructor(message, statusCode) {
        super(message);
        this.name = "NetworkError";
        this.statusCode = statusCode;
    }
}

// Good — error handling at boundary.
async function handleRequest(req, res) {
    try {
        const data = await processRequest(req);
        res.json(data);
    } catch (error) {
        if (error instanceof NetworkError) {
            res.status(error.statusCode).json({ error: error.message });
        } else {
            console.error("Unexpected error:", error);
            res.status(500).json({ error: "Internal server error" });
        }
    }
}
```

---

## 7 — Async and Promises

- Always use `async`/`await` over raw `.then()` chains.
- Handle errors with `try`/`catch` at appropriate boundaries.
- Use `Promise.all` for independent concurrent operations. Use `Promise.allSettled` when partial failure is acceptable.
- Avoid floating promises — always `await` or explicitly handle the promise.
- Use `AbortController` for cancellable operations.
- Never mix callbacks and promises in the same API.

```javascript
// Good — async/await with proper error handling.
async function fetchUsers(ids) {
    const promises = ids.map((id) => fetchUser(id));

    return Promise.all(promises);
}

// Good — cancellable operation.
async function fetchWithTimeout(url, timeoutMs) {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), timeoutMs);

    try {
        return await fetch(url, { signal: controller.signal });
    } finally {
        clearTimeout(timeoutId);
    }
}
```

---

## 8 — Modules and Imports

- Use ES modules (`import`/`export`) in `.mjs` files and modern projects.
- Use CommonJS (`require`/`module.exports`) only in `.cjs` files or legacy Node.js code.
- Use named exports by default. Use default exports only for single-purpose modules.
- Group imports: Node built-ins → third-party → project modules, separated by blank lines.
- Avoid circular imports. If module A imports from B and B from A, extract shared code into a third module.
- Re-export from barrel files (`index.js`) only for public API surfaces.

```javascript
import { readFile } from "node:fs/promises";
import path from "node:path";

import express from "express";

import { UserService } from "../services/user-service.js";
import { validateEmail } from "../utils/validation.js";
```

---

## 9 — Objects and Arrays

- Use object shorthand for properties and methods.
- Use computed property names when dynamic keys are needed.
- Prefer spreading (`{ ...obj, key: newValue }`) over `Object.assign` for shallow copies.
- Use destructuring for extracting values from objects and arrays.
- Prefer array methods (`map`, `filter`, `reduce`, `find`) over manual loops for transformations.
- Avoid mutating function parameters — return new objects instead.

```javascript
// Good — shorthand, destructuring, spread.
const { name, age } = user;

const updatedUser = { ...user, name: "New Name" };

const activeUsers = users.filter((u) => u.isActive);

// Good — computed property.
const key = "dynamicKey";
const obj = { [key]: "value" };
```

---

## 10 — Classes

- Use classes for stateful objects with multiple methods that share internal state.
- Use `#` private fields for true encapsulation.
- Prefer composition over inheritance. Use inheritance only for genuine "is-a" relationships.
- Keep classes focused — one responsibility per class.
- Use `static` methods for utility functions that do not need instance state.

```javascript
// Good — focused class with private fields.
class ConnectionPool {
    #connections = [];
    #maxSize;

    constructor(maxSize) {
        this.#maxSize = maxSize;
    }

    acquire() {
        if (this.#connections.length > 0) {
            return this.#connections.pop();
        }

        if (this.size < this.#maxSize) {
            return this.#createConnection();
        }

        throw new Error("Pool exhausted");
    }

    release(connection) {
        this.#connections.push(connection);
    }

    get size() {
        return this.#connections.length;
    }

    #createConnection() {
        return new Connection();
    }
}
```

---

## 11 — Null and Undefined

- Prefer `undefined` over `null` for absent values, unless interfacing with APIs that use `null`.
- Use optional chaining (`?.`) and nullish coalescing (`??`) instead of manual null checks.
- Avoid non-null assumptions — always guard against `null` and `undefined` at boundaries.
- Use explicit checks (`=== null` or `=== undefined`) rather than falsy checks when `0`, `""`, or `false` are valid values.

```javascript
// Good — safe access with optional chaining and default.
const displayName = user?.profile?.name ?? "Anonymous";

// Good — explicit null check when 0 is valid.
const count = response.count ?? 0;

// Bad — falsy check would incorrectly reject 0.
const count = response.count || 0;
```

---

## 12 — Self-Documenting Code

Write code that explains itself. Reserve comments for _why_, not _what_.

- Use descriptive names so the code reads naturally.
- Use JSDoc comments (`/** */`) on exported functions and public APIs.
- Delete stale or redundant comments. A wrong comment is worse than no comment.
- Use `@param`, `@returns`, and `@throws` tags for complex public functions.

```javascript
// Bad — restates the code.
// Increment the counter.
counter++;

// Good — explains a non-obvious constraint.
// Clamped to 100 because the API rejects page sizes above this.
const pageSize = Math.min(requestedSize, 100);

/**
 * Parse a duration string into milliseconds.
 * @param {string} input - Duration string (e.g., "5s", "100ms", "2m").
 * @returns {number} Duration in milliseconds.
 * @throws {Error} If the format is not recognised.
 */
function parseDuration(input) {
    // ...
}
```

---

## 13 — Whitespace as Structure

Use blank lines to reveal logical structure — like paragraphs in prose.

- **One blank line** between top-level declarations (functions, classes).
- **One blank line** between logical blocks within a function.
- **No** multiple consecutive blank lines.
- **No** trailing whitespace. One trailing newline at end of file.

---

## 14 — Security Essentials

- Validate and sanitise all user input at system boundaries.
- Use parameterised queries for database access — never interpolate user input into SQL strings.
- Escape output appropriately for the context (HTML, URL, shell).
- Do not log sensitive data (passwords, tokens, PII).
- Use `crypto.randomUUID()` or `crypto.getRandomValues()` for security-sensitive randomness — never `Math.random()`.
- Set appropriate CORS, CSP, and security headers.
- Avoid `eval()`, `new Function()`, and `innerHTML` with untrusted data.
- Use `Content-Security-Policy` headers to prevent XSS.
- Validate and sanitise file paths to prevent path traversal.

---

## 15 — Testing

- Use descriptive test names that state the expected behaviour.
- Follow Arrange-Act-Assert (AAA) structure.
- Test behaviour, not implementation. Mock only at system boundaries.
- Prefer `describe`/`it` blocks for grouping related tests.
- Keep tests independent — no shared mutable state between tests.

```javascript
describe("calculateDiscount", () => {
    it("returns zero for zero price", () => {
        const result = calculateDiscount(0, 0.1);

        expect(result).toBe(0);
    });

    it("applies the rate correctly", () => {
        const result = calculateDiscount(100, 0.2);

        expect(result).toBe(20);
    });
});
```

---

## 16 — Performance

- Profile before optimising. Use the browser DevTools Performance panel or Node's `--prof` / `--cpu-prof`.
- Write clear code first, then measure — avoid speculative micro-optimisation.
- Hoist invariant lookups (`array.length`, repeated property access) into local variables inside hot loops.
- Prefer a plain `for`/`for...of` loop over chained `map`/`filter`/`reduce` when iterating large arrays on hot paths.
- Batch DOM reads and writes to avoid layout thrashing; drive visual updates with `requestAnimationFrame`.
- Debounce or throttle high-frequency events such as `scroll`, `resize`, and `input`.
- Avoid allocating functions and objects inside hot loops; reuse them where possible.
- Remember that `JSON.parse`/`JSON.stringify` are synchronous and block the event loop on large payloads.

---

## 17 — File Organisation

- One primary export per file as a guideline. Related helpers may share a file.
- Group files by feature or domain, not by type.
- Keep `index.js` files thin — re-exports only, no logic.
- Use `.mjs` extension for ES modules, `.cjs` for CommonJS, or configure `"type": "module"` in `package.json`.

---

## 18 — Anti-Patterns

- **`var` declarations.** Use `const` or `let`.
- **`==` and `!=` operators.** Use `===` and `!==`.
- **Nested ternaries.** Use `if`/`else` or early returns for clarity.
- **Callback hell.** Use `async`/`await` or promises.
- **Mutation of function parameters.** Return new objects instead.
- **Ignoring promise rejections.** Always handle or propagate errors.
- **`eval()` and `new Function()`.** These are code injection vectors.
- **Implicit type coercion reliance.** Be explicit about type conversions.
- **Global variables.** Always use `const`/`let` with proper scoping.
- **`arguments` object.** Use rest parameters (`...args`) instead.

---

## 19 — Checklist

- [ ] `const` is used for all variables unless reassignment is needed. No `var`.
- [ ] Strict equality (`===`/`!==`) is used everywhere.
- [ ] Errors are handled at appropriate boundaries — no swallowed errors.
- [ ] Async operations use `async`/`await` — no raw `.then()` chains.
- [ ] Names follow the conventions in §2.
- [ ] No `eval()`, `new Function()`, or `innerHTML` with untrusted data.
- [ ] Tests cover the primary success and failure paths.
- [ ] Imports are ordered: built-ins → third-party → project.
- [ ] JSDoc comments document exported functions and public APIs.
- [ ] Files end with a single trailing newline.
