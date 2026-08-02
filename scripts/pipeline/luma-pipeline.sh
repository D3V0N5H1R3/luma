#!/usr/bin/env bash
# shellcheck shell=bash
#
# luma-pipeline.sh - shared helpers for the Luma prompt-pipeline runners.
#
# This file is meant to be *sourced* by luma-audit.sh and luma-fix.sh, not run
# directly. It provides the phase tables and the command construction that both
# runners share, so the audit (read-only) and fix (mutating) entry points stay
# in lock-step. It is the bash counterpart of scripts/pipeline/LumaPipeline.psm1
# and deliberately targets bash 3.2 (the stock macOS shell): no associative
# arrays, no mapfile, no ${var,,} case folding.

# Absolute directory that holds this library (and the sibling runners).
LUMA_PIPELINE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# Minimal, opt-out colour. Disabled when stdout is not a TTY or NO_COLOR is set.
if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
    LUMA_CLR_BANNER=$'\033[36m'
    LUMA_CLR_WARN=$'\033[33m'
    LUMA_CLR_OK=$'\033[32m'
    LUMA_CLR_DIM=$'\033[90m'
    LUMA_CLR_OFF=$'\033[0m'
else
    LUMA_CLR_BANNER=''
    LUMA_CLR_WARN=''
    LUMA_CLR_OK=''
    LUMA_CLR_DIM=''
    LUMA_CLR_OFF=''
fi

# Print a warning to stderr without aborting the caller.
luma_warn() {
    printf '%sWarning:%s %s\n' "$LUMA_CLR_WARN" "$LUMA_CLR_OFF" "$1" >&2
}

# Print a success line to stdout.
luma_ok() {
    printf '%s%s%s\n' "$LUMA_CLR_OK" "$1" "$LUMA_CLR_OFF"
}

# Print a framed banner: a rule, a title, an optional subtitle, another rule.
luma_write_banner() {
    local title="$1"
    local subtitle="${2:-}"
    local rule
    printf -v rule '%*s' 72 ''
    rule="${rule// /=}"
    printf '\n%s%s%s\n' "$LUMA_CLR_BANNER" "$rule" "$LUMA_CLR_OFF"
    printf '  %s\n' "$title"
    if [[ -n "$subtitle" ]]; then
        printf '  %s\n' "$subtitle"
    fi
    printf '%s%s%s\n' "$LUMA_CLR_BANNER" "$rule" "$LUMA_CLR_OFF"
}

# Resolve the repository root. Prefer the known layout (two levels up from this
# library), fall back to `git rev-parse` when the marker file is missing.
luma_repo_root() {
    local start="${1:-$LUMA_PIPELINE_DIR}"
    local candidate
    candidate="$(cd -- "$start/../.." && pwd)"
    if [[ -f "$candidate/CMakePresets.json" ]]; then
        printf '%s\n' "$candidate"
        return 0
    fi
    local git_root
    if git_root="$(git -C "$start" rev-parse --show-toplevel 2>/dev/null)"; then
        printf '%s\n' "$git_root"
        return 0
    fi
    printf '%s\n' "$candidate"
}

# Normalise and validate the shared --agent / --effort options that both the
# audit and fix runners accept. Operates on the caller's `agent` and `effort`
# variables in place: lower-cases `agent`, rejects any agent other than copilot
# or claude, and validates `effort` against Copilot's documented set. Claude
# accepts a different set (low..max plus ultracode), so for claude the CLI's own
# validation is deferred to rather than maintaining a second allow-list. Exits 2
# on an invalid value, matching the inline checks these runners previously each
# carried.
luma_validate_agent_and_effort() {
    agent="$(printf '%s' "$agent" | tr '[:upper:]' '[:lower:]')"
    case "$agent" in
        copilot|claude) ;;
        *)
            printf 'Error: invalid --agent %s (use copilot or claude).\n' "$agent" >&2
            exit 2
            ;;
    esac

    if [[ "$agent" == copilot ]]; then
        case "$effort" in
            ''|low|medium|high|xhigh|max) ;;
            *)
                printf 'Error: invalid --effort %s for copilot (use low, medium, high, xhigh, max).\n' \
                    "$effort" >&2
                exit 2
                ;;
        esac
    fi
}

