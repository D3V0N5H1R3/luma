# `.github/` — GitHub Configuration and AI Agent Infrastructure

This directory holds everything that GitHub, Dependabot, and AI-powered agents (Copilot, Claude Code) need to operate on the Luma repository. It is split into subdirectories for distinct concerns plus a handful of root-level configuration files consumed directly by GitHub or the CI system.

## Directory Structure

| Directory                        | Purpose                                                                                          |
| -------------------------------- | ------------------------------------------------------------------------------------------------ |
| [`actions/`](actions/)           | Reusable composite actions shared across workflows (build helpers, packaging).                    |
| [`agents/`](agents/)             | Copilot agent definitions — role-specific personas (`plan`, `implement`, `review`, `docs`, `test`). |
| [`codeql/`](codeql/)             | CodeQL configuration for the security analysis workflow.                                         |
| [`hooks/`](hooks/)               | AI agent hooks — deterministic guardrails run at tool-call lifecycle points.                      |
| [`ISSUE_TEMPLATE/`](ISSUE_TEMPLATE/) | GitHub issue form templates (bug report, feature request) and chooser config.                |
| [`prompts/`](prompts/)           | Copilot prompt files — reusable workflow instructions for AI agents (27 prompts).                 |
| [`workflows/`](workflows/)       | GitHub Actions workflow definitions (CI, linting, release, fuzzing, etc.).                        |

Each subdirectory carries its own `README.md` with detailed documentation.

## Root-Level Files

### [`copilot-instructions.md`](copilot-instructions.md)

The **single source of truth** for AI agent project context. Loaded automatically by VS Code Copilot, the Copilot coding agent, and Zed's agent. Contains the full architecture overview, coding conventions, build instructions, and language reference that agents need to work on the codebase. Claude Code imports this file via its own `CLAUDE.md` at the repository root.

### [`CODEOWNERS`](CODEOWNERS)

Defines code ownership for automated review requests. Auto-assigns the maintainer (`@D3V0N5H1R3`) to PRs touching owned paths. Vendored third-party code under `external/` is intentionally unowned (no review gate), while the first-party `external/gui-framework/` is owned.

### [`PULL_REQUEST_TEMPLATE.md`](PULL_REQUEST_TEMPLATE.md)

Default PR description template. Includes sections for summary, related issues, change type (mapped to Conventional Commit prefixes), testing details, and a pre-merge checklist covering builds, tests, formatting, linting, and documentation.

### [`dependabot.yml`](dependabot.yml)

Dependabot configuration for automated dependency updates:

- **GitHub Actions** — weekly updates for workflow action versions.
- **npm** — weekly updates for VS Code extension, test tooling, and Tree-sitter grammar dependencies.
- **Cargo** — weekly updates for the Zed extension and its Tree-sitter grammar.

All ecosystems use grouped PRs (one PR per ecosystem batch) to reduce noise.

### [`linux-distros.json`](workflows/linux-distros.json)

Matrix definition consumed by the CI multi-distro build job. Lists container images (Debian trixie, Kali, Fedora, Arch Linux) with their compiler and install commands. The workflow reads this file dynamically so adding a distro requires no workflow edits. Lives inside `workflows/` because it is exclusively a workflow implementation detail.

### [`plan-template.md`](plan-template.md)

Markdown template for implementation plans. Used by the `plan` agent and prompt to produce structured design documents with consistent sections: summary, motivation, scope, architecture, affected files, tasks, testing strategy, risks, and definition of done.

### [`stale-path-denylist.txt`](stale-path-denylist.txt)

Regex patterns for source paths that have been removed or renamed. The Docs Consistency workflow (`docs.yml`) fails if any of these appear in documentation or prompt files — catching stale references that outlived the thing they pointed at. Grows incrementally as paths are deleted or renamed.
