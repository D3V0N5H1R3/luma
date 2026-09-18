# Luma — Sandbox and Threat Model

This document defines the security model of Luma's sandbox mode (`--box`): what
it protects, what it deliberately does not, how it is enforced in the
interpreter, and how that enforcement is validated. It is the normative
reference for anyone running untrusted or semi-trusted Luma programs and for
contributors adding standard-library modules.

---

## Table of Contents

1. [Overview](#1--overview)
2. [Threat Model](#2--threat-model)
3. [Capability Model](#3--capability-model)
4. [Guarantees](#4--guarantees)
5. [Non-Goals and Limitations](#5--non-goals-and-limitations)
6. [Enforcement](#6--enforcement)
7. [Validation](#7--validation)
8. [Adding a Module Safely](#8--adding-a-module-safely)
9. [Recommended Deployment](#9--recommended-deployment)

---

## 1 — Overview

Luma programs are ordinarily trusted: they can read and write files, spawn
processes, open sockets, and make network requests through the standard
library. **Sandbox mode** — enabled with the `--box` command-line flag —
restricts a program to computation and in-memory data structures by removing
every standard-library module that can reach the outside world.

Sandbox mode is a **language-level capability restriction**. It is intended for
running code whose *logic* is untrusted but whose *host* is cooperative — for
example, evaluating user-supplied expressions, grading exercises, or executing
plug-in scripts inside a larger trusted application. It is **not** a substitute
for operating-system isolation when executing actively hostile code (see
[§5](#5--non-goals-and-limitations)).

---

## 2 — Threat Model

### Assets to Protect

- The host filesystem (confidentiality and integrity).
- The host process table and the ability to execute programs.
- The host network identity and outbound/inbound connectivity.
- Interactive terminal and standard I/O of the host process.
- Host availability (the sandboxed program must not exhaust unbounded
  resources — see [§4](#4--guarantees)).

### Adversary

A Luma program whose **source is untrusted** but which is parsed, compiled, and
executed by a trusted interpreter build on the host. The adversary can write
arbitrary *well-formed* Luma, including deeply nested data, heavy recursion,
large allocations, and pathological input to standard-library parsers.

The adversary is assumed **not** to be able to:

- supply a malicious interpreter binary or standard library,
- exploit a memory-safety defect in the interpreter (this is a *residual* risk,
  mitigated by fuzzing rather than eliminated — see
  [§5](#5--non-goals-and-limitations)),
- influence the host outside the interpreter process.

### Trust Boundary

The boundary is the set of standard-library functions exposed to the program.
Everything reachable from Luma source — module lookups, the include resolver,
and resource consumption — is inside the boundary and must be mediated. The
interpreter's own C++ internals are trusted.

---

## 3 — Capability Model

Every standard-library function is tagged with a **capability** describing the
host resource it needs. The capabilities are defined in
`shared/stdlib/stdlib_catalog.hpp`:

| Capability   | Meaning                                 | Example functions                             |
| ------------ | --------------------------------------- | --------------------------------------------- |
| `None`       | Pure computation; no host access        | `Math.absolute`, `String.length`, `Array.map` |
| `Console`    | Interactive terminal / standard I/O     | `Console.prompt`, `Console.read_line`         |
| `FileSystem` | Reads or writes the filesystem          | `FileSystem.read_file`, `KeyValueStore.*`     |
| `Process`    | Spawns or controls processes            | `Process.run`, `Process.exit`                 |
| `Network`    | Opens sockets or makes network requests | `Socket.connect`, `Http.get`                  |

A module is **safe** if all of its functions are `None`. Sandbox mode blocks
every module that is *not* safe — a **deny-by-default** posture over host
capabilities: a function is reachable in the sandbox only if it declares that it
needs no host resource.

Sandbox mode today is **all-or-nothing** over the non-`None` capabilities: `--box`
removes Console, FileSystem, Process, and Network access together. Finer-grained
per-capability grants (for example, allowing FileSystem but not Network) are a
planned extension; the capability tags already carry the information such a
policy would need.

A small number of modules are **sandbox-aware** (`Compression`, `Hash`, `Log`):
they remain available but register a reduced, side-effect-free surface when the
sandbox flag is set.

---

## 4 — Guarantees

When run with `--box`, the interpreter guarantees that a program **cannot**:

- read or write the filesystem, or open a key-value store,
- spawn, signal, or wait on operating-system processes, or call `Process.exit`,
- open, listen on, or connect sockets, or make HTTP requests,
- read from or prompt on the interactive console,
- reach a blocked module by any name-resolution path — the module is both
  **unregistered** (never bound in the environment) and **blocklisted** (a
  lookup produces a clear "not available in sandbox mode" error rather than a
  bare "undefined variable").

Independently of the sandbox flag, the interpreter also bounds resource
consumption via the limits centralised in `core/common/resource_limits.hpp`
(maximum call depth, collection and string sizes, task-queue length, open
sockets, and more; all overridable through `LUMA_LIMIT_*` environment
variables), and the include resolver rejects circular includes, `..` path
traversal, and symlinked include paths.

---

## 5 — Non-Goals and Limitations

Sandbox mode is **not an operating-system jail**. In particular:

- **It shares the interpreter's address space.** A memory-safety defect in the
  interpreter (a buffer overflow, use-after-free, or type confusion reachable
  from crafted Luma source or from a standard-library parser) could bypass every
  language-level check. This residual risk is *mitigated, not eliminated*, by
  continuous fuzzing and sanitizer coverage (see [§7](#7--validation)) — it is
  the primary reason the two efforts are coupled.
- **Resource limits are global, not per-tenant.** The `LUMA_LIMIT_*` bounds
  apply to the whole process; the sandbox does not yet enforce a separate CPU-time
  or memory budget per evaluation. A sandboxed program can still consume CPU and
  memory up to the global limits.
- **Timing and other side channels are not addressed.** `DateTime` and `Random`
  remain available in the sandbox (they are `None`-capability), so wall-clock
  time and randomness are observable.
- **It does not defend against a malicious host or interpreter build.** Supply
  chain integrity (verifying the interpreter and standard library) is out of
  scope for this mechanism.

For executing **actively hostile** code, run the sandboxed interpreter behind
OS-level isolation as well (see [§9](#9--recommended-deployment)).

---

## 6 — Enforcement

Enforcement lives at the boundary between the standard-library registry and the
runtime environment:

- **Registration** — `register_all(env, sandbox)` in
  `core/runtime/stdlib/common/stdlib_registry.hpp` skips every module tagged
  `os_only` when `sandbox` is true, so blocked modules are never bound. The
  module table (`kModules`) is the single source of truth for which modules are
  OS-only.
- **Blocklisting** — after registration, the same path records the blocked
  module prefixes on the environment via `set_sandbox_blocked`. The blocked set
  is derived from the catalog's capability tags
  (`stdlib::sandbox_blocked_modules()`), so it stays in lock-step with the
  capabilities rather than being maintained by hand.
- **Lookup** — `Environment::get` resolves a qualified name, then, on a miss,
  calls `verify_sandbox_access`, which walks up to the root scope's
  `SandboxPolicy` (`core/runtime/interpreter/sandbox_policy.hpp`) and throws a
  `RuntimeError` — "'…' is not available in sandbox mode (--box)" — if the
  module prefix is blocked. This makes the failure explicit and diagnosable.

The two independent tables (the registry's `os_only` flags and the catalog's
capability-derived blocked set) must always name the same modules; a regression
test enforces that invariant (see below).

---

## 7 — Validation

- **Capability and blocklist conformance** —
  `tests/runtime/stdlib_catalog_conformance_test.cpp` verifies that the blocked
  set contains exactly the OS-capable modules, that the registry's `os_only`
  flags and the catalog's blocked set agree, that a sandbox environment throws
  the sandbox error for each blocked module through the real lookup path, and
  that safe modules still resolve.
- **Feature tests** — `tests/features/language/sandbox.luma` (run with
  `luma --box --test`) confirms that safe modules keep working and that
  FileSystem, Process, Socket, and Http access is rejected.
- **Fuzzing** — the fuzz targets that execute arbitrary programs
  (`fuzz_vm`, `fuzz_structured`, and the whole pipeline via `fuzz_pipeline.hpp`)
  register the standard library **in sandbox mode**, so generated programs
  cannot perform real host side effects while fuzzing and the sandbox surface is
  itself continuously exercised. `fuzz_path` additionally carries an oracle that
  fails if the FileSystem path validator ever accepts a path that escapes the
  sandbox working directory. The fuzz workflow (`.github/workflows/fuzz.yml`)
  runs nightly on Linux/Clang with AddressSanitizer and a persisted, seeded
  corpus so coverage accumulates over time.

> **Note:** Fuzzing is authoritative on Linux/Clang. On Windows, a clang-cl fuzz
> executable may `__fastfail` for reasons unrelated to Luma; validate seeds with
> `-runs=0` there and run real campaigns on Linux, as CI does.

---

## 8 — Adding a Module Safely

When adding a standard-library module or function:

1. **Tag its capability** in the registration table in
   `shared/stdlib/stdlib_catalog.cpp`. Use the narrowest capability that
   describes the host resource it touches; use `None` only if it is pure
   computation.
2. **Mark OS-only modules** with `os_only = true` in `kModules`
   (`core/runtime/stdlib/common/stdlib_registry.hpp`) so they are skipped during
   sandbox registration, or make the module **sandbox-aware** if it can offer a
   safe reduced surface.
3. **Do not** reach the filesystem, network, processes, console, or environment
   from a `None`-capability function — even indirectly. Any host access must be
   reflected in the capability tag, or it is a sandbox escape.
4. Run the conformance and sandbox feature tests; the `os_only`/blocked-set
   invariant will fail the build if the two tables diverge.

---

## 9 — Recommended Deployment

For untrusted input where the *logic* is adversarial but the workload is pure
computation, `--box` alone is appropriate. For **actively hostile** code, layer
defence in depth:

- Run the interpreter under OS-level isolation (a container, a `seccomp`
  profile, or a restricted user) so an interpreter memory-safety defect cannot
  reach the host.
- Set conservative `LUMA_LIMIT_*` bounds and impose an external CPU-time and
  memory limit (for example `ulimit`, cgroups, or a job object) on the process.
- Keep the interpreter build current, since fuzzing-discovered fixes harden the
  residual memory-safety surface that the language-level sandbox cannot cover.
