---
description: "Research other languages, libraries, and GUI frameworks read-only and produce a prioritized, actionable list of candidate additions — language features, stdlib modules, types, and functions — that fit Luma's philosophy, without changing any code"
agent: "agent"
tools: ["search", "read"]
argument-hint: "Optional focus, e.g. 'string handling', 'concurrency', 'GraphicalUi', or 'the whole language and stdlib'"
version: 1
lastUpdated: "2026-08-01"
---

# New Requirements

Research how other programming languages, their standard libraries, and Elm-architecture GUI frameworks solve problems Luma's users face — and produce a **prioritized list of candidate additions** to Luma. This prompt is the discovery counterpart to the four implementation prompts it feeds: [new-language-feature.prompt.md](new-language-feature.prompt.md), [new-stdlib-module.prompt.md](new-stdlib-module.prompt.md), [new-stdlib-type.prompt.md](new-stdlib-type.prompt.md), and [new-stdlib-function.prompt.md](new-stdlib-function.prompt.md). This one *finds, filters, and ranks* candidate capabilities and routes each to the right builder; those *implement* a single chosen item end-to-end with the test suite green. It mirrors the two-step [refactor-audit](refactor-audit.prompt.md) → [refactor](refactor.prompt.md) and [bug-search](bug-search.prompt.md) → [bug-fix](bug-fix.prompt.md) pairings.

This is a **read-only study**. Make no code changes, and no build is required. The deliverable is a ranked report, not a diff — proving a candidate out is the first step of whichever implementation prompt picks it up.

> **Scope vs sibling prompts:** Stay **additive**. This prompt proposes *new capabilities* Luma does not yet have. [refactor-audit.prompt.md](refactor-audit.prompt.md) finds behaviour-preserving structural improvements; [bug-search.prompt.md](bug-search.prompt.md) finds defects; [consistency-check.prompt.md](consistency-check.prompt.md) finds drift between artefacts (code vs CMake, runtime vs type checker, docs vs implementation). When a candidate is really a bug, a smell, or a consistency gap, note it briefly and cross-reference the right prompt rather than restating it here.

## 1 — Understand Luma's Identity and Constraints

Luma is opinionated. A capability that is idiomatic in another language is only a candidate here if it earns its place *in Luma's terms*. Before proposing anything, internalise the philosophy so every candidate either fits it natively or is reshaped to fit — a candidate that fights the philosophy is worse than no candidate at all.

- [Luma_Initial_Concept.md](../../documents/Luma_Initial_Concept.md) — the founding vision: **as easy as Python, as secure as Rust**, drawing on C++, Python, Java, Rust, Swift, and Carbon. Note the explicit **non-goals** in its "Next" section — no classes (data and behaviour stay separate), no `null` (optionals instead), no `any` (generics instead).
- [Luma_User_Manual.md](../../documents/Luma_User_Manual.md) — the **current language surface** (lexer→VM semantics). Read this to establish what already exists so you never propose something Luma already has.
- [Luma_Standard_Library_Reference.md](../../documents/Luma_Standard_Library_Reference.md) — the **current stdlib surface**: every module, type, and function. Same purpose — separate genuine gaps from things already shipped.
- [Luma_Coding_Guidelines.md](../../documents/Luma_Coding_Guidelines.md) — the idioms a new capability must feel native to (immutability by default, pipe-first flow, expression orientation).
- [Luma_Error_Handling.md](../../documents/Luma_Error_Handling.md) — the `result`/`optional` conventions and error categories any fallible new capability must adopt (§6 Standard Library Conventions, §8 Anti-Patterns).
- [learnings.instructions.md](../../instructions/learnings.instructions.md) and [software-architecture.instructions.md](../../instructions/software-architecture.instructions.md) — established patterns, the **deliberate non-goals** that must not be reintroduced, and the over-engineering discipline (a capability must pay for itself; no speculative generality).

