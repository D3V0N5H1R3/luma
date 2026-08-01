---
description: "Use when writing, reviewing, or modifying shell scripts (.sh, .bash files). Covers portability, naming, style, error handling, quoting, and safe scripting patterns for Linux and macOS."
applyTo: "**/*.{sh,bash}"
priority: reference
---

# Working with Shell Scripts

These instructions govern how you write shell scripts. Every script you produce must follow these principles. They are aligned with the [POSIX Shell Command Language](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html), the [Bash Reference Manual](https://www.gnu.org/software/bash/manual/), and the [ShellCheck](https://www.shellcheck.net/) linting tool. Scripts must run correctly on both Linux (Ubuntu, Debian, Raspberry Pi OS, Kali Linux, Fedora, Arch Linux) and macOS.

---

## Table of Contents

1. [Simplicity First](#1--simplicity-first)
2. [Portability](#2--portability)
3. [Naming Conventions](#3--naming-conventions)
4. [Consistent Style](#4--consistent-style)
5. [Shebang and Header](#5--shebang-and-header)
6. [Variables and Quoting](#6--variables-and-quoting)
7. [Functions](#7--functions)
8. [Error Handling](#8--error-handling)
9. [Conditionals and Control Flow](#9--conditionals-and-control-flow)
10. [Command Substitution and Pipes](#10--command-substitution-and-pipes)
11. [File and Path Handling](#11--file-and-path-handling)
12. [Input Validation and Arguments](#12--input-validation-and-arguments)
13. [Process Management](#13--process-management)
14. [Temporary Files and Cleanup](#14--temporary-files-and-cleanup)
15. [Self-Documenting Code](#15--self-documenting-code)
16. [Whitespace as Structure](#16--whitespace-as-structure)
17. [Security Essentials](#17--security-essentials)
18. [Testing](#18--testing)
19. [Performance](#19--performance)
20. [Anti-Patterns](#20--anti-patterns)
21. [Checklist](#21--checklist)

---

## 1 — Simplicity First

Write the simplest script that solves the problem correctly.

- Prefer straightforward control flow over clever one-liners.
- If a task is complex enough to need arrays, associative arrays, or intricate string manipulation, consider using Python instead.
- Use standard utilities (`grep`, `sed`, `awk`, `find`, `sort`, `cut`, `tr`) idiomatically.
- A developer unfamiliar with the script should understand what it does within minutes.

**Test:** Before committing to an approach, ask — _is there a simpler way? Should this be a shell script at all?_

---

## 2 — Portability

Scripts must run on both Linux and macOS without modification.

- Use `#!/usr/bin/env bash` for Bash scripts. Use `#!/bin/sh` only for strictly POSIX-compliant scripts.
- Avoid Bash-only features in `#!/bin/sh` scripts. If you need Bash features, use `#!/usr/bin/env bash`.
- Avoid GNU-only flags for common utilities. macOS ships BSD variants of `sed`, `grep`, `date`, `find`, `readlink`, and others.
- Use `command -v` instead of `which` for checking command availability.
- Use `$(command)` instead of backticks for command substitution.
- Use `printf` instead of `echo -e` or `echo -n` for portable output with escape sequences.
- Avoid `readlink -f` (GNU-only). Use a portable alternative or require `realpath`.
- Test scripts on both Linux and macOS before committing.

```bash
# Good — portable command check.
if ! command -v jq &>/dev/null; then
    printf "Error: jq is required but not installed.\n" >&2
    exit 1
fi

# Bad — not portable.
if ! which jq >/dev/null 2>&1; then
    echo -e "Error: jq is required.\n" >&2
    exit 1
fi

# Good — portable path resolution.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Bad — GNU-only.
script_dir="$(readlink -f "$(dirname "$0")")"
```

### Common Linux vs macOS Differences

| Utility    | Linux (GNU)              | macOS (BSD)              | Portable Alternative                     |
| ---------- | ------------------------ | ------------------------ | ---------------------------------------- |
| `sed -i`   | `sed -i 's/a/b/'`       | `sed -i '' 's/a/b/'`    | Write to temp file and `mv`              |
| `readlink` | `readlink -f`            | No `-f` flag             | `cd "$(dirname "$0")" && pwd`            |
| `date`     | `date -d '1 day ago'`   | `date -v-1d`             | Use Python for complex date math         |
| `grep -P`  | Perl-compatible regex    | Not available            | Use `grep -E` (extended regex)           |
| `find`     | `-printf`, `-regextype` | Not available            | Use `-print` and pipe to `awk`           |
| `mktemp`   | `mktemp`                | `mktemp` (needs `XXXX`) | `mktemp /tmp/prefix.XXXXXX`             |
| `stat`     | `stat -c '%s'`          | `stat -f '%z'`           | Use `wc -c < file`                       |

---

## 3 — Naming Conventions

| Entity             | Convention     | Examples                                         |
| ------------------ | -------------- | ------------------------------------------------ |
| Variables          | `snake_case`   | `total_count`, `output_dir`, `is_verbose`        |
| Functions          | `snake_case`   | `build_project`, `check_dependencies`            |
| Constants          | `UPPER_CASE`   | `MAX_RETRIES`, `DEFAULT_TIMEOUT`, `VERSION`      |
| Environment vars   | `UPPER_CASE`   | `LUMA_HOME`, `BUILD_DIR`                         |
| File names         | `kebab-case`   | `run-tests.sh`, `build-release.sh`               |
| Boolean variables  | question       | `is_dry_run`, `has_errors`, `should_clean`       |
| Local variables    | `local` prefix | Always declare with `local` inside functions     |

- Name what the value represents, not its type. `output_dir` — good. `d` — bad.
- Avoid abbreviations unless universally understood (`tmp`, `dir`, `pid`, `fd`).
- Prefix boolean variables with `is_`, `has_`, `should_`, `can_`, or `will_`.

---

## 4 — Consistent Style

- **Indentation:** 4 spaces. No tabs.
- **Line length:** 100 characters maximum. Use `\` for line continuation at logical boundaries.
- **Braces:** Opening brace on the same line as `if`, `for`, `while`, `function`. `then` and `do` on the same line as the keyword.
- **Semicolons:** Use semicolons to place `then`/`do` on the same line: `if ...; then`, `for ...; do`.
- **Quoting:** Double-quote all variable expansions unless word splitting is intentionally desired.
- **Keywords:** Use `[[` instead of `[` for conditionals in Bash scripts.
- **Alignment:** Do not use extra spaces for alignment. Let consistent indentation handle grouping.

```bash
# Good — consistent style.
build_project() {
    local build_dir="$1"
    local config="${2:-Release}"

    if [[ ! -d "$build_dir" ]]; then
        mkdir -p "$build_dir"
    fi

    cmake -B "$build_dir" -DCMAKE_BUILD_TYPE="$config"
    cmake --build "$build_dir" --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
}
```

---

## 5 — Shebang and Header

Every script must start with a shebang line and a brief description.

- Use `#!/usr/bin/env bash` for Bash scripts (portable across systems).
- Use `#!/bin/sh` only for scripts that are strictly POSIX-compliant.
- Add `set -euo pipefail` immediately after the shebang for robust error handling.
- Include a brief comment describing the script's purpose.

```bash
#!/usr/bin/env bash
# Build the Luma interpreter and run all tests.

set -euo pipefail
```

### `set` Options

| Option       | Effect                                                         |
| ------------ | -------------------------------------------------------------- |
| `set -e`     | Exit immediately on command failure (non-zero exit code)       |
| `set -u`     | Treat unset variables as errors                                |
| `set -o pipefail` | Propagate failure from any command in a pipeline         |

---

## 6 — Variables and Quoting

Quoting is the single most important habit in shell scripting. Unquoted variables cause word splitting and glob expansion bugs.

- **Always double-quote** variable expansions: `"$var"`, `"${var}"`, `"$(command)"`.
- Use `${var:-default}` for default values. Use `${var:?error message}` for required variables.
- Declare local variables with `local` inside functions.
- Use `readonly` for constants that must not change.
- Avoid `eval`. If you think you need `eval`, you almost certainly do not.

```bash
# Good — quoted expansions, defaults, readonly.
readonly VERSION="1.0.0"
readonly BUILD_DIR="${BUILD_DIR:-build}"

process_file() {
    local file_path="$1"
    local output_dir="${2:-output}"

    if [[ ! -f "$file_path" ]]; then
        printf "Error: file not found: %s\n" "$file_path" >&2
        return 1
    fi

    cp -- "$file_path" "$output_dir/"
}

# Bad — unquoted, no local, no readonly.
VERSION=1.0.0
BUILD_DIR=$BUILD_DIR

process_file() {
    file_path=$1
    if [ ! -f $file_path ]; then
        echo "Error: file not found: $file_path" >&2
        return 1
    fi
    cp $file_path $output_dir/
}
```

---

## 7 — Functions

- Keep functions small — one logical operation per function.
- Declare all function variables with `local`.
- Use `return` for status codes (0 = success, non-zero = failure). Use `printf` to stdout for data output.
- Avoid subshells for functions that set variables the caller needs.
- Name functions with a verb-phrase: `check_dependencies`, `build_project`, `run_tests`.
- Define functions before they are called.

```bash
# Good — local variables, early return, clear output.
find_executable() {
    local name="$1"
    local search_paths=("$HOME/.local/bin" "/usr/local/bin" "/usr/bin")

    for dir in "${search_paths[@]}"; do
        if [[ -x "$dir/$name" ]]; then
            printf "%s" "$dir/$name"
            return 0
        fi
    done

    return 1
}

# Usage.
if exe_path="$(find_executable "luma")"; then
    printf "Found: %s\n" "$exe_path"
else
    printf "Not found.\n" >&2
    exit 1
fi
```

---

## 8 — Error Handling

- Use `set -euo pipefail` at the top of every script.
- Check return codes of critical commands explicitly when `set -e` is insufficient.
- Use `trap` for cleanup on exit, error, or signal.
- Write error messages to stderr (`>&2`).
- Use meaningful exit codes: 0 for success, 1 for general errors, 2 for usage errors.
- Use `|| true` only when a command's failure is genuinely acceptable.

```bash
#!/usr/bin/env bash
set -euo pipefail

cleanup() {
    local exit_code=$?

    if [[ -n "${tmp_dir:-}" ]]; then
        rm -rf "$tmp_dir"
    fi

    exit "$exit_code"
}

trap cleanup EXIT

tmp_dir="$(mktemp -d /tmp/build.XXXXXX)"

# Critical command — check explicitly.
if ! cmake --build build --config Release; then
    printf "Error: build failed.\n" >&2
    exit 1
fi
```

---

## 9 — Conditionals and Control Flow

- Use `[[ ]]` instead of `[ ]` in Bash scripts — it handles quoting, pattern matching, and logical operators more safely.
- Use `(( ))` for arithmetic comparisons.
- Use `case` for multi-branch string matching instead of long `if`/`elif` chains.
- Prefer early exits and guard clauses over deep nesting.
- Use `&&` and `||` for simple conditional execution. Avoid `||` for complex side effects.

```bash
# Good — [[ ]] with pattern matching.
if [[ "$filename" == *.cpp ]]; then
    compile_cpp "$filename"
fi

# Good — arithmetic comparison.
if (( retry_count >= MAX_RETRIES )); then
    printf "Error: max retries exceeded.\n" >&2
    exit 1
fi

# Good — case for multi-branch.
case "$command" in
    build)
        build_project "$@"
        ;;
    test)
        run_tests "$@"
        ;;
    clean)
        clean_artifacts
        ;;
    *)
        printf "Unknown command: %s\n" "$command" >&2
        usage
        exit 2
        ;;
esac
```

---

## 10 — Command Substitution and Pipes

- Use `$(command)` for command substitution. Never use backticks.
- Quote command substitutions: `"$(command)"`.
- Avoid unnecessary subshells. Use process substitution (`<(command)`) or here-strings when appropriate.
- Avoid `cat file | grep` — use `grep pattern file` directly (UUOC — Useless Use of Cat).
- Keep pipelines readable. Break long pipelines across multiple lines with `\` or pipe at the end of each line.

```bash
# Good — no useless cat, quoted substitution.
match_count="$(grep -c "error" "$log_file")"

# Good — readable multi-line pipeline.
find "$src_dir" -name "*.cpp" -type f \
    | sort \
    | while IFS= read -r file; do
        process_file "$file"
    done

# Bad — useless cat, unquoted substitution.
match_count=`cat $log_file | grep -c error`
```

---

## 11 — File and Path Handling

- Always quote file paths — they may contain spaces or special characters.
- Use `--` to separate options from filenames: `rm -- "$file"`, `cp -- "$src" "$dst"`.
- Use `mkdir -p` for creating directory hierarchies.
- Use `mktemp` for temporary files and directories.
- Prefer `[[ -f "$file" ]]` over `test -f "$file"` in Bash.
- Use `find` with `-print0` and `xargs -0` or `while IFS= read -r -d ''` for filenames with special characters.

```bash
# Good — safe file handling with special characters.
find "$dir" -name "*.luma" -print0 \
    | while IFS= read -r -d '' file; do
        printf "Processing: %s\n" "$file"
        luma --test "$file"
    done

# Good — resolve script directory portably.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"
```

---

## 12 — Input Validation and Arguments

- Validate all required arguments early.
- Provide a `usage()` function for scripts that accept arguments.
- Use `getopts` for option parsing. Use a `while`/`case` loop for long options.
- Default optional arguments with `${var:-default}`.
- Exit with code 2 for usage errors.

```bash
usage() {
    printf "Usage: %s [-v] [-o output_dir] <input_file>\n" "$(basename "$0")"
    printf "\nOptions:\n"
    printf "  -v              Enable verbose output\n"
    printf "  -o output_dir   Set output directory (default: build)\n"
    printf "  -h              Show this help message\n"
}

is_verbose=false
output_dir="build"

while getopts ":vo:h" opt; do
    case "$opt" in
        v) is_verbose=true ;;
        o) output_dir="$OPTARG" ;;
        h) usage; exit 0 ;;
        :) printf "Error: -%s requires an argument.\n" "$OPTARG" >&2; exit 2 ;;
        *) printf "Error: unknown option -%s.\n" "$OPTARG" >&2; usage; exit 2 ;;
    esac
done

shift $((OPTIND - 1))

if [[ $# -lt 1 ]]; then
    printf "Error: input file required.\n" >&2
    usage
    exit 2
fi

input_file="$1"
```

---

## 13 — Process Management

- Use `wait` to collect background process exit codes.
- Kill background processes in cleanup traps.
- Use `exec` only for replacing the current process with a final command.
- Avoid `nohup` in scripts — use proper process management if persistence is needed.

```bash
# Good — background process with cleanup.
cleanup() {
    if [[ -n "${server_pid:-}" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
}

trap cleanup EXIT

start_server &
server_pid=$!

# Wait for server to be ready.
for i in $(seq 1 30); do
    if curl -sf http://localhost:8080/health &>/dev/null; then
        break
    fi

    sleep 1
done

run_tests
```

---

## 14 — Temporary Files and Cleanup

- Always use `mktemp` for temporary files and directories. Never hardcode `/tmp/myfile`.
- Always register a `trap` to clean up temporary files on exit.
- Use `/tmp/prefix.XXXXXX` pattern for portability (macOS requires at least 3 `X`s).

```bash
tmp_file="$(mktemp /tmp/luma-build.XXXXXX)"
tmp_dir="$(mktemp -d /tmp/luma-test.XXXXXX)"

cleanup() {
    rm -f "$tmp_file"
    rm -rf "$tmp_dir"
}

trap cleanup EXIT
```

---

## 15 — Self-Documenting Code

Write scripts that explain themselves. Reserve comments for _why_, not _what_.

- Use descriptive variable and function names so the script reads naturally.
- Add a header comment explaining the script's purpose, usage, and any prerequisites.
- Delete stale or redundant comments. A wrong comment is worse than no comment.
- Use `usage()` functions as built-in documentation.

```bash
# Bad — restates the code.
# Create the build directory.
mkdir -p build

# Good — explains a non-obvious constraint.
# macOS find does not support -printf, so we use -print and awk instead.
find "$src_dir" -name "*.cpp" -print | awk -F/ '{print $NF}'
```

---

## 16 — Whitespace as Structure

Use blank lines to reveal logical structure — like paragraphs in prose.

- **One blank line** between function definitions.
- **One blank line** between logical blocks within a function (setup, processing, result).
- **No** multiple consecutive blank lines.
- **No** trailing whitespace. One trailing newline at end of file.
- Align nothing with extra spaces. Let consistent indentation handle grouping.

---

## 17 — Security Essentials

- Never use `eval` with user-provided input — this is code injection.
- Never use unquoted variables in commands — this causes word splitting and glob expansion.
- Validate and sanitise all external input before use.
- Avoid `curl | bash` patterns. Download, verify, then execute.
- Use `--` to prevent option injection: `rm -- "$user_input"`.
- Do not store secrets in scripts. Use environment variables, credential files, or secret managers.
- Set restrictive `umask` (e.g., `umask 077`) before creating sensitive files.
- Avoid creating world-writable or world-readable files.

```bash
# Good — safe argument handling.
rm -- "$user_provided_file"

# Bad — option injection if $file starts with -.
rm $file

# Good — validate input.
if [[ "$input" =~ ^[a-zA-Z0-9_-]+$ ]]; then
    process "$input"
else
    printf "Error: invalid input: %s\n" "$input" >&2
    exit 1
fi

# Bad — eval with external input.
eval "$user_command"
```

---

## 18 — Testing

- Test scripts on both Linux and macOS before committing.
- Use `shellcheck` to lint all shell scripts. Fix all warnings.
- Test edge cases: empty arguments, filenames with spaces, missing dependencies.
- Use `set -x` (or `bash -x script.sh`) for debugging.
- Write integration tests that exercise the script's main paths.
- Test with `bash --posix` if the script claims POSIX compliance.

```bash
# Run shellcheck on all scripts.
find scripts/ -name "*.sh" -exec shellcheck {} +

# Debug a specific script.
bash -x scripts/run-tests.sh
```

---

## 19 — Performance

- Avoid spawning subshells and external processes in tight loops. Use Bash built-ins where possible.
- Use `read` with `IFS=` for parsing instead of repeated `cut`/`awk` calls in loops.
- Prefer `printf` over `echo` — it is faster and more portable.
- Use `xargs -P` for parallel execution of independent tasks.
- For complex data processing, use `awk` in a single pass rather than chaining `grep | sed | cut`.
- If a script exceeds ~200 lines or needs complex data structures, consider rewriting in Python.

```bash
# Good — single awk pass instead of grep | cut | sort.
awk -F: '/^user_/ {print $2}' "$config_file" | sort

# Bad — multiple processes for simple extraction.
grep "^user_" "$config_file" | cut -d: -f2 | sort
```

---

## 20 — Anti-Patterns

- **Unquoted variables.** Always double-quote `"$var"` and `"$(command)"`.
- **`eval` with external input.** This is code injection. Never use `eval` on untrusted data.
- **Backticks for command substitution.** Use `$(command)` instead.
- **`[ ]` in Bash scripts.** Use `[[ ]]` for safer conditionals.
- **Parsing `ls` output.** Use `find` or glob patterns instead.
- **`cat file | grep`.** Use `grep pattern file` directly.
- **Hardcoded `/tmp` paths.** Use `mktemp` and clean up with `trap`.
- **Missing `set -euo pipefail`.** Always set this at the top of scripts.
- **`echo -e` or `echo -n`.** Use `printf` for portable formatted output.
- **GNU-only flags.** Avoid `sed -i`, `readlink -f`, `grep -P` without portability guards.
- **Global variables in functions.** Always declare with `local`.
- **Missing `--` separator.** Use `rm -- "$file"` to prevent option injection.

---

## 21 — Checklist

- [ ] Shebang is `#!/usr/bin/env bash` (or `#!/bin/sh` for POSIX scripts).
- [ ] `set -euo pipefail` is set at the top.
- [ ] All variable expansions are double-quoted.
- [ ] All function variables are declared `local`.
- [ ] Constants are declared `readonly`.
- [ ] Temporary files use `mktemp` with a `trap` for cleanup.
- [ ] Error messages go to stderr (`>&2`).
- [ ] No GNU-only flags — script runs on both Linux and macOS.
- [ ] No `eval`, no backticks, no `[ ]` in Bash scripts.
- [ ] No parsing of `ls` output. No useless `cat`.
- [ ] `shellcheck` reports no warnings.
- [ ] A `usage()` function exists for scripts that accept arguments.
- [ ] Files end with a single trailing newline.