# Emit the ordered audit (read-only) phase table.
# Fields, pipe-delimited: id|name|prompt|scope   (scope may be empty).
luma_audit_phases() {
    cat <<'LUMA_AUDIT_PHASES'
01-bug-search-core|Bug search (interpreter + stdlib)|bug-search.prompt.md|
02-bug-search-debugger|Bug search (debugger)|bug-search-debugger.prompt.md|
03-bug-search-language-server|Bug search (language server)|bug-search-language-server.prompt.md|
04-bug-search-editor-extension|Bug search (editor extensions)|bug-search-editor-extension.prompt.md|
05-consistency-check|Consistency check|consistency-check.prompt.md|
06-code-review|Code review (deep net)|code-review.prompt.md|core/ shared/ language-server/source/ debugger/source/
07-ux-audit|UX audit|ux-audit.prompt.md|
08-refactor-audit|Refactor audit|refactor-audit.prompt.md|
09-performance-audit|Performance audit|performance-audit.prompt.md|
LUMA_AUDIT_PHASES
}

# Emit the ordered fix (mutating) phase table.
# Fields, pipe-delimited: id|name|prompt|input_report|gate|commit|self_verifies.
# gate/commit/self_verifies are 1 (true) or 0 (false); input_report may be empty.
luma_fix_phases() {
    cat <<'LUMA_FIX_PHASES'
bug-fix-core|Bug fix (interpreter + stdlib)|bug-fix.prompt.md|01-bug-search-core.md|1|1|0
bug-fix-debugger|Bug fix (debugger)|bug-fix-debugger.prompt.md|02-bug-search-debugger.md|1|1|0
bug-fix-language-server|Bug fix (language server)|bug-fix-language-server.prompt.md|03-bug-search-language-server.md|1|1|0
bug-fix-editor-extension|Bug fix (editor extensions)|bug-fix-editor-extension.prompt.md|04-bug-search-editor-extension.md|1|1|0
refactor|Refactor|refactor.prompt.md|08-refactor-audit.md|1|1|0
optimize|Optimize|optimize.prompt.md|09-performance-audit.md|1|1|0
ux-improve|UX improvement|ux-improve.prompt.md|07-ux-audit.md|1|1|0
iterative-improvement|Iterative improvement (loop)|iterative-improvement.prompt.md||0|1|1
release-verification|Release verification (gate)|release-verification.prompt.md||0|0|1
update-learnings|Update learnings|update-learnings.prompt.md||0|1|0
LUMA_FIX_PHASES
}

# Locate the CLI executable for the requested agent, honouring the matching
# override variable ($LUMA_COPILOT for copilot, $LUMA_CLAUDE for claude).
luma_agent_exe() {
    local agent="$1"
    local exe
    if [[ "$agent" == claude ]]; then
        exe="${LUMA_CLAUDE:-claude}"
    else
        exe="${LUMA_COPILOT:-copilot}"
    fi
    if command -v "$exe" >/dev/null 2>&1; then
        printf '%s\n' "$exe"
        return 0
    fi
    return 1
}