If the focus is the GUI, read [Luma_Solaris_Guide.md](../../documents/Luma_Solaris_Guide.md) first and then [Luma_GraphicalUi_Guide.md](../../documents/Luma_GraphicalUi_Guide.md): **Solaris** is Luma's beginner-first authoring surface — a built-in stdlib module following the **Model-View-Update (Elm) architecture** (typed `record` models, `choice` messages, `|>` modifier chains) and the primary way apps are written — while `GraphicalUi` is the lower-level webview engine beneath it that you reach for only in advanced scenarios the surface does not yet expose. Both **already are** Elm-architecture, so mine Elm-style frameworks for *widgets, layouts, commands, subscriptions, theming, and accessibility* they lack — do **not** propose re-architecting them to Elm — and prefer landing a beginner-first GUI candidate on the Solaris surface, dropping to `GraphicalUi` only when the surface cannot express it. It renders through an **embedded HTML/CSS/JS webview** (WebView2 on Windows, WebKit on macOS, WebKitGTK on Linux) that turns widgets into DOM, so a GUI candidate is cheap when it maps onto what that substrate can already draw and effectively out of scope when it would need native rendering outside the webview — weigh that in every GUI candidate's fit and effort. To gauge effort and placement, skim [Luma_Software_Architecture.md](../../documents/Luma_Software_Architecture.md) for where each kind of addition lives in the pipeline and stdlib.

## 2 — Scope and Ground Rules

- **Default scope** is the whole language and standard library. If the invocation names a focus area (a domain like "dates and time", a module like "String", or a theme like "concurrency"), restrict the study to it and its immediate neighbours.
- **Establish the baseline first.** Confirm what Luma already offers (User Manual + Stdlib Reference) before proposing anything, so every candidate is genuinely new. A candidate that duplicates an existing feature under a different name is not a finding.
- **Additive only.** Propose capabilities that *add* to Luma without breaking existing programs. If an idea would require a breaking change to existing semantics, record that as an explicit risk/flag rather than smuggling it in.
- **Verify what exists.** Read the docs and, where useful, the source you cite — never claim Luma "lacks X" without confirming it is absent. A hallucinated gap wastes the builder's time. Web research is allowed to confirm a peer language's specifics; the Luma-side check is mandatory.
- **Make no changes.** Do not edit, format, build, or run tests. Producing the ranked, routed list is the whole job.

## 3 — Where to Research

Draw candidates from three wells, and label each finding's inspiration so the builder can consult the source:

1. **Peer languages and their standard libraries.** Survey what beginners reach for in **C, C++, Carbon, Java, C#, Kotlin, JavaScript, TypeScript, Rust, Swift, and Python** — and how their stdlibs package it. Favour capabilities that appear across *several* of them (a sign they are broadly useful, not niche) and that a Luma beginner would expect to find. Adapt the ergonomics to Luma: a Python method becomes a pipe-first free function, a Rust `Result`-returning API maps onto Luma's `result<T>`, a Swift optional-chaining idiom onto Luma's `optional<T>`.
2. **Elm-architecture / model-update-view GUI frameworks.** Mine **Elm** (and `elm-ui`), **Iced** (Rust), **SwiftUI**, **Jetpack Compose**, **Flutter**, and **Redux/React** for widgets, layout primitives, commands, subscriptions, theming, animation, and accessibility affordances that Luma's GUI — the beginner-first **Solaris** surface and the `GraphicalUi` engine beneath it — does not yet expose, expressed in Luma's existing Elm model, not a new paradigm. Because that model renders through a webview, also mine **webview-based app frameworks** (Tauri, Electron, Wails, Neutralino) and the **web platform** itself — HTML form controls, CSS capabilities, the Web Animations model, and WAI-ARIA accessibility — for affordances that translate directly into the DOM `GraphicalUi` already emits.
3. **Novel and creative approaches.** Do not limit yourself to imitation. Propose original ideas — new syntax, a new stdlib abstraction, a new safety affordance — where they serve Luma's beginner-first, safety-first mission better than any borrowed one. Hold these to the *same* fit bar as borrowed ideas (§6), and mark their inspiration as "Novel".

## 4 — What to Look For (and How to Route It)

Scan for gaps across the capability areas below, then classify each candidate into exactly one of the four **kinds** — the kind determines which implementation prompt receives the handoff.

**Capability areas to scan:** text and string handling; collections and iteration (arrays, dictionaries, sets, queues, stacks, trees, graphs); numerics and math (integers, numbers, linear algebra, calculus, random); dates, times, and durations; data formats and serialization (JSON, CSV, XML, encoding, compression, hashing); files, processes, networking, and other OS surfaces (all sandbox-aware); error handling and control flow; concurrency and structured parallelism; the GUI (the beginner-first `Solaris` surface and the `GraphicalUi` engine beneath it); and developer-facing ergonomics (logging, testing, reflection-free introspection).

**The four kinds — pick the smallest one that delivers the value:**

