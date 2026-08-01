---
description: "Use when writing, reviewing, or modifying GitHub Actions workflow files. Covers triggers, permissions, job structure, caching, security, and CI/CD best practices."
applyTo: ".github/workflows/**"
priority: reference
---

# Working with GitHub Actions

A reference for writing, debugging, and maintaining GitHub Actions workflows.
Every recommendation here prioritises **simplicity, readability, security, and fail-safety**.

> **Note — examples are illustrative.** Some snippets below use common ecosystem tooling
> (Node, npm, Python) to demonstrate a pattern generically. Luma's own CI builds C++ with
> CMake and runs CTest across an operating-system matrix — see §7.3 for the matrix pattern
> and §1.1 of the [recipes companion](github-actions-recipes.instructions.md) for a complete
> C++/CMake workflow that mirrors this repository. Apply the *pattern*, not the specific
> tooling, when writing workflows for this project.

> **Note — recipes live in a companion guide.** Complete, copy-paste workflow files
> (C++/CMake CI, Docker publish, release, deployment, CodeQL/SAST) and workflow debugging
> guidance live in [github-actions-recipes.instructions.md](github-actions-recipes.instructions.md),
> a manually-referenced companion with no `applyTo`. This guide covers the conventions; that
> one collects the ready-to-adapt examples.

---

## Table of Contents