# Run one prompt phase through the selected agent CLI (copilot or claude).
#
# Positional arguments:
#   1 agent       "copilot" or "claude"
#   2 mode        "plan" (read-only) or "agent" (mutating)
#   3 instruction the full natural-language instruction for the agent
#   4 repo_root   directory to run the agent in
#   5 sink        plan mode: report file (Copilot JSONL is reduced to the final
#                 message; Claude text output is captured as-is);
#                 agent mode: log file (tee)
#   6 log_dir     Copilot --log-dir value, or empty to omit (ignored for claude)
#   7 model       value for the model flag, or empty to omit
#   8 effort      value for the effort flag, or empty to omit
#   9 is_dry_run  "true" to print the command without running it
#
# Returns the agent exit code (0 in dry-run mode).
luma_invoke_agent_phase() {
    local agent="$1"
    local mode="$2"
    local instruction="$3"
    local repo_root="$4"
    local sink="$5"
    local log_dir="$6"
    local model="$7"
    local effort="$8"
    local is_dry_run="$9"

    local -a args=()
    local exe_name=copilot
    if [[ "$agent" == claude ]]; then
        exe_name=claude
        # Claude Code: -p is headless; text output keeps stdout clean to capture.
        args=(-p "$instruction" --output-format text)
        if [[ "$mode" == plan ]]; then
            # Plan mode is strictly read-only: no edits, no mutating commands.
            args+=(--permission-mode plan)
        else
            # Auto-accept edits and allow the tools a fixer needs, but deny any
            # push so nothing leaves the machine (deny rules beat allow rules).
            args+=(--permission-mode acceptEdits)
            args+=(--allowedTools Bash Edit Write)
            args+=(--disallowedTools "Bash(git push *)")
        fi
        if [[ -n "$model" ]]; then
            args+=(--model "$model")
        fi
        if [[ -n "$effort" ]]; then
            args+=(--effort "$effort")
        fi
    else
        args=(-p "$instruction" --allow-all-tools --no-ask-user --no-color)
        if [[ "$mode" == plan ]]; then
            # Read-only audit. Do NOT use Copilot's interactive `--plan` mode
            # here: in a non-interactive `-p` run it delivers its result through
            # the exit_plan_mode tool / a plan.md file rather than stdout, so it
            # emits an empty report (the agent exits 0 having written nothing to
            # stdout). Instead restrict the model to the read/search tools with
            # an allow-list, which guarantees it cannot modify, create, or run
            # anything, and still lets it write its report as the final message.
            #
            # Emit the structured JSONL event stream (--output-format=json), not
            # text. In text mode Copilot streams every intermediate assistant
            # message to stdout - and, when a prompt fans out to parallel
            # background sub-agents, their transcripts interleave into corrupted
            # output - so a plain stdout capture is not a clean report. (--silent
            # only drops the stats footer, it does not suppress the narration.)
            # The caller reduces the JSONL to the agent's final message.
            args+=(--output-format=json --available-tools="view,grep,glob")
        else
            args+=(--deny-tool="shell(git push)")
        fi
        if [[ -n "$log_dir" ]]; then
            args+=(--log-dir="$log_dir")
        fi
        if [[ -n "$model" ]]; then
            args+=(--model="$model")
        fi
        if [[ -n "$effort" ]]; then
            args+=(--effort="$effort")
        fi
    fi

    if [[ "$is_dry_run" == true ]]; then
        local rendered
        rendered="$(luma_render_command "$exe_name" "${args[@]}")"
        printf '%s  [dry-run] %s%s\n' "$LUMA_CLR_DIM" "$rendered" "$LUMA_CLR_OFF"
        if [[ "$mode" == plan ]]; then
            printf '%s  [dry-run] stdout -> %s%s\n' "$LUMA_CLR_DIM" "$sink" "$LUMA_CLR_OFF"
        else
            printf '%s  [dry-run] tee    -> %s%s\n' "$LUMA_CLR_DIM" "$sink" "$LUMA_CLR_OFF"
        fi
        return 0
    fi

    local exe
    if ! exe="$(luma_agent_exe "$agent")"; then
        luma_warn "$exe_name CLI not found on PATH (install it, or set LUMA_COPILOT / LUMA_CLAUDE)."
        return 127
    fi

    local exit_code=0
    if [[ "$mode" == plan ]]; then
        if [[ "$agent" == copilot ]]; then
            # Copilot plan mode streams JSONL events; capture them, then reduce
            # to the final report. Preserve the agent's own exit code - a missing
            # parser or empty extraction must not mask a successful or failed run.
            # stderr is captured separately so a diagnostic (e.g. an auth error)
            # is not lost, and can be surfaced when the agent produces no report.
            local raw err
            raw="$(mktemp "${TMPDIR:-/tmp}/luma-report.XXXXXX")"
            err="$(mktemp "${TMPDIR:-/tmp}/luma-report-err.XXXXXX")"
            if ( cd -- "$repo_root" && "$exe" "${args[@]}" ) >"$raw" 2>"$err"; then
                exit_code=0
            else
                exit_code=$?
            fi
            luma_extract_agent_report "$raw" >"$sink" || true
            if [[ ! -s "$sink" ]]; then
                if [[ -s "$raw" ]]; then
                    # The agent produced output but no extractable final message;
                    # keep the raw transcript so nothing is silently dropped.
                    luma_warn "could not extract a final report from Copilot output; keeping the raw JSONL transcript."
                    cat "$raw" >"$sink"
                else
                    # The agent wrote nothing to stdout: this phase did no work.
                    # Surface the captured stderr in the report and force a
                    # failure so the pipeline never reports a hollow success.
                    luma_warn "the agent produced no report for this phase (empty output); see the captured error in the report."
                    {
                        printf '# Phase produced no report\n\n'
                        printf 'The agent exited with code %d and wrote nothing to standard output, so no report could be produced.\n\n' "$exit_code"
                        printf 'Captured standard error:\n\n'
                        printf '```\n'
                        if [[ -s "$err" ]]; then
                            cat "$err"
                        else
                            printf '(none)\n'
                        fi
                        printf '```\n'
                    } >"$sink"
                    if [[ "$exit_code" -eq 0 ]]; then
                        exit_code=1
                    fi
                fi
            fi
            rm -f "$raw" "$err"
        else
            # Claude's headless -p text mode already prints only the final
            # message, so a direct stdout capture is the clean report.
            if ( cd -- "$repo_root" && "$exe" "${args[@]}" ) >"$sink"; then
                exit_code=0
            else
                exit_code=$?
            fi
        fi
    else
        if ( cd -- "$repo_root" && "$exe" "${args[@]}" ) > >(tee "$sink") 2>&1; then
            exit_code=0
        else
            exit_code=$?
        fi
    fi
    return "$exit_code"
}