1. **Language feature** → [new-language-feature.prompt.md](new-language-feature.prompt.md). New syntax, an operator, a keyword, or a type-system construct that the lexer, parser, type checker, compiler, and VM must all learn. Highest reach — and highest **conceptual cost for beginners** — so reserve it for capabilities the stdlib genuinely cannot express.
2. **Stdlib module** → [new-stdlib-module.prompt.md](new-stdlib-module.prompt.md). A cohesive new namespace of related built-ins for a domain Luma does not yet cover (e.g. a hypothetical `Yaml` module alongside the existing `Json`, `Csv`, and `Xml`). Choose this when the value is a *family* of functions, not one call.
3. **Stdlib type** → [new-stdlib-type.prompt.md](new-stdlib-type.prompt.md). A `record` or `choice` type a module returns or consumes (e.g. the existing `Http.Response` record, `Log.Level` choice). Choose this when the value is a new *shape of data* that accompanying functions build or read.
4. **Stdlib function** → [new-stdlib-function.prompt.md](new-stdlib-function.prompt.md). A single built-in added to a module that already exists (e.g. a hypothetical `Math.gcd` on the existing `Math` module). The smallest, lowest-risk kind — prefer it whenever one function on an existing module covers the gap.

**Routing rule:** prefer **function < type < module < language feature** whenever more than one kind could deliver the capability. Adding stdlib surface is cheaper and safer than adding language surface, and keeps the core small — a core Luma value. Only escalate to a language feature when no library form can express the capability cleanly (e.g. it needs new syntax, new evaluation rules, or type-checker support the catalog cannot encode). When a candidate is really several additions (a new module *plus* its types *plus* its functions), record it as one headline candidate that routes to [new-stdlib-module.prompt.md](new-stdlib-module.prompt.md) and note the constituent types/functions in its detail.

## 5 — How to Gather Evidence

Use the workspace search and file-reading tools to establish the Luma baseline, and your knowledge of peer languages (confirmed with web research where specifics matter) to find the gap. Parallelize independent read-only exploration. Do **not** build.

- **Map the current surface.** Skim the Stdlib Reference module by module and the User Manual construct by construct; keep a working note of what exists so candidates are provably new. Grep `core/runtime/stdlib/` and `shared/stdlib/` to confirm a function or module is genuinely absent before claiming it.
- **Compare against peers.** For each capability area, ask "what does a beginner coming from Python/JS/Java/Rust/Swift expect here that Luma is missing?" — then check whether the expectation survives translation into Luma's model.
- **Study the nearest Luma precedent.** For every candidate, find the closest existing thing (the module it would join, the type it resembles, the operator it parallels). Precedent both proves the gap is real and gives the builder a pattern to copy — cite it.
- **Anchor with a concrete use case.** Sketch the beginner task the capability enables and how it is done *today* (the awkward workaround, or "not possible"). If you cannot write the workaround, the gap may not be real.

## 6 — Filter and Prioritize

First apply the **fit filter** — a hard gate. A candidate must satisfy Luma's principles, or be reshaped until it does, or be dropped:

- **Beginner-first.** Teachable in a sentence, with one obvious way to use it. It should *lower* cognitive load, not add a second way to do an existing thing.
- **Safe by default.** Works with static, strong typing; immutable by default (`mutable` opt-in); **no `null`** (use `optional<T>`), **no `any`** (use generics); any OS-touching surface is sandbox-aware (`if (!sandbox)`).
- **Data and behaviour stay separate.** No classes, no inheritance. Model data as `record`, `choice`, `interface`, tuples; model behaviour as free functions with the **pipe-first** convention (receiver is the first argument).
- **Fits Luma's value and error model.** `integer` for indices, `number` otherwise; fallible operations return `result<T>` / `optional<T>` per [Luma_Error_Handling.md](../../documents/Luma_Error_Handling.md); concurrency stays within structured `task_scope`.
- **Minimal, consistent syntax.** No semicolons required, `#` comments, string interpolation and the `|>` pipe stay idiomatic. A language feature must not introduce ambiguity or parsing conflict.

Then rank every surviving candidate so the builder picks the highest-value item first. Weigh four dimensions:

- **Value** — how many beginners it helps, how often the need arises, and how much it improves expressiveness or safety.
- **Fit** — how natively it sits in Luma's philosophy after any reshaping (a candidate that needed heavy reshaping fits less well than one that dropped straight in).
- **Effort** — rough size, which correlates with kind: a stdlib **function** is Small, a **type** Small–Medium, a **module** Medium–Large, a **language feature** Large (it touches the whole pipeline).
- **Risk** — teachability cost, interaction with existing features, and blast radius. Language surface carries more risk than stdlib surface; anything that flirts with a non-goal is higher risk.

