---
description: "Use when writing, reviewing, or modifying Git commits, branches, merges, or repository operations. Covers commit message conventions, branch naming, merge strategy, and safe Git workflows."
---

# Working with Git

A reference for working with Git safely, simply, and effectively in an automated or assisted context. This guide has no `applyTo` file pattern because Git operations are not tied to a specific file type — consult it whenever you create commits, branches, tags, or merges, or resolve conflicts.

---

## Table of Contents

1. [First Principles](#1--first-principles)
2. [Initial Setup](#2--initial-setup)
3. [Reading Repository State](#3--reading-repository-state)
4. [Making Changes](#4--making-changes)
5. [Branching](#5--branching)
6. [Merging](#6--merging)
7. [Conflict Resolution](#7--conflict-resolution)
8. [Working with Remotes](#8--working-with-remotes)
9. [Undoing Changes Safely](#9--undoing-changes-safely)
10. [Stashing](#10--stashing)
11. [Tags](#11--tags)
12. [Inspecting and Debugging](#12--inspecting-and-debugging)
13. [Helpful Aliases](#13--helpful-aliases)
14. [Pre-Operation Safety Checklist](#14--pre-operation-safety-checklist)
15. [Common Workflows](#15--common-workflows)
16. [Things to Avoid](#16--things-to-avoid)

---

## 1 — First Principles

- **Never force-push to shared branches.** Treat `main`, `master`, `develop`, and any branch others may pull from as protected.
- **Always verify before destructive operations.** Confirm the current branch and status before resetting, rebasing, or deleting anything.
- **Prefer explicit over implicit.** Use full flag names (`--no-ff`, `--set-upstream`) for clarity and auditability.
- **Fail early, fail loudly.** Check exit codes and surface errors immediately rather than proceeding silently.

---

## 2 — Initial Setup

### 2.1 Configure Identity

Every commit needs an author. Set these before doing any work:

```bash
git config user.name "Your Name"
git config user.email "your@email.com"
```

Use `--global` only when appropriate for the environment. In ephemeral containers, repo-level config is sufficient.

### 2.2 Verify the Environment

Before running any Git operation, confirm the tool is available and the repo exists:

```bash
command -v git >/dev/null 2>&1 || { echo "Git is not installed."; exit 1; }
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || { echo "Not inside a Git repository."; exit 1; }
```

---

## 3 — Reading Repository State

Always understand the current state before making changes.

### 3.1 Status and Branch

```bash
# Current branch name
git branch --show-current

# Working tree status (short form for quick parsing)
git status --short

# Full status for human-readable context
git status
```

### 3.2 Log and History

```bash
# Compact log — last 20 commits, graph view
git log --oneline --graph --decorate -20

# Log for a specific file
git log --oneline --follow -- path/to/file

# Show a specific commit's changes
git show <commit-hash> --stat
```

### 3.3 Diff

```bash
# Unstaged changes
git diff

# Staged changes
git diff --cached

# Diff between two branches
git diff main..feature-branch --stat
```

---

## 4 — Making Changes

### 4.1 Stage and Commit

```bash
# Stage specific files (preferred over `git add .`)
git add path/to/file1 path/to/file2

# Stage all tracked, modified files (skip untracked)
git add --update

# Commit with a clear, conventional message
git commit -m "fix: resolve null pointer in user lookup"
```

**Commit message conventions** — use a short prefix to categorise the change:

| Prefix      | Purpose                                 |
| ----------- | --------------------------------------- |
| `chore:`    | Tooling, config, dependencies           |
| `docs:`     | Documentation only                      |
| `feat:`     | New feature                             |
| `fix:`      | Bug fix                                 |
| `refactor:` | Code restructuring, no behaviour change |
| `test:`     | Adding or updating tests                |

### 4.2 Amending the Last Commit

Only amend commits that have **not** been pushed:

```bash
git add path/to/corrected-file
git commit --amend --no-edit
```

---

## 5 — Branching

### 5.1 Create and Switch

```bash
# Create a new branch from the current HEAD and switch to it
git switch -c feature/short-description

# Switch to an existing branch
git switch main
```

`git switch` is the modern replacement for `git checkout` when changing branches. Prefer it for clarity.

### 5.2 List and Clean Up

```bash
# List local branches
git branch

# Delete a fully merged local branch
git branch -d feature/done

# Delete an unmerged branch (use with caution — data loss)
git branch -D feature/abandoned
```

### 5.3 Naming Conventions

Use lowercase, hyphen-separated names with a category prefix:

- `chore/upgrade-dependencies`
- `docs/update-readme`
- `feature/add-user-search`
- `fix/cart-total-rounding`
- `refactor/simplify-parser`
- `test/add-lexer-tests`

---

## 6 — Merging

### 6.1 Standard Merge (No Fast-Forward)

Preserving merge commits keeps the branch history readable:

```bash
git switch main
git merge --no-ff feature/add-user-search
```

### 6.2 Fast-Forward Merge

Appropriate for trivial or single-commit branches:

```bash
git switch main
git merge feature/typo-fix
```

### 6.3 Abort a Merge in Progress

If conflicts arise and you need to start over:

```bash
git merge --abort
```

---

## 7 — Conflict Resolution

### 7.1 Identify Conflicts

```bash
# List conflicting files
git diff --name-only --diff-filter=U
```

### 7.2 Resolve Manually

1. Open each conflicting file.
2. Look for conflict markers (`<<<<<<<`, `=======`, `>>>>>>>`).
3. Edit the file to keep the correct content and remove all markers.
4. Stage the resolved file:

    ```bash
    git add path/to/resolved-file
    ```

5. Complete the merge:

    ```bash
    git commit
    ```

### 7.3 Safety Check After Resolution

Verify no conflict markers remain anywhere in the tree:

```bash
grep -rn "<<<<<<< \|=======\|>>>>>>> " . --exclude-dir=.git && echo "CONFLICT MARKERS FOUND" || echo "Clean"
```

---

## 8 — Working with Remotes

### 8.1 Fetch and Pull

```bash
# Fetch updates without modifying the working tree
git fetch origin

# Pull with rebase to keep a linear history (preferred)
git pull --rebase origin main

# Standard pull (creates merge commits)
git pull origin main
```

### 8.2 Push

```bash
# Push the current branch and set upstream tracking
git push --set-upstream origin feature/my-branch

# Subsequent pushes
git push
```

### 8.3 Rules for Force-Push

Force-pushing rewrites remote history. Apply these rules strictly:

- **Allowed:** On your own feature branch that no one else is using.
- **Never:** On `main`, `master`, `develop`, or any shared/protected branch.
- When force-pushing is necessary, use the safer variant:

```bash
git push --force-with-lease
```

`--force-with-lease` refuses to push if someone else has pushed to the branch since your last fetch, preventing accidental overwrites.

---

## 9 — Undoing Changes Safely

### 9.1 Discard Unstaged Changes to a File

```bash
git restore path/to/file
```

### 9.2 Unstage a File (Keep Changes in Working Tree)

```bash
git restore --staged path/to/file
```

### 9.3 Revert a Commit (Safe — Creates a New Commit)

```bash
git revert <commit-hash> --no-edit
```

This is the **preferred** way to undo a change on a shared branch because it does not rewrite history.

### 9.4 Reset (Destructive — Local Only)

Only use on commits that have **not** been pushed:

```bash
# Move HEAD back, keep changes staged
git reset --soft HEAD~1

# Move HEAD back, keep changes in working tree
git reset --mixed HEAD~1

# Discard everything (DANGER — unrecoverable without reflog)
git reset --hard HEAD~1
```

---

## 10 — Stashing

Temporarily shelve work without committing:

```bash
# Stash current changes (tracked files)
git stash push -m "wip: halfway through refactor"

# List stashes
git stash list

# Restore the most recent stash and remove it from the stack
git stash pop

# Restore without removing from the stack
git stash apply
```

---

## 11 — Tags

### 11.1 Annotated Tags (Preferred)

```bash
git tag -a v1.2.0 -m "Release version 1.2.0"
```

### 11.2 Push Tags

```bash
# Push a single tag
git push origin v1.2.0

# Push all tags
git push origin --tags
```

---

## 12 — Inspecting and Debugging

```bash
# Who last modified each line of a file
git blame path/to/file

# Binary search for the commit that introduced a bug
git bisect start
git bisect bad # Current commit is broken
git bisect good <known-good-hash>

# Git will checkout commits for you to test; mark each as good/bad

git bisect reset # Return to original state when done
```

---

## 13 — Helpful Aliases

These can be set per-repo or globally to speed up common operations:

```bash
git config alias.st "status --short"
git config alias.lg "log --oneline --graph --decorate -20"
git config alias.co "switch"
git config alias.cb "switch -c"
git config alias.unstage "restore --staged"
```

---

## 14 — Pre-Operation Safety Checklist

Run through this checklist before any significant Git operation:

1. **Am I on the correct branch?** → `git branch --show-current`
2. **Is my working tree clean?** → `git status --short`
3. **Have I fetched the latest remote state?** → `git fetch origin`
4. **Am I about to modify shared history?** → If yes, stop and reconsider.
5. **Do I have a way to recover if this goes wrong?** → Note the current commit hash: `git rev-parse HEAD`

---

## 15 — Common Workflows

### 15.1 Feature Branch Workflow

```bash
git switch main
git pull --rebase origin main
git switch -c feature/my-feature

# ... make changes ...

git add --update
git commit -m "feat: implement my feature"
git push --set-upstream origin feature/my-feature

# Open a pull request / merge request via your platform
```

### 15.2 Hotfix Workflow

```bash
git switch main
git pull --rebase origin main
git switch -c fix/critical-bug

# ... apply fix ...

git add --update
git commit -m "fix: patch critical bug in auth flow"
git push --set-upstream origin fix/critical-bug

# Merge immediately after review
```

### 15.3 Sync a Feature Branch with Main

```bash
git switch feature/my-feature
git fetch origin
git rebase origin/main

# Resolve any conflicts, then:
git push --force-with-lease
```

---

## 16 — Things to Avoid

| Anti-Pattern                             | Why                                                | Do This Instead                                                    |
| ---------------------------------------- | -------------------------------------------------- | ------------------------------------------------------------------ |
| `git add .` in a large repo              | Stages unintended files (build artifacts, secrets) | `git add --update` or name files explicitly                        |
| `git push --force` on shared branches    | Destroys others' work                              | `git revert` to undo, or `--force-with-lease` on personal branches |
| Committing secrets or credentials        | Extremely difficult to fully remove from history   | Use `.gitignore` and environment variables                         |
| Giant, multi-purpose commits             | Hard to review, revert, or bisect                  | One logical change per commit                                      |
| Vague commit messages like `"fix stuff"` | Useless for future debugging                       | Use conventional commit format with context                        |