# Render a command as a copy-pasteable string, quoting arguments that need it.
luma_render_command() {
    local out=''
    local arg
    for arg in "$@"; do
        if [[ "$arg" == *[[:space:]]* || "$arg" == *'"'* ]]; then
            local escaped="${arg//\"/\\\"}"
            arg="\"$escaped\""
        fi
        if [[ -z "$out" ]]; then
            out="$arg"
        else
            out="$out $arg"
        fi
    done
    printf '%s\n' "$out"
}

# Reduce a Copilot JSONL event stream (from `--output-format json`) to the
# agent's final report on stdout. Argument 1 is the JSONL file. The report is
# the `content` of the last `assistant.message` event that carries text:
# tool-call-only steps have empty content, and a background sub-agent's messages
# are emitted before the main agent's closing synthesis, so the last text
# message is the real report. Tries python3, then python, then node (Copilot
# itself runs on Node, so one is normally present); each reads the file afresh,
# and its output is committed only on success, so a parser that is present but
# broken falls through cleanly instead of corrupting the report. Returns
# non-zero when no report is found or no parser works, so the caller can fall
# back to keeping the raw transcript.
luma_extract_agent_report() {
    local jsonl="$1"
    local py_prog='import sys, json
last = ""
# Split on "\n" only. U+2028/U+2029/NEL are valid unescaped in JSON strings, and
# str.splitlines() would cut a record on them, dropping the message; node and the
# PowerShell mirror split on newlines only, so this keeps all three in lock-step.
for line in sys.stdin.buffer.read().decode("utf-8", "replace").split("\n"):
    line = line.strip()
    if not line:
        continue
    try:
        obj = json.loads(line)
    except Exception:
        continue
    if isinstance(obj, dict) and obj.get("type") == "assistant.message":
        data = obj.get("data")
        if not isinstance(data, dict):
            continue
        content = data.get("content")
        if isinstance(content, str) and content.strip():
            last = content
if not last:
    sys.exit(3)
sys.stdout.buffer.write(last.rstrip("\r\n").encode("utf-8"))'
    local js_prog='const fs = require("fs");
let last = "";
for (const line of fs.readFileSync(0, "utf8").split(/\r?\n/)) {
  const s = line.trim();
  if (!s) continue;
  let o;
  try { o = JSON.parse(s); } catch (e) { continue; }
  if (o && o.type === "assistant.message") {
    const c = o.data && o.data.content;
    if (typeof c === "string" && c.trim().length > 0) last = c;
  }
}
if (!last) process.exit(3);
process.stdout.write(last.replace(/[\r\n]+$/, ""));'
    local out=''
    local have_parser=false
    if command -v python3 >/dev/null 2>&1; then
        have_parser=true
        if out="$(python3 -c "$py_prog" <"$jsonl" 2>/dev/null)"; then
            printf '%s\n' "$out"
            return 0
        fi
    fi
    if command -v python >/dev/null 2>&1; then
        have_parser=true
        if out="$(python -c "$py_prog" <"$jsonl" 2>/dev/null)"; then
            printf '%s\n' "$out"
            return 0
        fi
    fi
    if command -v node >/dev/null 2>&1; then
        have_parser=true
        if out="$(node -e "$js_prog" <"$jsonl" 2>/dev/null)"; then
            printf '%s\n' "$out"
            return 0
        fi
    fi
    if [[ "$have_parser" != true ]]; then
        luma_warn "no JSON parser (python3/python/node) on PATH; install one for clean audit reports."
    fi
    return 3
}

