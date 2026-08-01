---
description: "Use when writing, reviewing, or modifying Python source code (.py files). Covers naming, style, type hints, error handling, modules, and idiomatic Python patterns."
applyTo: "**/*.py"
priority: reference
---

# Working with Python

These instructions govern how you write Python source code. Every function, class, and module you produce must follow these principles. They are aligned with [PEP 8](https://peps.python.org/pep-0008/), [PEP 20 (The Zen of Python)](https://peps.python.org/pep-0020/), and the official [Python documentation](https://docs.python.org/3/).

---

## Table of Contents

1. [Simplicity First](#1--simplicity-first)
2. [Naming Conventions](#2--naming-conventions)
3. [Consistent Style](#3--consistent-style)
4. [Variables and Scope](#4--variables-and-scope)
5. [Functions](#5--functions)
6. [Type Hints](#6--type-hints)
7. [Error Handling](#7--error-handling)
8. [Classes](#8--classes)
9. [Modules and Imports](#9--modules-and-imports)
10. [Collections and Comprehensions](#10--collections-and-comprehensions)
11. [Iterators and Generators](#11--iterators-and-generators)
12. [Context Managers](#12--context-managers)
13. [Strings](#13--strings)
14. [Async and Concurrency](#14--async-and-concurrency)
15. [Self-Documenting Code](#15--self-documenting-code)
16. [Whitespace as Structure](#16--whitespace-as-structure)
17. [Security Essentials](#17--security-essentials)
18. [Testing](#18--testing)
19. [Performance](#19--performance)
20. [Anti-Patterns](#20--anti-patterns)
21. [Checklist](#21--checklist)

---

## 1 — Simplicity First

Write the simplest code that solves the problem correctly.

- Prefer straightforward control flow over clever tricks or metaprogramming.
- If the standard library provides a solution, use it. Do not reinvent `collections`, `itertools`, or `pathlib`.
- A developer unfamiliar with the codebase should understand your code within minutes.
- "Simple is better than complex. Complex is better than complicated." — PEP 20.

**Test:** Before committing to an approach, ask — _is there a simpler way?_

---

## 2 — Naming Conventions

| Entity          | Convention     | Examples                                          |
| --------------- | -------------- | ------------------------------------------------- |
| Variables       | `snake_case`   | `total_count`, `user_name`, `is_valid`            |
| Functions       | `snake_case`   | `calculate_area`, `fetch_user`                    |
| Classes         | `PascalCase`   | `HttpClient`, `UserProfile`                       |
| Constants       | `UPPER_CASE`   | `MAX_RETRIES`, `DEFAULT_TIMEOUT`                  |
| Modules         | `snake_case`   | `user_service`, `http_client`                     |
| Packages        | `lowercase`    | `utils`, `networking`                             |
| Private members | `_` prefix     | `_connection`, `_internal_state`                  |
| Name-mangled    | `__` prefix    | `__secret` (use sparingly)                        |
| Boolean vars    | question       | `is_ready`, `has_children`, `should_retry`        |
| Type variables  | `PascalCase`   | `T`, `KeyType`, `ResponseT`                       |

- Name what the value represents, not its type. `connection_timeout` — good. `t` — bad.
- Avoid abbreviations unless universally understood (`id`, `url`, `http`).
- Prefix boolean variables with `is`, `has`, `should`, `can`, or `will`.
- Never shadow built-in names (`list`, `dict`, `type`, `id`, `input`, `open`).

---

## 3 — Consistent Style

- **Indentation:** 4 spaces. No tabs.
- **Line length:** 100 characters maximum. Break long expressions at logical boundaries.
- **Quotes:** Double quotes for strings by default. Single quotes are acceptable when a string contains double quotes.
- **Trailing commas:** Use trailing commas in multiline tuples, lists, dicts, function signatures, and arguments.
- **Parentheses:** Use implicit line continuation with parentheses rather than backslash continuation.
- **Blank lines:** Two blank lines before and after top-level definitions (functions, classes). One blank line between methods in a class.

```python
# Good — consistent style.
def process_users(
    users: list[User],
    max_retries: int = 3,
    timeout: float = 30.0,
) -> list[Result]:
    results = []

    for user in users:
        if not user.is_active:
            continue

        result = fetch_profile(user, timeout=timeout)
        results.append(result)

    return results
```

---

## 4 — Variables and Scope

- Declare variables close to their first use.
- Minimise variable scope. Prefer tighter blocks.
- Use `_` for intentionally unused variables (e.g., `for _ in range(10)`).
- Avoid global mutable state. Use function parameters and return values for data flow.
- Constants belong at module level and use `UPPER_CASE`.

```python
# Good — minimal scope, clear intent.
MAX_BATCH_SIZE = 100

def process_batch(items: list[str]) -> list[str]:
    results = []

    for item in items[:MAX_BATCH_SIZE]:
        cleaned = item.strip().lower()

        if cleaned:
            results.append(cleaned)

    return results
```

---

## 5 — Functions

- Keep functions small — one logical operation per function.
- Use keyword arguments for optional parameters to improve readability.
- Use `*` to force keyword-only arguments when parameter names carry meaning.
- Return early to avoid deep nesting. Guard clauses first.
- Avoid mutable default arguments. Use `None` and assign inside the function body.
- Limit positional parameters. More than 3–4 suggests the function does too much or needs a data class.

```python
# Good — keyword-only, no mutable default, early return.
def send_email(
    to: str,
    subject: str,
    body: str,
    *,
    cc: list[str] | None = None,
    retries: int = 3,
) -> bool:
    if not to:
        return False

    cc = cc or []
    # ...
    return True


# Bad — mutable default argument.
def append_item(item: str, items: list[str] = []) -> list[str]:
    items.append(item)  # Shared across calls!
    return items
```

---

## 6 — Type Hints

Use type hints consistently. They serve as documentation and enable static analysis.

- Annotate all function parameters and return types.
- Use built-in generics (`list[str]`, `dict[str, int]`, `tuple[int, ...]`) — not `typing.List`, `typing.Dict` (deprecated since Python 3.9).
- Use `X | Y` union syntax (Python 3.10+) instead of `typing.Union[X, Y]`.
- Use `None` return type for functions that return nothing. Use `-> NoReturn` for functions that never return.
- Use `TypeAlias` for complex type expressions.
- Avoid `Any` — use `object` or narrow protocols instead.

```python
# Good — modern type hint syntax.
UserMap: TypeAlias = dict[str, User]

def find_user(users: UserMap, name: str) -> User | None:
    return users.get(name)

def load_config(path: Path) -> dict[str, str]:
    with open(path) as f:
        return json.load(f)
```

---

## 7 — Error Handling

- Use exceptions for exceptional conditions — not for control flow.
- Catch specific exceptions, never bare `except:` or `except Exception:` without re-raising.
- Use custom exception classes for domain-specific errors. Inherit from appropriate built-in exceptions.
- Use `raise ... from err` to preserve exception chains.
- Validate input at function boundaries. Fail fast with clear error messages.
- Use `finally` or context managers for cleanup — never rely on manual cleanup after a try block.

```python
# Good — specific exception, chain preserved.
class ConfigError(ValueError):
    """Raised when configuration is invalid."""


def load_config(path: Path) -> dict[str, str]:
    if not path.exists():
        raise FileNotFoundError(f"Config file not found: {path}")

    try:
        with open(path) as f:
            return json.load(f)
    except json.JSONDecodeError as err:
        raise ConfigError(f"Invalid JSON in {path}") from err


# Bad — bare except swallows everything.
try:
    result = do_something()
except:
    pass
```

---

## 8 — Classes

- Use classes to model concepts with state and behaviour. Use functions for stateless operations.
- Use `@dataclass` for data-holding classes. Use `frozen=True` for immutable data.
- Keep inheritance shallow. Prefer composition over deep hierarchies.
- Use `@property` for computed attributes. Avoid getter/setter methods.
- Use `__slots__` for classes with many instances to reduce memory.
- Define `__repr__` for debuggability. Define `__eq__` and `__hash__` for value semantics.
- Use abstract base classes (`abc.ABC`) to define interfaces.
- Use `@staticmethod` and `@classmethod` appropriately — `@classmethod` for alternative constructors, `@staticmethod` for utility methods that do not need class or instance access.

```python
# Good — dataclass with frozen immutability.
@dataclass(frozen=True)
class Point:
    x: float
    y: float

    def distance_to(self, other: Point) -> float:
        return math.sqrt((self.x - other.x) ** 2 + (self.y - other.y) ** 2)


# Good — abstract base class for interfaces.
class Serializer(abc.ABC):
    @abc.abstractmethod
    def serialize(self, data: dict) -> bytes:
        ...

    @abc.abstractmethod
    def deserialize(self, raw: bytes) -> dict:
        ...
```

---

## 9 — Modules and Imports

- Group imports in order: standard library, third-party, local — separated by blank lines.
- Sort imports alphabetically within each group.
- Use absolute imports. Avoid relative imports except within packages.
- Import modules, not individual names, when the module name adds context: `import os.path` over `from os.path import join`.
- Avoid wildcard imports (`from module import *`).
- One import per line. Do not combine unrelated imports.
- Place all imports at the top of the file, after module docstrings and `__future__` imports.

```python
# Good — grouped, sorted, absolute.
from __future__ import annotations

import json
import os
from pathlib import Path

import requests

from myproject.config import Settings
from myproject.models import User
```

---

## 10 — Collections and Comprehensions

- Use list/dict/set comprehensions for simple transformations. Use `for` loops for complex logic.
- Keep comprehensions to a single level of nesting. If a comprehension needs `if`/`else` and nested `for`, use a loop instead.
- Prefer `dict.get(key, default)` over `if key in dict`.
- Use `collections.defaultdict`, `collections.Counter`, and `collections.deque` for specialised use cases.
- Use unpacking (`a, b = pair`) to extract values from tuples and sequences.

```python
# Good — simple comprehension.
names = [user.name for user in users if user.is_active]

# Good — dict comprehension.
scores = {name: compute_score(name) for name in names}

# Bad — overly complex comprehension.
result = [
    transform(x, y)
    for x in items
    if x.is_valid
    for y in x.children
    if y.score > threshold
]

# Better — explicit loop for complex logic.
result = []

for x in items:
    if not x.is_valid:
        continue

    for y in x.children:
        if y.score > threshold:
            result.append(transform(x, y))
```

---

## 11 — Iterators and Generators

- Use generators for lazy evaluation of large or infinite sequences.
- Prefer `yield` over building and returning a list when the caller consumes items one at a time.
- Use `itertools` for standard iteration patterns (`chain`, `islice`, `groupby`, `product`).
- Use `enumerate` instead of manual index tracking. Use `zip` for parallel iteration.
- Prefer `any()` and `all()` over manual loop-and-flag patterns.

```python
# Good — generator for lazy processing.
def read_records(path: Path) -> Iterator[dict]:
    with open(path) as f:
        for line in f:
            yield json.loads(line)


# Good — enumerate and zip.
for i, name in enumerate(names):
    print(f"{i}: {name}")

pairs = list(zip(keys, values))

# Good — any/all over manual loops.
has_errors = any(r.is_error for r in results)
all_valid = all(item.is_valid for item in items)
```

---

## 12 — Context Managers

- Use `with` statements for all resource management (files, locks, connections, temporary state).
- Use `contextlib.contextmanager` for simple custom context managers.
- Use `contextlib.suppress` instead of try/except/pass for expected exceptions.
- Never leave resources open outside a `with` block.

```python
# Good — context manager for file I/O.
with open(path) as f:
    data = f.read()

# Good — custom context manager.
@contextlib.contextmanager
def temporary_directory() -> Iterator[Path]:
    tmp = Path(tempfile.mkdtemp())

    try:
        yield tmp
    finally:
        shutil.rmtree(tmp)

# Good — suppress expected exceptions.
with contextlib.suppress(FileNotFoundError):
    os.remove(temp_file)
```

---

## 13 — Strings

- Use f-strings for interpolation. Avoid `%` formatting and `str.format()` for new code.
- Use `str.join()` for concatenating sequences of strings.
- Use raw strings (`r"..."`) for regex patterns and file paths with backslashes.
- Use triple-quoted strings for multiline text.
- Prefer `pathlib.Path` over string manipulation for file paths.

```python
# Good — f-string interpolation.
message = f"User {name} (id={user_id}) logged in at {timestamp}"

# Good — join for sequences.
csv_line = ",".join(fields)

# Good — pathlib for paths.
config_path = Path.home() / ".config" / "myapp" / "settings.json"

# Bad — string concatenation in loops.
result = ""

for item in items:
    result += item.name + ", "  # O(n²) allocation
```

---

## 14 — Async and Concurrency

- Use `asyncio` for I/O-bound concurrency. Use `concurrent.futures` or `multiprocessing` for CPU-bound parallelism.
- Use `async`/`await` — never mix sync and async code without proper bridging.
- Use `asyncio.TaskGroup` (Python 3.11+) for structured concurrency.
- Always await coroutines. Never create fire-and-forget tasks without tracking them.
- Use `asyncio.Lock`, `asyncio.Semaphore`, and `asyncio.Queue` for coordination.
- Avoid `asyncio.run()` inside already-running event loops.

```python
# Good — structured concurrency with TaskGroup.
async def fetch_all(urls: list[str]) -> list[Response]:
    async with asyncio.TaskGroup() as tg:
        tasks = [tg.create_task(fetch(url)) for url in urls]

    return [task.result() for task in tasks]


# Good — semaphore for rate limiting.
semaphore = asyncio.Semaphore(10)

async def rate_limited_fetch(url: str) -> Response:
    async with semaphore:
        return await fetch(url)
```

---

## 15 — Self-Documenting Code

Write code that explains itself. Reserve comments for _why_, not _what_.

- Use descriptive names so the code reads naturally.
- Use docstrings (Google or NumPy style) for public modules, classes, and functions.
- Delete stale or redundant comments. A wrong comment is worse than no comment.
- Use type hints as inline documentation — they communicate intent to both humans and tools.

```python
# Bad — restates the code.
# Increment the counter.
counter += 1

# Good — explains a non-obvious constraint.
# Limit to 1000: upstream API rejects larger page sizes.
page_size = min(requested_size, 1000)


def haversine(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Compute the great-circle distance between two coordinates.

    Args:
        lat1: Latitude of first point (degrees).
        lon1: Longitude of first point (degrees).
        lat2: Latitude of second point (degrees).
        lon2: Longitude of second point (degrees).

    Returns:
        Distance in kilometres.
    """
    ...
```

---

## 16 — Whitespace as Structure

Use blank lines to reveal logical structure — like paragraphs in prose.

- **Two blank lines** before and after top-level function and class definitions.
- **One blank line** between methods in a class.
- **One blank line** between logical blocks within a function (setup, processing, result).
- **No** multiple consecutive blank lines within function or method bodies.
- **No** trailing whitespace. One trailing newline at end of file.
- Align nothing with extra spaces. Let consistent indentation handle grouping.

---

## 17 — Security Essentials

- Validate and sanitise all external input before use.
- Never use `eval()`, `exec()`, or `compile()` on untrusted input — this is code injection.
- Never use `pickle` to deserialise untrusted data — use JSON, MessagePack, or Protocol Buffers.
- Use parameterised queries for database access — never interpolate user input into SQL strings.
- Avoid `subprocess.run(shell=True)` with user-provided input — use argument lists instead.
- Use `secrets` module for cryptographic randomness, not `random`.
- Do not store secrets in source code or environment variables without proper secret management.
- Use `hashlib` with salt for password hashing, or prefer `bcrypt`/`argon2`.

```python
# Good — parameterised query.
cursor.execute("SELECT * FROM users WHERE id = ?", (user_id,))

# Bad — SQL injection vulnerability.
cursor.execute(f"SELECT * FROM users WHERE id = {user_id}")

# Good — subprocess without shell.
subprocess.run(["git", "log", "--oneline", "-n", "10"], check=True)

# Bad — shell injection vulnerability.
subprocess.run(f"git log --oneline -n {count}", shell=True)
```

---

## 18 — Testing

- Use `pytest` as the test framework. Use `unittest` only when integrating with legacy code.
- Name test files `test_*.py` and test functions `test_*`.
- Name tests descriptively: `test_rejects_empty_input`, `test_returns_default_on_missing_key`.
- Keep tests independent — no shared mutable state between test cases.
- Use fixtures (`@pytest.fixture`) for setup and teardown, not manual setUp/tearDown.
- Use `pytest.raises` for exception assertions. Use `pytest.approx` for floating-point comparisons.
- Test both success and failure paths. Test edge cases (empty input, None, boundaries).
- Use `unittest.mock.patch` or `pytest-mock` for isolating dependencies.
- Keep test files next to the code they test, or in a parallel `tests/` directory.

```python
# Good — descriptive test names, fixtures, edge cases.
@pytest.fixture
def sample_config(tmp_path: Path) -> Path:
    config = tmp_path / "config.json"
    config.write_text('{"key": "value"}')
    return config


def test_loads_valid_config(sample_config: Path) -> None:
    result = load_config(sample_config)
    assert result == {"key": "value"}


def test_raises_on_missing_file(tmp_path: Path) -> None:
    missing = tmp_path / "nonexistent.json"

    with pytest.raises(FileNotFoundError):
        load_config(missing)


def test_raises_on_invalid_json(tmp_path: Path) -> None:
    bad_config = tmp_path / "config.json"
    bad_config.write_text("not json")

    with pytest.raises(ConfigError):
        load_config(bad_config)
```

---

## 19 — Performance

- Profile before optimising. Use `cProfile`, `timeit`, or `py-spy`.
- Use appropriate data structures: `set` for membership checks, `deque` for queues, `dict` for lookups.
- Use generators and `itertools` to avoid materialising large intermediate lists.
- Use `str.join()` instead of repeated `+=` for string building.
- Use `__slots__` on classes with many instances.
- Avoid repeated attribute lookups in tight loops — cache in a local variable.
- Use `functools.lru_cache` for expensive pure function calls.
- Move imports to the top level — conditional imports in hot paths add overhead.

---

## 20 — Anti-Patterns

- **Bare `except:`.** Always catch specific exceptions.
- **Mutable default arguments.** Use `None` and assign inside the function.
- **Wildcard imports.** `from module import *` pollutes the namespace and breaks tooling.
- **Global mutable state.** Pass data explicitly through function parameters.
- **`eval()` / `exec()` on user input.** This is code injection.
- **Ignoring return values.** Check the result of fallible functions.
- **String concatenation in loops.** Use `str.join()` or f-strings.
- **Shadowing built-ins.** Never name variables `list`, `dict`, `type`, `id`, `input`, `open`, `map`, `filter`.
- **`isinstance` chains.** Use polymorphism, protocols, or `match`/`case` instead.
- **Deep inheritance.** Prefer composition. If the hierarchy exceeds 2–3 levels, reconsider the design.
- **`pickle` for untrusted data.** Use JSON or another safe serialization format.

---

## 21 — Checklist

- [ ] All functions have type-annotated parameters and return types.
- [ ] Exceptions are specific and chained with `raise ... from err`.
- [ ] No bare `except:` or `except Exception:` without re-raise.
- [ ] No mutable default arguments.
- [ ] No wildcard imports (`from x import *`).
- [ ] No `eval()`, `exec()`, or `pickle` on untrusted input.
- [ ] f-strings used for interpolation, `str.join()` for concatenation.
- [ ] Names follow the conventions in §2. No shadowed built-ins.
- [ ] Imports are grouped (stdlib → third-party → local) and sorted.
- [ ] `with` statements used for all resource management.
- [ ] Tests cover the primary success and failure paths.
- [ ] Files end with a single trailing newline.