Synthesize these into a single **Priority** (High / Medium / Low): Priority rises with value and fit, and falls with effort and risk. Favour high-value, high-fit, low-cost items — typically stdlib functions and types — at the top. Where two candidates tie on value, prefer the one with the smaller kind and the cleaner fit.

## 7 — What to Exclude

- **Nothing that already exists.** If the User Manual or Stdlib Reference already covers it, it is not a candidate — note the existing feature and move on.
- **Nothing that violates a non-goal and cannot be reshaped.** Do **not** propose classes/inheritance, `null`, `any`, exceptions-as-control-flow, implicit numeric coercions, pointer arithmetic, user-defined operator overloading, unrestricted macros or runtime reflection, dynamic typing, or global mutable state. These conflict with Luma's founding decisions ([Luma_Initial_Concept.md](../../documents/Luma_Initial_Concept.md), [learnings.instructions.md](../../instructions/learnings.instructions.md)). If a useful idea arrives wrapped in one of these, propose the *reshaped, Luma-native* form or drop it.
- **No over-engineering.** Per the project's implementation discipline, a capability must pay for itself in real beginner value. Do not propose abstractions, generality, or "power-user" features with no concrete, common use case.
- **No breaking changes.** Additive proposals only. If the value genuinely requires changing existing semantics, record it as a flagged risk for human decision — do not present it as a drop-in addition.
- **Not bugs, smells, or drift.** Defects belong in [bug-search.prompt.md](bug-search.prompt.md), behaviour-preserving structure in [refactor-audit.prompt.md](refactor-audit.prompt.md), artefact drift in [consistency-check.prompt.md](consistency-check.prompt.md). Note and cross-reference; do not restate here.
- **No hallucinated gaps.** Every candidate needs a Luma-side check confirming the capability is absent.

## 8 — Output Format

Produce the report in two parts.

> The `N01`–`N04` rows are **illustrative of the format and routing only**, not a backlog. `Math.gcd`, a `Yaml` module, and a list comprehension are shown because Luma does not ship them today — but its surface is large and still growing, so re-run the §5 absence check against the current User Manual and Stdlib Reference before recording any real candidate (§7, "No hallucinated gaps").

First, a summary table ordered by priority for quick scanning:

```markdown
| ID  | Candidate                                | Kind             | Inspiration  | Priority | Effort | Fit  |
| --- | ---------------------------------------- | ---------------- | ------------ | -------- | ------ | ---- |
| N01 | `Math.gcd` — greatest common divisor     | Stdlib function  | Python, C++  | High     | Small  | High |
| N02 | `Yaml` module — parse and emit YAML      | Stdlib module    | Python, Rust | High     | Large  | High |
| N03 | List comprehension `[x*2 for x in xs]`   | Language feature | Python       | Low      | Large  | Med  |
| N04 | …                                        | …                | …            | …        | …      | …    |
```

Then, one detailed entry per candidate:

```markdown
### N01 — <Short, capability-oriented title>

- **Kind:** <Language feature | Stdlib module | Stdlib type | Stdlib function> <(target module, for stdlib kinds)>
- **Priority:** <High | Medium | Low>
- **Inspiration:** <language(s) / library / GUI framework, or "Novel">
- **Gap:** <the beginner task this enables and how it is done today — the awkward workaround, or "not currently possible">
- **Proposal:** <concrete sketch. For stdlib: the signature(s) in catalog style, e.g. `Math.gcd(a: integer, b: integer) -> integer`. For a language feature: a short Luma snippet plus the rough grammar and how it parses alongside existing syntax.>
- **Philosophy fit:** <how it satisfies §6 — beginner value, safety, data/behaviour separation, error model — and, if it needed reshaping to fit, what you changed from the source idiom>
- **Luma precedent:** <the nearest existing module/type/operator the builder can pattern-match on>
- **Effort / risk:** <Small | Medium | Large> effort, <Low | Medium | High> risk
- **Handoff:** route to <new-language-feature | new-stdlib-module | new-stdlib-type | new-stdlib-function>.prompt.md — "<one-line goal string ready to paste into that prompt's argument>"
```

Close with a short note on what you would build first and why (highest value and fit at the lowest cost — usually a stdlib function or type). Make each **Handoff** goal specific enough, and matched to the right prompt, that the chosen implementation prompt can act on it without re-discovering the gap.