# Build (optionally) and test the project through the CMake preset gate.
#
# Positional arguments:
#   1 repo_root
#   2 preset      CMake preset name (e.g. "default")
#   3 skip_build  "true" to skip the build step
#   4 skip_test   "true" to skip the test step
#   5 is_dry_run  "true" to print commands without running them
#
# Returns 0 when the gate passes, non-zero otherwise.
luma_build_and_test() {
    local repo_root="$1"
    local preset="$2"
    local skip_build="$3"
    local skip_test="$4"
    local is_dry_run="$5"

    local build_dir="$repo_root/build"

    if [[ "$is_dry_run" == true ]]; then
        if [[ ! -d "$build_dir" ]]; then
            printf '%s  [dry-run] cmake --preset %s%s\n' \
                "$LUMA_CLR_DIM" "$preset" "$LUMA_CLR_OFF"
        fi
        if [[ "$skip_build" != true ]]; then
            printf '%s  [dry-run] cmake --build --preset %s%s\n' \
                "$LUMA_CLR_DIM" "$preset" "$LUMA_CLR_OFF"
        fi
        if [[ "$skip_test" != true ]]; then
            printf '%s  [dry-run] ctest --preset %s%s\n' \
                "$LUMA_CLR_DIM" "$preset" "$LUMA_CLR_OFF"
        fi
        return 0
    fi

    # Preflight: the build toolchain must be present. Distinguish a missing
    # toolchain (nothing to build with) from a genuinely broken build, so the
    # caller's "baseline is not green" message is not misattributed to the code.
    if ! command -v cmake >/dev/null 2>&1; then
        luma_warn 'cmake is not on PATH; the build + test gate cannot run. Install CMake and a C++ toolchain, or pass --skip-baseline to skip the gate.'
        return 1
    fi
    if [[ "$skip_test" != true ]] && ! command -v ctest >/dev/null 2>&1; then
        luma_warn 'ctest is not on PATH; the test gate cannot run. Install CMake and a C++ toolchain, or pass --skip-baseline / --skip-test.'
        return 1
    fi

    if [[ ! -d "$build_dir" ]]; then
        printf 'Configuring (cmake --preset %s)...\n' "$preset"
        if ! ( cd -- "$repo_root" && cmake --preset "$preset" ); then
            return 1
        fi
    fi
    if [[ "$skip_build" != true ]]; then
        printf 'Building (cmake --build --preset %s)...\n' "$preset"
        if ! ( cd -- "$repo_root" && cmake --build --preset "$preset" ); then
            return 1
        fi
    fi
    if [[ "$skip_test" != true ]]; then
        printf 'Testing (ctest --preset %s)...\n' "$preset"
        if ! ( cd -- "$repo_root" && ctest --preset "$preset" ); then
            return 1
        fi
    fi
    return 0
}

# Resolve a Python interpreter for the lint/format scripts. Prints the command
# name on stdout and returns 0, preferring python3 (the usual name on macOS and
# Linux, where these shell runners run) and falling back to python. Returns 1
# when neither is on PATH so the caller can skip the lint/format gate rather than
# fail hard, matching the skip-if-missing philosophy of scripts/lint.py.
luma_python_exe() {
    local exe
    for exe in python3 python; do
        if command -v "$exe" >/dev/null 2>&1; then
            printf '%s\n' "$exe"
            return 0
        fi
    done
    return 1
}

