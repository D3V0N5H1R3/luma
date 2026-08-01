---
description: "Use when writing, reviewing, or modifying TypeScript source code (.ts, .tsx files). Covers naming, style, type safety, error handling, async patterns, and modern TypeScript idioms."
applyTo: "**/*.{ts,tsx}"
priority: reference
---

# Working with TypeScript

These instructions govern how you write TypeScript source code. Every function, type, and file you produce must follow these principles. They are aligned with the official [TypeScript documentation](https://www.typescriptlang.org/docs/) and common community conventions.

---

## Table of Contents

1. [Simplicity First](#1--simplicity-first)
2. [Type Safety](#2--type-safety)
3. [Naming Conventions](#3--naming-conventions)
4. [Consistent Style](#4--consistent-style)
5. [Functions](#5--functions)
6. [Error Handling](#6--error-handling)
7. [Immutability](#7--immutability)
8. [Async and Promises](#8--async-and-promises)
9. [Modules and Imports](#9--modules-and-imports)
10. [Classes and Interfaces](#10--classes-and-interfaces)
11. [Generics](#11--generics)
12. [Enums and Unions](#12--enums-and-unions)
13. [Null Safety](#13--null-safety)
14. [Self-Documenting Code](#14--self-documenting-code)
15. [Whitespace as Structure](#15--whitespace-as-structure)
16. [Security Essentials](#16--security-essentials)
17. [Testing](#17--testing)
18. [Performance](#18--performance)
19. [File Organisation](#19--file-organisation)
20. [Anti-Patterns](#20--anti-patterns)
21. [Checklist](#21--checklist)

---

## 1 — Simplicity First

Write the simplest code that solves the problem correctly.

- Prefer straightforward control flow over clever abstractions.
- Avoid over-engineering with excessive generics, decorators, or metaprogramming.
- If a built-in method or standard API does what you need, use it.
- A developer unfamiliar with the codebase should understand your code within minutes.

**Test:** Before committing to an approach, ask — _is there a simpler way?_

---

## 2 — Type Safety

Leverage TypeScript's type system to catch errors at compile time. Treat the type system as your first line of defence.

- Enable `strict: true` in `tsconfig.json`. Never disable strict checks in production code.
- Avoid `any`. Use `unknown` when the type is genuinely unknown, then narrow with type guards.
- Prefer literal types and discriminated unions over broad primitive types.
- Use `satisfies` to validate types without widening.
- Use `as const` for immutable literal values.

```typescript
// Bad — loses type information.
const config: any = loadConfig();

// Good — narrowing with unknown.
function processValue(value: unknown): string {
    if (typeof value === "string") {
        return value.toUpperCase();
    }

    throw new Error(`Expected string, got ${typeof value}`);
}

// Good — discriminated union.
type Result<T> = { ok: true; value: T } | { ok: false; error: string };
```

---

## 3 — Naming Conventions

| Entity            | Convention   | Examples                                 |
| ----------------- | ------------ | ---------------------------------------- |
| Variables         | `camelCase`  | `totalCount`, `userName`, `isValid`      |
| Functions         | `camelCase`  | `calculateArea`, `fetchUser`             |
| Classes           | `PascalCase` | `HttpClient`, `UserProfile`              |
| Interfaces        | `PascalCase` | `Serializable`, `EventHandler`           |
| Type aliases      | `PascalCase` | `UserId`, `ConnectionOptions`            |
| Enums             | `PascalCase` | `Direction`, `HttpStatus`                |
| Enum members      | `PascalCase` | `Direction.North`, `HttpStatus.NotFound` |
| Constants         | `UPPER_CASE` | `MAX_RETRIES`, `DEFAULT_TIMEOUT`         |
| File names        | `kebab-case` | `user-service.ts`, `http-client.ts`      |
| Type parameters   | `PascalCase` | `T`, `TKey`, `TValue`, `TResult`         |
| Private fields    | `camelCase`  | `#connection`, `private count`           |
| Boolean variables | question     | `isReady`, `hasChildren`, `shouldRetry`  |
| React components  | `PascalCase` | `UserCard`, `NavigationBar`              |

- Do not prefix interfaces with `I`. Use `UserService`, not `IUserService`.
- Name what the value represents, not its type. `connectionTimeout` — good. `num` — bad.
- Avoid abbreviations unless universally understood (`id`, `url`, `http`).
- Private class fields use a `camelCase` name; mark privacy with the `#` prefix (true private fields) or the `private` keyword.

> **House deviation — Luma editor extensions.** The VS Code extension under `extensions/vscode/`
> deliberately uses `snake_case` for local variables, function parameters, and class fields
> (e.g. `lsp_path`, `file_path`, `luma_config`) to stay consistent with the Rust
> (Zed) and C++ sources it sits beside, and with the shared `defaults.json` keys the code
> generators emit. Types, classes, interfaces, and enums still use `PascalCase`, and exported
> constants still use `UPPER_CASE`. New code in `extensions/vscode/` should follow this
> established `snake_case` convention rather than the `camelCase` default above; everywhere
> else in the repository, use `camelCase` as specified.

---

## 4 — Consistent Style

- **Indentation:** 4 spaces. No tabs.
- **Semicolons:** Always use semicolons.
- **Quotes:** Double quotes for strings. Template literals for interpolation.
- **Braces:** Always use braces for `if`, `else`, `for`, `while` — even single-statement bodies.
- **Line length:** 100 characters maximum.
- **Trailing commas:** Use trailing commas in multiline arrays, objects, parameters, and type parameters.
- **`const` placement:** Prefer `const` over `let`. Never use `var`.

```typescript
// Good — consistent style.
const users: User[] = [
    { name: "Alice", age: 30 },
    { name: "Bob", age: 25 },
];

if (users.length > 0) {
    processUsers(users);
}
```

---

## 5 — Functions

- Keep functions small — one logical operation per function.
- Always declare return types explicitly on exported and public functions.
- Use arrow functions for callbacks and short inline expressions.
- Use function declarations for top-level named functions.
- Prefer default parameters over optional parameters when a sensible default exists.
- Use object destructuring for functions with more than three parameters.

```typescript
// Good — explicit return type, single responsibility.
function calculateDiscount(price: number, rate: number): number {
    return price * rate;
}

// Good — object parameter for many arguments.
interface FetchOptions {
    url: string;
    method?: string;
    headers?: Record<string, string>;
    timeout?: number;
}

async function fetchData({
    url,
    method = "GET",
    headers = {},
    timeout = 5000,
}: FetchOptions): Promise<Response> {
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

- Use typed errors and discriminated unions for expected failure paths.
- Throw exceptions only for truly exceptional, unrecoverable situations.
- Always catch errors at system boundaries (API handlers, event listeners, top-level entry points).
- Never swallow errors silently. Log or rethrow.
- Use `Error` subclasses with descriptive messages and appropriate context.

```typescript
// Good — typed result for expected failures.
type ParseResult<T> =
    | { success: true; data: T }
    | { success: false; error: string };

function parseJson<T>(input: string): ParseResult<T> {
    try {
        const data = JSON.parse(input) as T;

        return { success: true, data };
    } catch (e) {
        return { success: false, error: `Invalid JSON: ${String(e)}` };
    }
}

// Good — custom error class.
class NetworkError extends Error {
    constructor(
        message: string,
        public readonly statusCode: number,
    ) {
        super(message);
        this.name = "NetworkError";
    }
}
```

---

## 7 — Immutability

Prefer immutable data. Mutations are a common source of bugs, especially in asynchronous code.

- Use `const` for all variable declarations unless reassignment is required.
- Use `readonly` on properties that should not change after construction.
- Use `Readonly<T>`, `ReadonlyArray<T>`, and `ReadonlyMap<K, V>` for immutable data structures.
- Prefer spreading (`{ ...obj, key: newValue }`) over mutation for object updates.
- Use `as const` for literal values that should not be widened.

```typescript
// Good — immutable by default.
const config = {
    maxRetries: 3,
    timeout: 5000,
} as const;

interface User {
    readonly id: string;
    readonly name: string;
    readonly email: string;
}

// Good — new object instead of mutation.
function updateName(user: User, name: string): User {
    return { ...user, name };
}
```

---

## 8 — Async and Promises

- Always use `async`/`await` over raw `.then()` chains.
- Handle errors with `try`/`catch` at appropriate boundaries.
- Use `Promise.all` for independent concurrent operations. Use `Promise.allSettled` when partial failure is acceptable.
- Avoid floating promises — always `await` or explicitly handle the promise.
- Use `AbortController` for cancellable operations.
- Never mix callbacks and promises in the same API.

```typescript
// Good — async/await with proper error handling.
async function fetchUsers(ids: string[]): Promise<User[]> {
    const promises = ids.map((id) => fetchUser(id));

    return Promise.all(promises);
}

// Good — cancellable operation.
async function fetchWithTimeout(
    url: string,
    timeoutMs: number,
): Promise<Response> {
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

## 9 — Modules and Imports

- Use ES modules (`import`/`export`). Never use `require()` in TypeScript files.
- Use named exports by default. Use default exports only for React components or single-purpose modules.
- Group imports: Node built-ins → third-party → project modules, separated by blank lines.
- Use path aliases (configured in `tsconfig.json`) to avoid deep relative paths.
- Re-export from barrel files (`index.ts`) only for public API surfaces. Avoid barrel files for internal modules.
- Avoid circular imports. If module A imports from B and B from A, extract shared types into a third module.

```typescript
import { readFile } from "node:fs/promises";
import path from "node:path";

import express from "express";

import { UserService } from "@/services/user-service";
import { validateEmail } from "@/utils/validation";
```

---

## 10 — Classes and Interfaces

- Prefer interfaces over type aliases for object shapes that may be extended. An interface models one concept — if it accumulates unrelated members, split it into smaller interfaces.
- Prefer composition over inheritance. Use inheritance only for genuine "is-a" relationships.
- Use `private` fields (`#field`) for true encapsulation. Use TypeScript's `private` keyword for compile-time-only protection.
- Keep classes focused — one responsibility per class.
- Use dependency injection for testability.

```typescript
// Good — interface for contract, class for implementation.
interface Logger {
    info(message: string): void;
    error(message: string, cause?: Error): void;
}

class ConsoleLogger implements Logger {
    info(message: string): void {
        console.log(`[INFO] ${message}`);
    }

    error(message: string, cause?: Error): void {
        console.error(`[ERROR] ${message}`, cause);
    }
}
```

---

## 11 — Generics

- Use generics to write reusable, type-safe code — not to impress.
- Constrain type parameters with `extends` when the function requires certain capabilities.
- Use meaningful names: `T` only when trivially obvious. Otherwise, use `TKey`, `TValue`, `TItem`, etc.
- Avoid deeply nested generic types that harm readability. Extract type aliases for complex generics.

```typescript
// Good — constrained generic.
function getProperty<T, K extends keyof T>(obj: T, key: K): T[K] {
    return obj[key];
}

// Good — extracted type alias for complex generic.
type AsyncHandler<TReq, TRes> = (req: TReq) => Promise<TRes>;
```

---

## 12 — Enums and Unions

- Prefer string literal unions over enums for simple sets of values.
- Use `const enum` only when bundle size matters and you understand the erasure implications.
- Use discriminated unions for state machines and variant types. A discriminated union represents one decision or one dimension of variation — do not merge unrelated alternatives into a single union.

```typescript
// Good — string literal union for simple cases.
type Direction = "north" | "south" | "east" | "west";

// Good — discriminated union for complex state.
type LoadingState<T> =
    | { status: "idle" }
    | { status: "loading" }
    | { status: "success"; data: T }
    | { status: "error"; error: string };
```

---

## 13 — Null Safety

- Enable `strictNullChecks` (implied by `strict: true`).
- Prefer `undefined` over `null` for absent values, unless interfacing with APIs that use `null`.
- Use optional chaining (`?.`) and nullish coalescing (`??`) instead of manual null checks.
- Avoid non-null assertions (`!`) except when you can prove the value exists and a type guard is impractical.

```typescript
// Good — safe access with optional chaining and default.
const displayName = user?.profile?.name ?? "Anonymous";

// Good — type guard for narrowing.
function isNonNull<T>(value: T | null | undefined): value is T {
    return value != null;
}
```

---

## 14 — Self-Documenting Code

Write code that explains itself. Reserve comments for _why_, not _what_.

- Use descriptive names so the code reads naturally.
- Use JSDoc comments (`/** */`) on exported functions, interfaces, and types to document the public API.
- Delete stale or redundant comments. A wrong comment is worse than no comment.
- Use `@param`, `@returns`, and `@throws` tags for complex public functions.

```typescript
// Bad — restates the code.
// Increment the counter.
counter++;

// Good — explains a non-obvious constraint.
// Clamped to 100 because the API rejects page sizes above this.
const pageSize = Math.min(requestedSize, 100);
```

---

## 15 — Whitespace as Structure

Use blank lines to reveal logical structure — like paragraphs in prose.

- **One blank line** between top-level declarations (functions, classes, interfaces).
- **One blank line** between logical blocks within a function.
- **No** multiple consecutive blank lines.
- **No** trailing whitespace. One trailing newline at end of file.

---

## 16 — Security Essentials

- Validate and sanitise all user input at system boundaries.
- Use parameterised queries for database access — never interpolate user input into SQL strings.
- Escape output appropriately for the context (HTML, URL, shell).
- Do not log sensitive data (passwords, tokens, PII).
- Use `crypto.randomUUID()` or `crypto.getRandomValues()` for security-sensitive randomness — never `Math.random()`.
- Set appropriate CORS, CSP, and security headers.
- Validate JWTs with proper signature verification and expiry checks.
- Avoid `eval()`, `new Function()`, and `innerHTML` with untrusted data.

---

## 17 — Testing

- Use descriptive test names that state the expected behaviour.
- Follow Arrange-Act-Assert (AAA) structure.
- Test behaviour, not implementation. Mock only at system boundaries.
- Prefer `describe`/`it` blocks for grouping related tests.
- Keep tests independent — no shared mutable state between tests.

```typescript
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

## 18 — Performance

- Profile before optimising. TypeScript compiles to JavaScript, so use the same runtime tools (DevTools, Node `--prof` / `--cpu-prof`).
- Types are erased at runtime and carry no cost — but non-`const` `enum`s and decorators emit real code.
- Prefer `const enum` or unions of string literals over runtime `enum`s when you only need compile-time constants.
- Keep type-level computation modest — deeply recursive conditional or mapped types slow `tsc` and degrade editor responsiveness.
- Enable `incremental` builds and project references to speed up rebuilds on large codebases.
- Apply the JavaScript runtime guidance too: hoist invariant lookups, avoid allocations in hot loops, batch DOM work, and debounce high-frequency events.
- Favour `readonly` views over defensive deep clones of large objects.

---

## 19 — File Organisation

- One primary export per file as a guideline. Related helpers may share a file.
- Group files by feature or domain, not by type (prefer `users/user-service.ts` over `services/user-service.ts`).
- Keep `index.ts` files thin — re-exports only, no logic.
- Separate types into `*.types.ts` files only when they are shared across multiple modules.

---

## 20 — Anti-Patterns

- **`any` as an escape hatch.** Use `unknown` and narrow properly.
- **Non-null assertions (`!`) everywhere.** Add proper null checks or refactor the types.
- **Nested ternaries.** Use `if`/`else` or early returns for clarity.
- **God classes.** Split large classes into focused, single-responsibility units.
- **Barrel files for internal code.** They cause circular imports and slow bundlers.
- **Mutation of function parameters.** Return new objects instead.
- **Ignoring promise rejections.** Always handle or propagate errors.
- **`as` type assertions for convenience.** Use type guards or refactor types to avoid assertions.

---

## 21 — Checklist

- [ ] `strict: true` is enabled and no `any` types are used.
- [ ] All exported functions have explicit return types.
- [ ] `const` is used for all variables unless reassignment is needed.
- [ ] Errors are handled at appropriate boundaries — no swallowed errors.
- [ ] Async operations use `async`/`await` — no raw `.then()` chains.
- [ ] Names follow the conventions in §3.
- [ ] No non-null assertions (`!`) without clear justification.
- [ ] Tests cover the primary success and failure paths.
- [ ] Imports are ordered: built-ins → third-party → project.
- [ ] Files end with a single trailing newline.