1. [Core Concepts](#1--core-concepts)
2. [Formatting and Whitespace](#2--formatting-and-whitespace)
3. [Naming Conventions](#3--naming-conventions)
4. [Workflow File Anatomy](#4--workflow-file-anatomy)
5. [Triggers (`on:`)](#5--triggers-on)
6. [Permissions](#6--permissions)
7. [Jobs](#7--jobs)
8. [Steps](#8--steps)
9. [Environment Variables and Secrets](#9--environment-variables-and-secrets)
10. [Caching and Artifacts](#10--caching-and-artifacts)
11. [Reusable Workflows and Composite Actions](#11--reusable-workflows-and-composite-actions)
12. [Security Checklist](#12--security-checklist)
13. [Style and Maintenance Guidelines](#13--style-and-maintenance-guidelines)
14. [Checklist Before Committing a Workflow](#14--checklist-before-committing-a-workflow)

> Complete workflow recipes and debugging guidance live in the companion
> [github-actions-recipes.instructions.md](github-actions-recipes.instructions.md).

---

## 1 — Core Concepts

GitHub Actions is a CI/CD platform built into GitHub.
The hierarchy is: **Workflow → Job → Step**.

| Concept      | Lives in                     | Purpose                                     |
| ------------ | ---------------------------- | ------------------------------------------- |
| **Action**   | Marketplace / local repo     | A reusable unit of automation               |
| **Job**      | Inside a workflow            | A unit of work that runs on one runner      |
| **Runner**   | GitHub-hosted or self-hosted | The machine that executes a job             |
| **Step**     | Inside a job                 | A single command or action invocation       |
| **Workflow** | `.github/workflows/*.yml`    | A complete automation triggered by an event |

Key things to remember:

- Jobs run **in parallel by default**. Use `needs:` to create dependencies.
- Each job gets a **fresh runner**. Files do not persist between jobs unless explicitly shared via artifacts or caches.
- Steps within a job run **sequentially** in the same shell environment and share the filesystem.

---

## 2 — Formatting and Whitespace

Workflow files are YAML. Consistent formatting makes them scannable at a glance.

### Indentation

Use 2 spaces per indentation level (the YAML standard). Do not use tabs. Indent map values and sequence items consistently.

```yaml
jobs:
    build:
        runs-on: ubuntu-latest
        steps:
            - uses: actions/checkout@v4
            - name: Build
              run: cmake --build build
```

### Blank Lines

Use one blank line to separate logical groups within a workflow:

- Between the `name` / `on` / `permissions` / `concurrency` / `jobs` top-level blocks.
- Between jobs.
- Between groups of related steps within a job (e.g., setup steps vs. build steps vs. test steps).

Do not use multiple consecutive blank lines.

### Line Length

Aim for a maximum of 100 characters per line. Break long `run:` commands using YAML block scalars (`|` or `>`). Break long `if:` conditions or `with:` values at logical boundaries.

### Comments

Start comments with `#` followed by a single space. Use comments to explain **why**, not **what**. Add comments for non-obvious triggers, permissions, environment variables, and conditional logic.

```yaml
# Only deploy when a version tag is pushed — not on every push to main.
on:
    push:
        tags: ["v*.*.*"]
```

---

## 3 — Naming Conventions

| Entity                       | Convention                          | Examples                                        |
| ---------------------------- | ----------------------------------- | ----------------------------------------------- |
| Artifact names               | `kebab-case`                        | `build-output`, `coverage-report`               |
| Cache keys                   | Descriptive, `kebab-case` with hash | `pip-linux-${{ hashFiles('...') }}`             |
| Custom action inputs/outputs | `kebab-case`                        | `node-version`, `build-path`                    |
| Environment variables        | `UPPER_SNAKE_CASE`                  | `NODE_ENV`, `DATABASE_URL`                      |
| Job IDs                      | `kebab-case`                        | `build`, `run-tests`, `deploy-staging`          |
| Secrets                      | `UPPER_SNAKE_CASE`                  | `DEPLOY_KEY`, `API_TOKEN`                       |
| Step `name:`                 | Sentence case, imperative verb      | `Install dependencies`, `Run unit tests`        |
| Step IDs (`id:`)             | `kebab-case`                        | `get-version`, `upload-artifact`                |
| Workflow `name:`             | Title Case, descriptive             | `CI`, `Release Publish`, `Deploy to Production` |
| Workflow file names          | `kebab-case.yml`                    | `ci.yml`, `release-publish.yml`                 |

- **Name every step.** Descriptive `name:` fields make workflow logs scannable.
- Choose descriptive names. `run-tests` — good. `j1` — bad.
- Use the naming conventions of upstream actions for their inputs — do not rename them.

---

## 4 — Workflow File Anatomy

Every workflow file lives under `.github/workflows/` and must be valid YAML.

```yaml
name: CI # Human-readable name (shown in the UI)

on: # 1. Trigger definition
    push:
        branches: [main]
    pull_request:
        branches: [main]

permissions: # 2. Least-privilege token scope
    contents: read

concurrency: # 3. Prevent redundant runs
    group: ${{ github.workflow }}-${{ github.ref }}
    cancel-in-progress: true

jobs: # 4. Jobs
    build:
        runs-on: ubuntu-latest # Runner image
        timeout-minutes: 15 # Always set an upper bound
        steps:
            - uses: actions/checkout@v4 # Pin to major version tag
            - run: echo "Hello, world!"
```

Always include at minimum: `name`, `on`, `permissions`, and `jobs`.

---

## 5 — Triggers (`on:`)

### 5.1 — Common Triggers

```yaml
# Push to specific branches
on:
  push:
    branches: [main, release/*]

# Pull requests targeting specific branches
on:
  pull_request:
    branches: [main]

# Manual dispatch (with optional inputs)
on:
  workflow_dispatch:
    inputs:
      environment:
        description: "Target environment"
        required: true
        type: choice
        options: [staging, production]

# Scheduled (cron)
on:
  schedule:
    - cron: "30 5 * * 1" # Every Monday at 05:30 UTC

# On tag creation
on:
  push:
    tags: ["v*.*.*"]
```

### 5.2 — Path and Branch Filtering

Use path filters to avoid running workflows for unrelated changes. `paths` and `paths-ignore` are mutually exclusive — use one or the other, never both in the same trigger:

```yaml
# Option A — allowlist (preferred): only run when these paths change
on:
    push:
        branches: [main]
        paths:
            - "pyproject.toml"
            - "src/**"
            - "tests/**"
```

```yaml
# Option B — denylist: run unless only these paths changed
on:
    push:
        branches: [main]
        paths-ignore:
            - "**.md"
            - "docs/**"
```

**Rule:** prefer `paths` (allowlist) over `paths-ignore` (denylist) for clarity.

---

## 6 — Permissions

**Always declare the minimum permissions the workflow needs.** The default `GITHUB_TOKEN` has broad permissions — restrict it.

```yaml
# Top-level: applies to all jobs unless overridden
permissions:
    contents: read

jobs:
    deploy:
        permissions:
            contents: read
            deployments: write # Only this job needs deployment write access
```

Available permission scopes include: `actions`, `contents`, `deployments`, `id-token`, `issues`, `packages`, `pull-requests`, `security-events`, `statuses`.

Set each to `read`, `write`, or omit it (no access). Prefer a `contents: read` baseline and grant specific write scopes only on the jobs that need them; `read-all` is a reasonable fallback when in doubt.

---

## 7 — Jobs

### 7.1 — Runner Selection

```yaml
jobs:
    build:
        runs-on: ubuntu-latest # Recommended default
```

Prefer `ubuntu-latest` unless you specifically need macOS or Windows. It is the fastest and cheapest runner.

### 7.2 — Job Dependencies

```yaml
jobs:
    lint:
        runs-on: ubuntu-latest
        steps: [...]

    test:
        runs-on: ubuntu-latest
        steps: [...]

    deploy:
        needs: [lint, test] # Runs only after both succeed
        runs-on: ubuntu-latest
        steps: [...]
```

### 7.3 — Matrix Strategies

Use matrices to run the same job across multiple configurations. For a C++/CMake project
like Luma, the most useful axis is the operating system (and, where relevant, the compiler):

```yaml
jobs:
    test:
        runs-on: ${{ matrix.os }}
        strategy:
            fail-fast: false # Don't cancel siblings on first failure
            matrix:
                os: [ubuntu-latest, macos-latest, windows-latest]
        steps:
            - uses: actions/checkout@v4
            - run: cmake --preset default
            - run: cmake --build --preset default
            - run: ctest --preset default --output-on-failure
```

**Refining the matrix with `include` and `exclude`.** Use `include` to add or
extend specific combinations (e.g., attach extra variables to one entry, or add a
one-off sanitizer build) and `exclude` to drop combinations that are not worth
running:

```yaml
strategy:
    fail-fast: false
    matrix:
        os: [ubuntu-latest, windows-latest]
        compiler: [gcc, clang]
        include:
            # Add an extra sanitizer build alongside the generated combinations.
            - os: ubuntu-latest
              compiler: gcc
              sanitizers: true
        exclude:
            # clang is not the focus on Windows here.
            - os: windows-latest
              compiler: clang
```

When the axes are irregular, an entirely `include`-based matrix (listing each
full combination explicitly) is often clearer than a combinatorial one — this is
the pattern Luma's own `ci.yml` uses for its per-compiler build jobs.

### 7.4 — Timeouts

Always set `timeout-minutes` to prevent hung jobs from burning runner minutes:

```yaml
jobs:
    build:
        runs-on: ubuntu-latest
        timeout-minutes: 15 # Kill the job if it runs longer
```

Default is 360 minutes (6 hours) — far too generous for most workflows.

---

## 8 — Steps

### 8.1 — Using Actions

**Pin every action to a full-length commit SHA** and add the version as a
trailing comment. A Git tag or branch is a *mutable* reference: anyone who gains
write access to an action's repository can move a tag like `@v4` onto a malicious
commit, which then runs with your workflow's permissions — a classic supply-chain
attack. A commit SHA is immutable and cannot be redirected, so every workflow in
this repository pins to one.

```yaml
# Best — pinned to an immutable SHA, with a human-readable version comment
- uses: actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd # v6.0.2

# Tolerable only for trusted first-party actions/* — the tag is still mutable
- uses: actions/checkout@v6

# Bad — fully mutable, can change under you without notice
- uses: actions/checkout@main
```

**Current project pins** (update these when bumping versions):

| Action | SHA Pin | Version |
| ------ | ------- | ------- |
| `actions/checkout` | `de0fac2e4500dabe0009e67214ff5f5447ce83dd` | v6.0.2 |
| `actions/upload-artifact` | `043fb46d1a93c77aae656e7c1c64a875d1fc6a0a` | v7.0.1 |
| `actions/download-artifact` | `3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c` | v8.0.1 |
| `actions/cache` | `27d5ce7f107fe9357f9df03efb73ab90386fccae` | v5.0.5 |
| `actions/setup-node` | `48b55a011bda9f5d6aeb4c2d9c7362e8dae4041e` | v6.4.0 |
| `actions/setup-python` | `a26af69be951a213d495a4c3e4e4022e16d87065` | v5.6.0 |
| `github/codeql-action` | `9e0d7b8d25671d64c341c19c0152d693099fb5ba` | v4.35.5 |
| `codecov/codecov-action` | `75cd11691c0faa626561e295848008c8a7dddffe` | v5.5.4 |
| `softprops/action-gh-release` | `b4309332981a82ec1c5618f44dd2e27cc8bfbfda` | v3.0.0 |
| `docker/setup-qemu-action` | `ce360397dd3f832beb865e1373c09c0e9f86d70a` | v4.0.0 |
| `dtolnay/rust-toolchain` | `3c5f7ea28cd621ae0bf5283f0e981fb97b8a7af9` | stable (1.94.1) |

SHA pins are kept current automatically: `.github/dependabot.yml` enables the
`github-actions` ecosystem, so Dependabot opens weekly PRs that bump each pin
(the SHA **and** its version comment) when a new release ships. Pinning to a SHA
and letting Dependabot update it gives you both immutability and freshness —
review each action's changelog before merging a major bump.

### 8.2 — Running Commands

```yaml
- name: Install dependencies
  run: npm ci

- name: Run multi-line script
  run: |
      echo "Building..."
      npm run build
      echo "Done."
```

### 8.3 — Conditional Steps

```yaml
- name: Deploy to production
  if: github.ref == 'refs/heads/main' && github.event_name == 'push'
  run: ./deploy.sh

- name: Upload coverage (even if tests fail)
  if: always()
  uses: actions/upload-artifact@v4
  with:
      name: coverage
      path: coverage/
```

Common conditions: `success()`, `failure()`, `always()`, `cancelled()`.

### 8.4 — Step Outputs

```yaml
- name: Get version
  id: version
  run: echo "tag=$(git describe --tags --abbrev=0)" >> "$GITHUB_OUTPUT"

- name: Use version
  run: echo "Deploying ${{ steps.version.outputs.tag }}"
```

**Never use `set-output` (deprecated).** Always write to `$GITHUB_OUTPUT`.

### 8.5 — Checkout Optimisation and Hardening

`actions/checkout` defaults are not always the safest or fastest. Two settings
are worth making explicit on every checkout:

```yaml
- uses: actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd # v6.0.2
  with:
      persist-credentials: false # Don't leave the token in .git/config
      fetch-depth: 1             # Shallow clone — only the commit being built
```

- **`persist-credentials: false`** stops checkout from writing the `GITHUB_TOKEN`
  into `.git/config`, where a later step (or a compromised dependency) could read
  it. Every checkout in this repository sets this.
- **`fetch-depth: 1`** (the default) fetches only the latest commit. Keep it
  shallow for build and test jobs. Use `fetch-depth: 0` **only** when you genuinely
  need full history — e.g., generating a changelog from tags (see §1.3 of the
  [recipes companion](github-actions-recipes.instructions.md)).
- **`submodules`** defaults to off; enable it only when a job actually needs
  submodules, since fetching them adds checkout time.

---

## 9 — Environment Variables and Secrets

### 9.1 — Environment Variables

```yaml
env: # Workflow-level
    NODE_ENV: production

jobs:
    build:
        env: # Job-level
            CI: true
        steps:
            - name: Print info
              env: # Step-level (narrowest scope — preferred)
                  DATABASE_URL: ${{ secrets.DATABASE_URL }}
              run: echo "Connected"
```

**Rule:** define variables at the narrowest scope possible.

### 9.2 — Secrets

Secrets are configured in the repository or organisation settings, then referenced via `${{ secrets.NAME }}`.

```yaml
- name: Deploy
  env:
      API_KEY: ${{ secrets.API_KEY }}
  run: ./deploy.sh
```

Security rules for secrets:

- **Never** echo or log secrets. GitHub masks them, but avoid relying on that.
- **Never** pass secrets as command-line arguments (they appear in process lists). Use environment variables instead.
- **Never** interpolate secrets directly in `run:` blocks with `${{ }}` — this risks script injection. Pass them through `env:` instead.

```yaml
# DANGEROUS — script injection risk
- run: curl -H "Authorization: ${{ secrets.TOKEN }}" https://api.example.com

# SAFE — passed via environment variable
- env:
    TOKEN: ${{ secrets.TOKEN }}
  run: curl -H "Authorization: $TOKEN" https://api.example.com
```

### 9.3 — OIDC for Cloud Providers

Avoid storing long-lived cloud credentials as secrets. Use OpenID Connect instead:

```yaml
permissions:
    id-token: write
    contents: read

steps:
    - uses: aws-actions/configure-aws-credentials@v4
      with:
          role-to-assume: arn:aws:iam::123456789012:role/GitHubActionsRole
          aws-region: us-east-1
```

This gives your workflow short-lived, scoped credentials with no stored secrets.

---

## 10 — Caching and Artifacts

### 10.1 — Dependency Caching

Most setup actions have built-in caching. Use it:

```yaml
- uses: actions/setup-node@v4
  with:
      node-version: 20
      cache: npm # Automatically caches ~/.npm
```

For custom caching:

```yaml
- uses: actions/cache@v4
  with:
      path: ~/.cache/pip
      key: pip-${{ runner.os }}-${{ hashFiles('**/requirements.txt') }}
      restore-keys: |
          pip-${{ runner.os }}-
```

### 10.2 — Artifacts

Share files between jobs or preserve build outputs:

```yaml
# Upload
- uses: actions/upload-artifact@v4
  with:
      name: build-output
      path: dist/
      retention-days: 7

# Download (in a dependent job)
- uses: actions/download-artifact@v4
  with:
      name: build-output
      path: dist/
```

---

## 11 — Reusable Workflows and Composite Actions

### 11.1 — Reusable Workflows

Extract shared CI logic into a workflow that other workflows can call:

```yaml
# .github/workflows/reusable-test.yml
name: Reusable Test Workflow

on:
    workflow_call:
        inputs:
            node-version:
                required: true
                type: string
        secrets:
            npm-token:
                required: false

jobs:
    test:
        runs-on: ubuntu-latest
        steps:
            - uses: actions/checkout@v4
            - uses: actions/setup-node@v4
              with:
                  node-version: ${{ inputs.node-version }}
            - run: npm ci
            - run: npm test
```

```yaml
# .github/workflows/ci.yml — caller
jobs:
    test:
        uses: ./.github/workflows/reusable-test.yml
        with:
            node-version: "20"
        secrets: inherit # Forward all secrets (or list them explicitly)
```

### 11.2 — Composite Actions

Bundle multiple steps into a single reusable action within your repo:

```yaml
# .github/actions/setup-project/action.yml
name: Setup Project
description: Checkout, install dependencies, and build

inputs:
    node-version:
        description: Node.js version
        default: "20"

runs:
    using: composite
    steps:
        - uses: actions/checkout@v4
        - uses: actions/setup-node@v4
          with:
              node-version: ${{ inputs.node-version }}
              cache: npm
        - run: npm ci
          shell: bash
        - run: npm run build
          shell: bash
```

```yaml
# Usage in a workflow
steps:
    - uses: ./.github/actions/setup-project
      with:
          node-version: "22"
```

**Rule:** always set `shell:` explicitly in composite action `run:` steps.

---

## 12 — Security Checklist

1. **Set `permissions` explicitly** — never rely on defaults.
2. **Pin actions to SHAs** — tags are mutable and can be overwritten.
3. **Never interpolate untrusted input in `run:`** — use `env:` to pass values safely.
4. **Use OIDC** for cloud authentication instead of long-lived secrets.
5. **Restrict `pull_request_target`** — it runs with write access; never checkout and execute PR code in this context.
6. **Limit `workflow_dispatch` and manual triggers** — apply branch protection and environment rules.
7. **Audit third-party actions** — prefer official (`actions/*`) or verified-creator actions.
8. **Set `timeout-minutes`** on every job.
9. **Enable `concurrency` with `cancel-in-progress`** — prevent redundant runs and resource waste.
10. **Disable credential persistence** — set `persist-credentials: false` on `actions/checkout` (see §8.5).
11. **Scan code and dependencies** — run SAST and dependency review in CI (see *Supply-Chain and Code Scanning* below).

### Script Injection Prevention

User-controlled inputs (issue titles, PR bodies, branch names) can inject arbitrary commands:

```yaml
# VULNERABLE — an attacker-controlled PR title is injected into the script
- run: echo "PR title is ${{ github.event.pull_request.title }}"

# SAFE — value is passed through an environment variable
- env:
      PR_TITLE: ${{ github.event.pull_request.title }}
  run: echo "PR title is $PR_TITLE"
```

### Supply-Chain and Code Scanning

Catch vulnerabilities before they ship by layering automated scanning into CI:

- **SAST (static analysis):** run CodeQL (see §1.5 of the
  [recipes companion](github-actions-recipes.instructions.md)) on pushes, pull requests, and
  a weekly schedule. Treat new high-severity alerts as blocking. This repository's
  `codeql.yml` analyses the C/C++ sources.
- **SCA (dependency review):** add `actions/dependency-review-action` on pull
  requests to flag dependencies with known CVEs or disallowed licences before they
  are merged.
- **Secret scanning:** enable GitHub's built-in secret scanning and push
  protection for the repository, and consider a pre-commit secret scanner so
  credentials are caught locally before they reach the remote.
- **Action updates:** let Dependabot (`.github/dependabot.yml`) keep pinned action
  SHAs current (see §8.1).

---

## 13 — Style and Maintenance Guidelines

1. **One concern per workflow file.** Separate CI, deployment, and release into distinct files.
2. **Name every step.** Descriptive `name:` fields make logs scannable.
3. **Use `>` or `|` for multiline values.** Avoid cramming logic into one line.
4. **Keep shell logic under ~15 lines.** If it grows beyond that, move it into a script file in the repo and call it from the step.
5. **Prefer built-in caching** (`cache:` option in setup actions) over manual `actions/cache` when available.
6. **Review action changelogs** before bumping major versions.
7. **Use `concurrency` groups** on every workflow to prevent stale runs from piling up.
8. **Document non-obvious choices** with YAML comments directly in the workflow.
9. **Use 2-space YAML indentation consistently.** One blank line between logical sections.
10. **Use `kebab-case`** for job IDs, step IDs, artifact names, and workflow file names. `UPPER_SNAKE_CASE` for environment variables and secrets.
11. **Share a repeated trigger filter with a YAML anchor.** When `push` and `pull_request` need the same `paths:` list, define it once on the first trigger with a `&paths` anchor and reference it from the second with `*paths`; the parser expands anchors before the workflow runs, so behaviour is identical while the list lives in one place. (Trade-off: the GitHub web editor renders anchors unexpanded.)

---

## 14 — Checklist Before Committing a Workflow

- [ ] `permissions` is set explicitly — not relying on defaults.
- [ ] Every job has `timeout-minutes`.
- [ ] `concurrency` with `cancel-in-progress` is configured.
- [ ] Actions are pinned to a full commit SHA (kept current by Dependabot).
- [ ] Secrets are passed via `env:`, never interpolated in `run:` blocks.
- [ ] Every step has a descriptive `name:`.
- [ ] Path filters are set on triggers to avoid unnecessary runs.
- [ ] Shell logic is under ~15 lines; longer scripts live in a file.
- [ ] Job IDs, step IDs, and artifact names use `kebab-case`.
- [ ] YAML uses 2-space indentation with blank lines between logical sections.
- [ ] `actions/checkout` sets `persist-credentials: false`; `fetch-depth` is minimal (`0` only when full history is needed).
- [ ] Security scanning (CodeQL/SAST, dependency review) runs for code changes.