# Ensure build/compile_commands.json exists so scripts/lint.py can run clang-tidy.
#
# The Makefile and Ninja generators emit a compile database when
# CMAKE_EXPORT_COMPILE_COMMANDS is set (every preset sets it), so on the Linux and
# macOS hosts these shell runners target, the normal `default` build already
# produces build/compile_commands.json, and this is a no-op. When it is missing
# (for example build/ was configured with a generator that does not emit one), do
# a configure-only pass with a database-capable generator (Ninja, else Unix
# Makefiles) into a dedicated build-compiledb/ directory and copy the result into
# build/ where lint.py looks for it.
#
# Best-effort: does nothing when clang-tidy is absent (lint.py would skip it
# anyway) and warns but still succeeds when the database cannot be produced, so a
# missing tool never fails the gate - clang-tidy just stays skipped.
#
# Positional arguments:
#   1 repo_root
#   2 is_dry_run  "true" to print the intended command without running it
luma_ensure_compile_db() {
    local repo_root="$1"
    local is_dry_run="$2"

    if [[ "$is_dry_run" == true ]]; then
        printf '%s  [dry-run] generate build/compile_commands.json for clang-tidy (configure-only)%s\n' \
            "$LUMA_CLR_DIM" "$LUMA_CLR_OFF"
        return 0
    fi

    local build_db="$repo_root/build/compile_commands.json"

    # Already present (Makefiles/Ninja emitted it, or a previous run copied it in).
    [[ -f "$build_db" ]] && return 0
    # No clang-tidy: lint.py skips it regardless, so a database would be moot.
    command -v clang-tidy >/dev/null 2>&1 || return 0
    if ! command -v cmake >/dev/null 2>&1; then
        luma_warn 'cmake not found; cannot generate compile_commands.json (clang-tidy will be skipped).'
        return 0
    fi

    local generator
    if command -v ninja >/dev/null 2>&1; then
        generator='Ninja'
    else
        generator='Unix Makefiles'
    fi

    local db_dir="$repo_root/build-compiledb"
    printf 'Generating compile_commands.json for clang-tidy (cmake -G "%s", configure-only)...\n' "$generator"
    if ! cmake -S "$repo_root" -B "$db_dir" -G "$generator" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DLUMA_BUILD_TESTS=OFF; then
        luma_warn 'could not configure a compile database; clang-tidy will be skipped this run.'
        return 0
    fi

    local generated_db="$db_dir/compile_commands.json"
    if [[ ! -f "$generated_db" ]]; then
        luma_warn 'configure produced no compile_commands.json; clang-tidy will be skipped.'
        return 0
    fi

    mkdir -p "$repo_root/build"
    cp -f "$generated_db" "$build_db"
    return 0
}

# Run the deterministic lint/format quality gate (format.py, then lint.py).
#
# scripts/format.py applies every available formatter plus the safe auto-fix
# subset of the linters (it orders each language's lint --fix before its
# formatter internally). scripts/lint.py then verifies what remains, including
# clang-tidy over the C++ sources; it is read-only and is the authority for the
# gate, which passes only when lint.py exits 0. lint.py always runs last because
# it is the verification pass. clang-tidy needs build/compile_commands.json, which
# luma_ensure_compile_db emits on demand; lint.py skips clang-tidy gracefully when
# the database still cannot be produced.
#
# Positional arguments:
#   1 repo_root
#   2 check_only  "true" to run lint.py only (skip the mutating format.py); used
#                 for the baseline check so the starting tree is verified without
#                 being changed
#   3 is_dry_run  "true" to print the commands without running them
#
# Returns 0 when lint.py passes (or the gate is skipped / dry-run), non-zero on a
# lint failure.
luma_lint_and_format() {
    local repo_root="$1"
    local check_only="$2"
    local is_dry_run="$3"

    if [[ "$is_dry_run" == true ]]; then
        if [[ "$check_only" != true ]]; then
            printf '%s  [dry-run] python scripts/format.py%s\n' \
                "$LUMA_CLR_DIM" "$LUMA_CLR_OFF"
        fi
        luma_ensure_compile_db "$repo_root" true
        printf '%s  [dry-run] python scripts/lint.py%s\n' \
            "$LUMA_CLR_DIM" "$LUMA_CLR_OFF"
        return 0
    fi

    local py
    if ! py="$(luma_python_exe)"; then
        luma_warn 'no Python interpreter (python3/python) on PATH; skipping the lint/format gate.'
        return 0
    fi

    if [[ "$check_only" != true ]]; then
        printf 'Formatting (python scripts/format.py)...\n'
        # A non-zero format.py means a formatter hit a problem it could not
        # auto-fix; lint.py is the authority for the gate and surfaces the same
        # issue, so warn here but let lint.py decide pass/fail.
        if ! ( cd -- "$repo_root" && "$py" scripts/format.py ); then
            luma_warn 'formatter reported problems it could not auto-fix; see the lint output below.'
        fi
    fi

    # clang-tidy (invoked by lint.py) needs build/compile_commands.json; emit it on
    # demand. Best-effort - a missing toolchain just leaves clang-tidy skipped.
    luma_ensure_compile_db "$repo_root" false

    printf 'Linting (python scripts/lint.py)...\n'
    if ! ( cd -- "$repo_root" && "$py" scripts/lint.py ); then
        return 1
    fi
    return 0
}

