---
description: "Use when you need a complete, copy-paste GitHub Actions workflow file or workflow debugging guidance. A companion to github-actions.instructions.md that collects ready-to-adapt recipes: C++/CMake CI, Docker publish, release, deployment, CodeQL/SAST, and debugging techniques."
priority: reference
---

# GitHub Actions Recipes

A companion to [github-actions.instructions.md](github-actions.instructions.md) holding the
complete, copy-paste workflow files and debugging guidance that would otherwise bloat the
conventions guide. This file has **no** `applyTo` pattern — it is referenced manually when you need
a full example rather than a rule. The main guide states the conventions; apply them to everything
you adapt from here.

> **Note — examples are illustrative.** Some snippets use common ecosystem tooling (Docker, Node)
> to demonstrate a pattern generically. Luma's own CI builds C++ with CMake and runs CTest across
> an operating-system matrix — see §7.3 of the [main guide](github-actions.instructions.md#7--jobs)
> for the matrix pattern and §1.1 below for a complete C++/CMake workflow that mirrors this
> repository. Apply the *pattern*, not the specific tooling, when writing workflows for this project.

---

## Table of Contents

1. [Common Workflow Patterns](#1--common-workflow-patterns)
2. [Debugging](#2--debugging)

---

## 1 — Common Workflow Patterns

### 1.1 — C++ / CMake CI

```yaml
name: CI

on:
    push:
        branches: [main]
        paths:
            - "CMakeLists.txt"
            - "core/**"
            - "tests/**"
    pull_request:
        branches: [main]

permissions:
    contents: read

concurrency:
    group: ${{ github.workflow }}-${{ github.ref }}
    cancel-in-progress: true

jobs:
    build:
        name: ${{ matrix.os }}
        runs-on: ${{ matrix.os }}
        timeout-minutes: 15
        strategy:
            fail-fast: false
            matrix:
                os: [ubuntu-latest, macos-latest, windows-latest]
        steps:
            - uses: actions/checkout@v4

            - name: Configure
              run: cmake -B build -DCMAKE_BUILD_TYPE=Release

            - name: Build
              run: cmake --build build --config Release --parallel

            - name: Run tests
              run: ctest --test-dir build --output-on-failure -C Release
```

### 1.2 — Docker Build and Push

```yaml
name: Docker Publish

on:
    push:
        tags: ["v*.*.*"]

permissions:
    contents: read
    packages: write

jobs:
    publish:
        runs-on: ubuntu-latest
        timeout-minutes: 20
        steps:
            - uses: actions/checkout@v4

            - uses: docker/login-action@v3
              with:
                  registry: ghcr.io
                  username: ${{ github.actor }}
                  password: ${{ secrets.GITHUB_TOKEN }}

            - uses: docker/setup-buildx-action@v3

            - uses: docker/build-push-action@v6
              with:
                  context: .
                  push: true
                  tags: ghcr.io/${{ github.repository }}:${{ github.ref_name }}
                  cache-from: type=gha
                  cache-to: type=gha,mode=max
```

### 1.3 — Release with Changelog

```yaml
name: Release

on:
    push:
        tags: ["v*.*.*"]

permissions:
    contents: write

jobs:
    release:
        runs-on: ubuntu-latest
        timeout-minutes: 10
        steps:
            - uses: actions/checkout@v4
              with:
                  fetch-depth: 0

            - name: Generate changelog
              id: changelog
              run: |
                  prev_tag=$(git tag --sort=-v:refname | sed -n '2p')
                  changelog=$(git log "${prev_tag}..HEAD" --pretty=format:"- %s (%h)" --no-merges)
                  {
                    echo "body<<EOF"
                    echo "$changelog"
                    echo "EOF"
                  } >> "$GITHUB_OUTPUT"

            - uses: softprops/action-gh-release@v3
              with:
                  body: ${{ steps.changelog.outputs.body }}
                  generate_release_notes: true
```

### 1.4 — Deployment with Environments

```yaml
name: Deploy

on:
    push:
        branches: [main]

permissions:
    contents: read
    deployments: write

jobs:
    deploy:
        runs-on: ubuntu-latest
        timeout-minutes: 15
        environment:
            name: production # Requires approval rules in repo settings
            url: https://example.com
        steps:
            - uses: actions/checkout@v4
            - name: Deploy
              env:
                  DEPLOY_KEY: ${{ secrets.DEPLOY_KEY }}
              run: ./scripts/deploy.sh
```

### 1.5 — CodeQL / Static Analysis (SAST)

Scan source code for security vulnerabilities with CodeQL. For a compiled
language the analysis must observe a real build, so use `build-mode: manual` and
run the project's own build between the `init` and `analyze` steps:

```yaml
name: CodeQL

on:
    push:
        branches: [main]
    pull_request:
        branches: [main]
    schedule:
        - cron: "23 4 * * 1" # Weekly, so new advisories surface even without a push.

permissions:
    actions: read
    contents: read
    security-events: write # Required to upload results to the Security tab.

concurrency:
    group: ${{ github.workflow }}-${{ github.ref }}
    cancel-in-progress: true

jobs:
    analyze:
        name: Analyze (C++)
        runs-on: ubuntu-latest
        timeout-minutes: 30
        steps:
            - uses: actions/checkout@v4
              with:
                  persist-credentials: false

            - name: Initialize CodeQL
              uses: github/codeql-action/init@v4
              with:
                  languages: c-cpp
                  build-mode: manual

            - name: Build
              run: cmake --preset default && cmake --build --preset default

            - name: Perform CodeQL Analysis
              uses: github/codeql-action/analyze@v4
              with:
                  category: "/language:c-cpp"
```

For interpreted languages (the language server's TypeScript, helper Python) use
`build-mode: none` instead. Pair SAST with dependency review on pull requests for
supply-chain coverage:

```yaml
- name: Dependency review
  uses: actions/dependency-review-action@v4
  with:
      fail-on-severity: high
```

---

## 2 — Debugging

### 2.1 — Enable Debug Logging

Re-run any workflow with **"Enable debug logging"** checked, or set these repository secrets:

- `ACTIONS_RUNNER_DEBUG` = `true`
- `ACTIONS_STEP_DEBUG` = `true`

### 2.2 — Useful Diagnostic Steps

```yaml
- name: Dump context (debug only)
  if: runner.debug == '1'
  env:
      GITHUB_CONTEXT: ${{ toJSON(github) }}
  run: echo "$GITHUB_CONTEXT"

- name: List workspace
  if: runner.debug == '1'
  run: ls -alR "$GITHUB_WORKSPACE"
```

### 2.3 — Anti-Patterns

| Symptom                                  | Likely Cause                                    | Fix                                       |
| ---------------------------------------- | ----------------------------------------------- | ----------------------------------------- |
| "Context access might be invalid"        | Typo in expression (`steps.id.outputs.name`)    | Verify `id` values on prior steps         |
| "Resource not accessible by integration" | Missing or insufficient `permissions`           | Add the required permission scope         |
| Cache miss every run                     | Incorrect `hashFiles` glob                      | Test the glob locally; check `key` format |
| Job hangs indefinitely                   | No `timeout-minutes` set                        | Add a timeout to every job                |
| Secret is empty                          | Secret name mismatch or not set for environment | Verify secret name in settings            |
| Workflow never triggers                  | Wrong branch name in `on.push.branches`         | Check branch names match exactly          |