# Report whether the working tree is clean (no staged or unstaged changes).
# In dry-run mode it assumes clean and says so. Returns 0 when clean.
luma_clean_working_tree() {
    local repo_root="$1"
    local is_dry_run="$2"

    if [[ "$is_dry_run" == true ]]; then
        printf '%s  [dry-run] git -C %s status --porcelain%s\n' \
            "$LUMA_CLR_DIM" "$repo_root" "$LUMA_CLR_OFF"
        return 0
    fi

    local status
    status="$(git -C "$repo_root" status --porcelain)"
    if [[ -n "$status" ]]; then
        return 1
    fi
    return 0
}

# Stage the tree and commit a checkpoint, if anything changed. Run artifacts are
# kept out of the commit: the default pipeline-artifacts/ is git-ignored, and any
# custom artifact root that lives inside the working tree is unstaged before the
# commit. Git resolves whether the path is inside the repo, so an out-of-tree
# artifact root is a harmless no-op.
#
# Positional arguments:
#   1 repo_root
#   2 subject        commit subject line
#   3 agent          "copilot" or "claude" (selects the Co-authored-by trailer)
#   4 artifact_root  artifact directory to keep out of the commit (may be empty)
#   5 is_dry_run     "true" to print commands without running them
#
# Progress is printed to stderr; the resulting short SHA (or empty when nothing
# was committed) is printed to stdout so callers can capture it.
luma_git_checkpoint() {
    local repo_root="$1"
    local subject="$2"
    local agent="$3"
    local artifact_root="$4"
    local is_dry_run="$5"

    # Attribute the checkpoint to the agent that actually did the work.
    local trailer
    if [[ "$agent" == claude ]]; then
        trailer='Co-authored-by: Claude <noreply@anthropic.com>'
    else
        trailer='Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>'
    fi

    if [[ "$is_dry_run" == true ]]; then
        printf '%s  [dry-run] git add -A && git reset -- %s && git commit%s\n' \
            "$LUMA_CLR_DIM" "${artifact_root:-<artifact-root>}" "$LUMA_CLR_OFF" >&2
        return 0
    fi

    git -C "$repo_root" add -A >&2
    # Keep run artifacts out of the commit even when the artifact root lives
    # inside the tree and is not covered by .gitignore. When the root is outside
    # the repo, git errors that the path is outside the repository; ignore it.
    if [[ -n "$artifact_root" ]]; then
        git -C "$repo_root" reset -q -- "$artifact_root" >/dev/null 2>&1 || true
    fi

    if git -C "$repo_root" diff --cached --quiet; then
        printf 'No changes to commit for this phase.\n' >&2
        return 0
    fi

    local message="$subject

$trailer"

    if ! git -C "$repo_root" commit -m "$message" >&2; then
        luma_warn 'git commit failed.'
        return 1
    fi

    local sha
    sha="$(git -C "$repo_root" rev-parse --short HEAD)"
    printf 'Committed %s\n' "$sha" >&2
    printf '%s\n' "$sha"
}

# Decide whether a failed per-phase step should stop the run. Used by the fix
# runner so the abort rule stays in one place. Returns 0 (stop) or 1 (keep going).
#
# The rule:
#   * Without --continue-on-failure, any failure stops the run.
#   * With --continue-on-failure, a per-file failure (agent-failed, gate-failed,
#     error) is skipped and the run continues -- that is the flag's purpose.
#   * A 'commit-failed' is different: it is systemic. A pre-commit hook or git
#     itself keeps rejecting the commit, so advancing to the next file cannot
#     make it succeed, and because luma_git_checkpoint stages the whole tree
#     (git add -A), the un-committable change is re-staged into every later
#     checkpoint and re-fails identically -- a pile-up. So a commit-failed stops
#     the run even under --continue-on-failure, UNLESS --revert-on-failure is set
#     (which resets the tree clean, making it safe to carry on).
#
# Positional arguments:
#   1 status               the step's status (ok, error, agent-failed,
#                          gate-failed, commit-failed)
#   2 continue_on_failure  "true" when --continue-on-failure was passed
#   3 revert_on_failure    "true" when --revert-on-failure was passed
luma_should_abort() {
    local status="$1"
    local continue_on_failure="$2"
    local revert_on_failure="$3"

    [[ "$status" == ok ]] && return 1
    [[ "$continue_on_failure" != true ]] && return 0
    [[ "$status" == commit-failed && "$revert_on_failure" != true ]] && return 0
    return 1
}

# Move the pipeline artifact root aside into .git/ so the release-verification
# phase's `git clean -Xdf` cannot delete this run's logs and reports. Echoes the
# backup path (empty when there is nothing to protect: a missing root, or a root
# outside the working tree that `git clean` never reaches). A distinct backup
# name keeps this runner-owned safeguard from colliding with the agent-side
# set-aside in the release-verification prompt. This is the bash counterpart of
# Save-PipelineArtifact in LumaPipeline.psm1.
luma_save_pipeline_artifacts() {
    local artifact_root="$1"
    local repo_root="$2"

    [[ -d "$artifact_root" ]] || return 0

    local root_abs repo_abs
    root_abs="$(cd -- "$artifact_root" && pwd)" || return 0
    repo_abs="$(cd -- "$repo_root" && pwd)" || return 0
    case "$root_abs/" in
        "$repo_abs"/*) ;;
        *) return 0 ;;
    esac

    local git_dir
    git_dir="$(git -C "$repo_root" rev-parse --absolute-git-dir 2>/dev/null)" || return 0
    [[ -n "$git_dir" ]] || return 0
    local backup="$git_dir/_pipeline-artifacts.pipeline.bak"

    rm -rf "$backup"
    mv "$artifact_root" "$backup" || return 0
    printf '%s\n' "$backup"
}

# Move an artifact root saved by luma_save_pipeline_artifacts back into place.
# Best-effort and idempotent: an empty backup path or a missing backup is a
# no-op, so a caller can invoke this unconditionally. If the artifact root was
# recreated while set aside, the saved entries are merged back into it. This is
# the bash counterpart of Restore-PipelineArtifact in LumaPipeline.psm1.
luma_restore_pipeline_artifacts() {
    local backup="$1"
    local artifact_root="$2"

    [[ -n "$backup" && -d "$backup" ]] || return 0

    if [[ ! -e "$artifact_root" ]]; then
        mkdir -p "$(dirname -- "$artifact_root")"
        mv "$backup" "$artifact_root" || return 0
        return 0
    fi

    local entry base
    for entry in "$backup"/* "$backup"/.[!.]*; do
        [[ -e "$entry" ]] || continue
        base="$(basename -- "$entry")"
        # ${artifact_root:?} aborts rather than expanding to "/$base" (a root-level
        # delete) should artifact_root ever be empty. It cannot be here - the
        # branch above returned when the root was absent - but the guard keeps the
        # destructive rm safe regardless (ShellCheck SC2115).
        rm -rf "${artifact_root:?}/$base"
        mv "$entry" "${artifact_root:?}/$base"
    done
    rmdir "$backup" 2>/dev/null || rm -rf "$backup"
}
