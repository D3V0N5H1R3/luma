#!/usr/bin/env bash
#
# luma-conformance.sh - per-file conformance pass of the Luma prompt pipeline.
#
# For each selected file-type target (C++, CSS, CMake, GitHub Actions,
# JavaScript, Luma, Markdown, PowerShell, Python, Rust, Shell, TypeScript) this
# runner enumerates every matching tracked file and runs one agent session per
# file, telling it to make that single file conform to the target's
# instruction/guide files under instructions/ (and documents/ for Luma) and to
# fix every issue it finds.
#
# This is the bash counterpart of Invoke-LumaConformance.ps1 and reuses the same
# shared helpers (luma-pipeline.sh) and the same safety model as luma-fix.sh:
#
#   * a clean working tree is required before starting (unless --allow-dirty);
#   * work happens on a dedicated pipeline/conformance-<stamp> branch (unless
#     --no-branch);
#   * a green build + test baseline is established first when a build-gated
#     target (C++, CMake) is selected (unless --skip-baseline);
#   * each file is committed as its own checkpoint (unless --no-commit);
#   * the cmake + ctest gate runs for the build-affecting targets at the cadence
#     set by --gate-mode, and only a green tree continues;
#   * the run stops at the first failure (unless --continue-on-failure).
#
# Only the C++ and CMake targets change what `cmake --build` + `ctest` covers,
# so only those are gated by the script. For every target the per-file
# instruction tells the agent to run the verification appropriate to that file
# type (cargo, npm/tsc, shellcheck, PSScriptAnalyzer, markdownlint, actionlint,
# the Luma test runner, ...) and to keep the project green. Nothing is pushed.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/pipeline/luma-pipeline.sh
# shellcheck disable=SC1091
source "$SCRIPT_DIR/luma-pipeline.sh"

usage() {
    cat <<'USAGE'
Usage: luma-conformance.sh [options]

Review and fix the repository's source files for conformance to the project's
coding-standard guides, one file at a time, through an agent CLI (Copilot or
Claude). Work happens on a dedicated branch and is committed file by file.

Options:
  --target <filter>       Only run targets whose id matches exactly, or whose id
                          or name contains <filter> (case-insensitive).
                          Repeatable, and comma-separated values are accepted.
                          Omit to run every target. Use --list to see them.
  --path <substring>      Only process files whose repository-relative path
                          contains this case-insensitive substring.
  --max-files <n>         Cap the number of files processed per target.
  --artifact-root <dir>   Base directory for run output
                          (default: <repo>/pipeline-artifacts).
  --preset <name>         CMake preset for the gate (default: default).
  --agent <name>          Agent CLI backend: copilot (default) or claude.
  --model <name>          Value for the agent's model flag
                          (default: claude-opus-4.8; pass '' for the agent's own).
  --effort <level>        Value for the agent's effort flag (low, medium, high,
                          xhigh, max; default: max; pass '' to omit).
  --gate-mode <mode>      When the cmake + ctest gate runs for build-affecting
                          targets (C++, CMake): per-target (default), per-file,
                          or off.
  --allow-dirty           Do not require a clean working tree before starting.
  --skip-baseline         Skip the initial green build + test baseline.
  --skip-build            Skip the build step of every gate (test only).
  --skip-test             Skip the test step of every gate (build only).
  --no-branch             Run on the current branch instead of a new one.
  --no-commit             Do not commit a checkpoint after each file.
  --continue-on-failure   Keep going after a failed file or gate instead of
                          stopping.
  --revert-on-failure     On a failure, git reset --hard: to HEAD after a failed
                          file or per-file gate; to the target's starting commit
                          after a failed per-target gate. Untracked files a
                          failed session created are left in place.
  --list                  List the targets in order (with file counts) and exit.
  --list-files            List the files each selected target would process and
                          exit.
  --dry-run               Print every agent / cmake / ctest / git command
                          without executing it.
  -h, --help              Show this help and exit.

Examples:
  luma-conformance.sh --list
  luma-conformance.sh --target cpp --list-files
  luma-conformance.sh --target markdown --dry-run
  luma-conformance.sh --target shell,powershell
USAGE
}

# Emit the ordered file-type targets. Tab-delimited (the extension patterns
# contain '|' regex alternations, so a pipe field separator cannot be used):
#   id <TAB> name <TAB> pattern <TAB> gate <TAB> verify <TAB> instructions
# gate is 1 only for the targets that change what `cmake --build` + `ctest`
# covers (C++ sources and CMake build files); every other target is verified by
# the agent through the verify field. instructions is a space-separated list of
# repository-relative guide paths.
conformance_targets() {
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        'cpp' 'C++ sources' '\.(cpp|hpp|h)$' '1' \
        'build and test with cmake --build --preset default and ctest --preset default (or the relevant subset)' \
        'instructions/cpp.instructions.md instructions/testing.instructions.md'
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        'css' 'CSS files' '\.css$' '0' \
        'run the CSS lint (for example npx stylelint) if the project configures one' \
        'instructions/css.instructions.md'
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        'cmake' 'CMake files' '(^|/)CMakeLists\.txt$|\.cmake$' '1' \
        're-configure and build with the default preset to confirm the change is valid' \
        'instructions/cmake.instructions.md'
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        'github-actions' 'GitHub Actions workflows' '^\.github/workflows/.*\.(ya?ml)$' '0' \
        'validate the workflow (for example actionlint) and confirm the YAML is well-formed' \
        'instructions/github-actions.instructions.md instructions/github-actions-recipes.instructions.md'
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        'javascript' 'JavaScript sources' '\.(js|mjs|cjs)$' '0' \
        'run the owning package'\''s lint/test (for example npm run lint and npm test) if configured' \
        'instructions/javascript.instructions.md instructions/testing.instructions.md'
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        'luma' 'Luma sources' '\.luma$' '0' \
        'run the file through the built luma interpreter (for example build/<config>/luma <file> --test, or python scripts/run_examples.py / python scripts/run_luma_tests.py) as appropriate' \
        'instructions/luma.instructions.md instructions/testing.instructions.md documents/Luma_User_Manual.md documents/Luma_Standard_Library_Reference.md documents/Luma_Solaris_Guide.md documents/Luma_GraphicalUi_Guide.md documents/Luma_Performance_Guide.md documents/Luma_Error_Handling.md documents/Luma_Coding_Guidelines.md'
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        'markdown' 'Markdown files' '\.md$' '0' \
        'run the Markdown lint (for example npx markdownlint-cli2) if configured, and keep every link valid' \
        'instructions/markdown.instructions.md instructions/readme.instructions.md'
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        'powershell' 'PowerShell scripts' '\.(ps1|psm1|psd1)$' '0' \
        'run PSScriptAnalyzer (for example pwsh -File scripts/run_psscriptanalyzer.ps1, or Invoke-ScriptAnalyzer)' \
        'instructions/powershell.instructions.md'
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        'python' 'Python sources' '\.py$' '0' \
        'run the Python checks (for example python scripts/lint.py --only ruff) and any affected tests' \
        'instructions/python.instructions.md instructions/testing.instructions.md'
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        'rust' 'Rust sources' '\.rs$' '0' \
        'run cargo fmt --check, cargo clippy, and cargo test in the owning crate' \
        'instructions/rust.instructions.md instructions/testing.instructions.md'
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        'shell' 'Shell scripts' '\.(sh|bash)$' '0' \
        'run shellcheck on the script and confirm it still parses' \
        'instructions/shell.instructions.md'
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        'typescript' 'TypeScript sources' '\.(ts|tsx)$' '0' \
        'run the owning package'\''s type-check and lint/test (for example tsc --noEmit, npm run lint, npm test) if configured' \
        'instructions/typescript.instructions.md instructions/testing.instructions.md'
}

# Enumerate the tracked files a target should process, one per line. Uses the
# globals repo_root, path_filter, max_files, and the self_exclude array. Lists
# tracked files with `git ls-files` (already byte-sorted), keeps those matching
# the target's extension pattern (case-insensitively, mirroring the PowerShell
# runner's default -match so an uppercase extension like .CPP still matches),
# drops vendored code under external/ (keeping
# the first-party external/gui-framework/), applies the optional path substring
# filter, drops the runner's own active files, and caps at max_files.
list_target_files() {
    local pattern="$1"
    local rel lower_rel lower_pf ex skip count
    count=0
    if [[ -n "$path_filter" ]]; then
        lower_pf="$(printf '%s' "$path_filter" | tr '[:upper:]' '[:lower:]')"
    fi
    while IFS= read -r rel; do
        [[ -n "$rel" ]] || continue
        case "$rel" in
            external/gui-framework/*) ;;
            external/*) continue ;;
        esac
        if [[ -n "$path_filter" ]]; then
            lower_rel="$(printf '%s' "$rel" | tr '[:upper:]' '[:lower:]')"
            case "$lower_rel" in
                *"$lower_pf"*) ;;
                *) continue ;;
            esac
        fi
        skip=false
        for ex in ${self_exclude[@]+"${self_exclude[@]}"}; do
            if [[ "$rel" == "$ex" ]]; then
                skip=true
                break
            fi
        done
        [[ "$skip" == true ]] && continue
        printf '%s\n' "$rel"
        count=$((count + 1))
        if [[ "$max_files" -gt 0 && "$count" -ge "$max_files" ]]; then
            break
        fi
    done < <(git -C "$repo_root" ls-files | grep -Ei "$pattern" || true)
}

# Build the per-file agent instruction for one target file. Arguments:
#   1 name          the target's display name (e.g. "C++ sources")
#   2 verify        the verification the agent should run
#   3 rel           the repository-relative file path
#   4 instructions  space-separated list of guide paths
build_conformance_instruction() {
    local name="$1" verify="$2" rel="$3" instructions="$4"
    local singular="${name%s}"
    local doc
    local IFS=$' \t\n'

    printf 'You are reviewing and fixing a single %s in the Luma project for conformance to the project'\''s coding standards.\n\n' \
        "$singular"
    printf 'Target file (this is the only file you should change, unless a fix strictly requires an adjustment to a directly-coupled file such as its test):\n'
    printf '    %s\n\n' "$rel"
    printf 'Authoritative standards - read each one in full and apply every rule that is relevant to this file:\n'
    for doc in $instructions; do
        printf '    %s\n' "$doc"
    done
    printf '    instructions/learnings.instructions.md   (always-on: accumulated project pitfalls)\n\n'
    printf 'Do this:\n'
    printf '1. Read the target file and the standards above.\n'
    printf '2. Find every place the file violates those standards - naming, structure, style, idioms, error handling, comments, and anything else the guides require - and any bugs you notice while reviewing.\n'
    printf '3. Fix all of them with the smallest correct changes. Preserve the existing behaviour and public interface unless a guide requires otherwise.\n'
    printf '4. If the file is a test, keep it aligned with instructions/testing.instructions.md; if a fix changes tested behaviour, update the matching test.\n'
    printf '5. Verify your change: %s. Keep the full build and test suite green.\n\n' "$verify"
    printf 'Constraints:\n'
    printf -- '- Do not modify unrelated files. Do not touch vendored code under external/ (except external/gui-framework/).\n'
    printf -- '- Do not push to any remote. Do not amend, rebase, or rewrite existing commits. Leave staging and committing to the pipeline.\n'
    printf -- '- If the file already fully conforms, make no changes and say so briefly.\n'
}

need_value() {
    printf 'Error: %s requires a value.\n' "$1" >&2
    exit 2
}

# Split a comma-separated filter argument and append the pieces to `filters`.
add_filters() {
    local raw="$1"
    local part
    local -a parts=()
    IFS=',' read -r -a parts <<< "$raw" || true
    for part in ${parts[@]+"${parts[@]}"}; do
        part="${part#"${part%%[![:space:]]*}"}"
        part="${part%"${part##*[![:space:]]}"}"
        if [[ -n "$part" ]]; then
            filters+=("$part")
        fi
    done
}

path_filter=''
max_files=0
artifact_root=''
preset='default'
agent='copilot'
model='claude-opus-4.8'
effort='max'
gate_mode='per-target'
allow_dirty=false
skip_baseline=false
skip_build=false
skip_test=false
no_branch=false
no_commit=false
continue_on_failure=false
revert_on_failure=false
is_list=false
is_list_files=false
is_dry_run=false
filters=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target) shift; [[ $# -gt 0 ]] || need_value --target; add_filters "$1" ;;
        --target=*) add_filters "${1#*=}" ;;
        --path) shift; [[ $# -gt 0 ]] || need_value --path; path_filter="$1" ;;
        --path=*) path_filter="${1#*=}" ;;
        --max-files) shift; [[ $# -gt 0 ]] || need_value --max-files; max_files="$1" ;;
        --max-files=*) max_files="${1#*=}" ;;
        --artifact-root) shift; [[ $# -gt 0 ]] || need_value --artifact-root; artifact_root="$1" ;;
        --artifact-root=*) artifact_root="${1#*=}" ;;
        --preset) shift; [[ $# -gt 0 ]] || need_value --preset; preset="$1" ;;
        --preset=*) preset="${1#*=}" ;;
        --agent) shift; [[ $# -gt 0 ]] || need_value --agent; agent="$1" ;;
        --agent=*) agent="${1#*=}" ;;
        --model) shift; [[ $# -gt 0 ]] || need_value --model; model="$1" ;;
        --model=*) model="${1#*=}" ;;
        --effort) shift; [[ $# -gt 0 ]] || need_value --effort; effort="$1" ;;
        --effort=*) effort="${1#*=}" ;;
        --gate-mode) shift; [[ $# -gt 0 ]] || need_value --gate-mode; gate_mode="$1" ;;
        --gate-mode=*) gate_mode="${1#*=}" ;;
        --allow-dirty) allow_dirty=true ;;
        --skip-baseline) skip_baseline=true ;;
        --skip-build) skip_build=true ;;
        --skip-test) skip_test=true ;;
        --no-branch) no_branch=true ;;
        --no-commit) no_commit=true ;;
        --continue-on-failure) continue_on_failure=true ;;
        --revert-on-failure) revert_on_failure=true ;;
        --list) is_list=true ;;
        --list-files) is_list_files=true ;;
        --dry-run) is_dry_run=true ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        -*) printf 'Error: unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
        *) printf 'Error: unexpected argument: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

luma_validate_agent_and_effort

case "$gate_mode" in
    per-target|per-file|off) ;;
    *)
        printf 'Error: invalid --gate-mode %s (use per-target, per-file, or off).\n' "$gate_mode" >&2
        exit 2
        ;;
esac

# max_files: 0 means no cap; any provided value must be a positive integer.
case "$max_files" in
    ''|*[!0-9]*)
        printf 'Error: --max-files must be a positive integer.\n' >&2
        exit 2
        ;;
esac
max_files=$(( 10#$max_files ))

repo_root="$(luma_repo_root "$SCRIPT_DIR")"

# The runner's own files are excluded from enumeration so a session never edits a
# script that is currently executing (editing a running bash script corrupts it).
# The pipeline scripts live at a fixed in-repo location.
self_exclude=(
    'scripts/pipeline/luma-conformance.sh'
    'scripts/pipeline/luma-pipeline.sh'
)

records=()
while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    records+=("$line")
done < <(conformance_targets)

# Select the targets to run: prefer an exact id match per filter (so "shell"
# selects only the shell target, not "powershell" as a substring), else fall
# back to a substring match over "id name". De-duplicated, order preserved.
selected=()
selected_ids=' '
add_selected() {
    local rec="$1" id="$2"
    case "$selected_ids" in
        *" $id "*) return 0 ;;
    esac
    selected+=("$rec")
    selected_ids="$selected_ids$id "
}

if [[ ${#filters[@]} -gt 0 ]]; then
    for filter in "${filters[@]}"; do
        lc_filter="$(printf '%s' "$filter" | tr '[:upper:]' '[:lower:]')"
        found_exact=false
        for rec in "${records[@]}"; do
            IFS=$'\t' read -r rid _rname _rest <<< "$rec"
            if [[ "$rid" == "$lc_filter" ]]; then
                add_selected "$rec" "$rid"
                found_exact=true
            fi
        done
        if [[ "$found_exact" == true ]]; then
            continue
        fi
        matched=false
        for rec in "${records[@]}"; do
            IFS=$'\t' read -r rid rname _rest <<< "$rec"
            haystack="$(printf '%s %s' "$rid" "$rname" | tr '[:upper:]' '[:lower:]')"
            case "$haystack" in
                *"$lc_filter"*)
                    add_selected "$rec" "$rid"
                    matched=true
                    ;;
            esac
        done
        if [[ "$matched" != true ]]; then
            printf "Error: no target matched '%s'. Use --list to see the available targets.\n" "$filter" >&2
            exit 2
        fi
    done
else
    selected=("${records[@]}")
fi

if [[ "$is_list" == true ]]; then
    printf '%-6s %-16s %-26s %-6s %-6s %s\n' \
        'Order' 'Id' 'Name' 'Files' 'Gated' 'Instructions'
    order=0
    for rec in "${selected[@]}"; do
        order=$((order + 1))
        IFS=$'\t' read -r id name pattern gate _verify instructions <<< "$rec"
        count="$(list_target_files "$pattern" | wc -l | tr -d '[:space:]')"
        gated='no'
        [[ "$gate" == 1 ]] && gated='yes'
        instr_csv="$(printf '%s' "$instructions" | tr ' ' ',')"
        printf '%-6s %-16s %-26s %-6s %-6s %s\n' \
            "$order" "$id" "$name" "$count" "$gated" "$instr_csv"
    done
    exit 0
fi

if [[ "$is_list_files" == true ]]; then
    for rec in "${selected[@]}"; do
        IFS=$'\t' read -r id name pattern _gate _verify _instructions <<< "$rec"
        files=()
        while IFS= read -r f; do
            [[ -n "$f" ]] && files+=("$f")
        done < <(list_target_files "$pattern")
        luma_write_banner "$name [$id]" "${#files[@]} file(s)"
        for f in ${files[@]+"${files[@]}"}; do
            printf '  %s\n' "$f"
        done
    done
    exit 0
fi

if [[ -z "$artifact_root" ]]; then
    artifact_root="$repo_root/pipeline-artifacts"
fi
# Make the artifact root absolute so it can be reliably excluded from commits
# regardless of the caller's working directory.
case "$artifact_root" in
    /*) ;;
    *) artifact_root="$PWD/$artifact_root" ;;
esac

timestamp="$(date +%Y%m%d-%H%M%S)"
run_dir="$artifact_root/conformance-$timestamp"
log_dir="$run_dir/logs"
if [[ "$is_dry_run" != true ]]; then
    mkdir -p "$log_dir"
fi

total=${#selected[@]}

# A baseline is only meaningful when a build-gated target (C++, CMake) is in the
# selection and the gate is not disabled.
needs_baseline=false
if [[ "$gate_mode" != off ]]; then
    for rec in "${selected[@]}"; do
        IFS=$'\t' read -r _id _name _pattern gate _rest <<< "$rec"
        if [[ "$gate" == 1 ]]; then
            needs_baseline=true
            break
        fi
    done
fi

selected_ids_display="${selected_ids# }"
selected_ids_display="${selected_ids_display% }"
selected_ids_display="$(printf '%s' "$selected_ids_display" | tr ' ' ',')"
if [[ -z "$selected_ids_display" ]]; then
    # No --target filter: every target is selected.
    for rec in "${selected[@]}"; do
        IFS=$'\t' read -r id _rest <<< "$rec"
        if [[ -z "$selected_ids_display" ]]; then
            selected_ids_display="$id"
        else
            selected_ids_display="$selected_ids_display,$id"
        fi
    done
fi

luma_write_banner "Luma conformance pipeline (mutating, gated)" "Repo: $repo_root"
printf '  Targets  : %s (%s)\n' "$total" "$selected_ids_display"
printf '  Preset   : %s\n' "$preset"
printf '  Agent    : %s\n' "$agent"
printf '  Model    : %s\n' "${model:-(agent default)}"
printf '  Effort   : %s\n' "${effort:-(agent default)}"
printf '  GateMode : %s\n' "$gate_mode"
printf '  Output   : %s\n' "$run_dir"
if [[ "$is_dry_run" == true ]]; then
    printf '%s  Mode     : DRY RUN (nothing invoked, built, or committed)%s\n' \
        "$LUMA_CLR_WARN" "$LUMA_CLR_OFF"
fi

# --- Safety: clean tree, dedicated branch, green baseline ---------------------

if [[ "$allow_dirty" != true ]]; then
    if ! luma_clean_working_tree "$repo_root" "$is_dry_run"; then
        printf 'Error: working tree is not clean. Commit or stash your changes, or pass --allow-dirty.\n' >&2
        exit 1
    fi
fi

branch_name="pipeline/conformance-$timestamp"
if [[ "$no_branch" != true ]]; then
    printf 'Creating branch: %s\n' "$branch_name"
    if [[ "$is_dry_run" == true ]]; then
        printf '%s  [dry-run] git -C %s checkout -b %s%s\n' \
            "$LUMA_CLR_DIM" "$repo_root" "$branch_name" "$LUMA_CLR_OFF"
    else
        if ! git -C "$repo_root" checkout -b "$branch_name"; then
            printf 'Error: failed to create branch %s.\n' "$branch_name" >&2
            exit 1
        fi
    fi
fi

if [[ "$needs_baseline" == true && "$skip_baseline" != true ]]; then
    luma_write_banner "Baseline gate" "Verifying a green build + test before making changes"
    if ! luma_build_and_test "$repo_root" "$preset" "$skip_build" "$skip_test" "$is_dry_run"; then
        if [[ "$is_dry_run" != true ]]; then
            printf 'Error: baseline build + test is not green. Fix the baseline first, or pass --skip-baseline.\n' >&2
            exit 1
        fi
    fi
fi

# Capture the run's starting commit so a --no-commit per-target revert has a
# sensible fallback (there are no per-target checkpoints to reset to).
run_start_sha=''
if [[ "$is_dry_run" != true ]]; then
    run_start_sha="$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || true)"
fi

# --- Run targets, file by file ------------------------------------------------

results=()
aborted=false
position=0

for rec in "${selected[@]}"; do
    [[ "$aborted" == true ]] && break
    position=$((position + 1))
    IFS=$'\t' read -r id name pattern gate verify instructions <<< "$rec"

    files=()
    while IFS= read -r f; do
        [[ -n "$f" ]] && files+=("$f")
    done < <(list_target_files "$pattern")

    gate_this_target=false
    if [[ "$gate" == 1 && "$gate_mode" != off ]]; then
        gate_this_target=true
    fi
    gate_label='none'
    [[ "$gate_this_target" == true ]] && gate_label="$gate_mode"

    luma_write_banner "[$position/$total] $name" "${#files[@]} file(s)   gate: $gate_label"

    if [[ ${#files[@]} -eq 0 ]]; then
        printf '%s  No files matched for this target.%s\n' "$LUMA_CLR_DIM" "$LUMA_CLR_OFF"
        continue
    fi

    # Starting point for a per-target revert.
    target_start_sha="$run_start_sha"
    if [[ "$is_dry_run" != true ]]; then
        head_sha="$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || true)"
        [[ -n "$head_sha" ]] && target_start_sha="$head_sha"
    fi

    target_log_dir="$log_dir/$id"
    [[ "$is_dry_run" != true ]] && mkdir -p "$target_log_dir"

    fpos=0
    for rel in ${files[@]+"${files[@]}"}; do
        fpos=$((fpos + 1))
        printf '\n%s  [%s %s/%s] %s%s\n' \
            "$LUMA_CLR_BANNER" "$id" "$fpos" "${#files[@]}" "$rel" "$LUMA_CLR_OFF"

        instruction="$(build_conformance_instruction "$name" "$verify" "$rel" "$instructions")"
        # Nest the per-file log under the target, mirroring the source path so two
        # files with the same leaf name never collide.
        phase_log="$target_log_dir/$rel.log"
        [[ "$is_dry_run" != true ]] && mkdir -p "$(dirname "$phase_log")"

        status='ok'
        if luma_invoke_agent_phase "$agent" agent "$instruction" "$repo_root" \
            "$phase_log" "$target_log_dir" "$model" "$effort" "$is_dry_run"; then
            status='ok'
        else
            agent_rc=$?
            if [[ "$agent_rc" -eq 127 ]]; then
                status='error'
            else
                luma_warn "agent reported a non-zero exit for '$rel'."
                status='agent-failed'
            fi
        fi

        # Per-file gate (only when the target is build-affecting and gate-mode
        # is per-file).
        if [[ "$status" == ok && "$gate_this_target" == true && "$gate_mode" == per-file ]]; then
            if ! luma_build_and_test "$repo_root" "$preset" "$skip_build" "$skip_test" "$is_dry_run"; then
                if [[ "$is_dry_run" != true ]]; then
                    luma_warn "gate failed after '$rel'."
                    status='gate-failed'
                fi
            fi
        fi

        commit_sha='-'
        if [[ "$status" == ok && "$no_commit" != true ]]; then
            # A rejected commit (e.g. the pre-commit hook) must not look like
            # success. Mark it commit-failed so the failure handling below reverts
            # or stops, instead of silently re-staging the un-committable change
            # into every later checkpoint (git add -A) and accumulating a pile of
            # uncommitted edits reported as "ok | -".
            if sha="$(luma_git_checkpoint "$repo_root" "chore(conformance): $id $rel" "$agent" "$artifact_root" "$is_dry_run")"; then
                [[ -n "$sha" ]] && commit_sha="$sha"
            else
                luma_warn "checkpoint commit failed for '$rel' (a pre-commit hook or git rejected it)."
                status='commit-failed'
            fi
        fi

        if [[ "$status" != ok ]]; then
            if [[ "$revert_on_failure" == true ]]; then
                printf '%s    Reverting uncommitted changes to HEAD...%s\n' \
                    "$LUMA_CLR_WARN" "$LUMA_CLR_OFF"
                if [[ "$is_dry_run" == true ]]; then
                    printf '%s    [dry-run] git -C %s reset --hard HEAD%s\n' \
                        "$LUMA_CLR_DIM" "$repo_root" "$LUMA_CLR_OFF"
                else
                    git -C "$repo_root" reset --hard HEAD || luma_warn 'revert failed.'
                fi
            fi
            luma_should_abort "$status" "$continue_on_failure" "$revert_on_failure" && aborted=true
        fi

        results+=("$id|$rel|$status|$commit_sha")

        if [[ "$aborted" == true ]]; then
            if [[ "$status" == commit-failed && "$continue_on_failure" == true ]]; then
                luma_warn "stopping after '$rel': the commit was rejected and cannot be skipped (git add -A would re-stage it). Fix the blocker (e.g. the pre-commit hook), or pass --revert-on-failure to discard failed files and keep going."
            else
                luma_warn "stopping after '$rel' (pass --continue-on-failure to keep going)."
            fi
            break
        fi
    done

    [[ "$aborted" == true ]] && break

    # Per-target gate: one build + test after all of the target's files.
    if [[ "$gate_this_target" == true && "$gate_mode" == per-target ]]; then
        luma_write_banner "Gate: $name" "Build + test after target"
        if ! luma_build_and_test "$repo_root" "$preset" "$skip_build" "$skip_test" "$is_dry_run"; then
            if [[ "$is_dry_run" != true ]]; then
                luma_warn "per-target gate failed for '$id'."
                if [[ "$revert_on_failure" == true && -n "$target_start_sha" ]]; then
                    printf '%s  Reverting target to %s...%s\n' \
                        "$LUMA_CLR_WARN" "${target_start_sha:0:9}" "$LUMA_CLR_OFF"
                    git -C "$repo_root" reset --hard "$target_start_sha" || luma_warn 'target revert failed.'
                fi
                results+=("$id|(per-target gate)|gate-failed|-")
                if [[ "$continue_on_failure" != true ]]; then
                    aborted=true
                    luma_warn "stopping after the '$id' gate (pass --continue-on-failure to keep going)."
                fi
            fi
        fi
    fi
done

# --- Summary ------------------------------------------------------------------

if [[ "$is_dry_run" != true ]]; then
    summary_file="$run_dir/SUMMARY.md"
    {
        printf '# Luma conformance run\n\n'
        printf -- '- Generated: %s\n' "$timestamp"
        printf -- '- Repository: %s\n' "$repo_root"
        if [[ "$no_branch" != true ]]; then
            printf -- '- Branch: %s\n' "$branch_name"
        fi
        printf -- '- Agent: %s   Model: %s   Effort: %s\n' \
            "$agent" "${model:-(default)}" "${effort:-(default)}"
        printf -- '- Gate mode: %s\n' "$gate_mode"
        printf '\n| Target | File | Status | Commit |\n'
        printf '| ------ | ---- | ------ | ------ |\n'
        for r in ${results[@]+"${results[@]}"}; do
            IFS='|' read -r -a row <<< "$r"
            printf '| %s | %s | %s | %s |\n' \
                "${row[0]}" "${row[1]}" "${row[2]}" "${row[3]}"
        done
        if [[ "$aborted" == true ]]; then
            printf '\nThe run stopped early on a failure.\n'
        else
            printf '\nAll selected files were processed.\n'
        fi
    } > "$summary_file"
    luma_ok "Summary written: $summary_file"
fi

luma_write_banner "Conformance summary"
if [[ "$no_branch" != true && "$is_dry_run" != true ]]; then
    printf 'Branch: %s\n' "$branch_name"
fi

count_ok=0
count_agent_failed=0
count_gate_failed=0
count_commit_failed=0
count_error=0
count_total=0
for r in ${results[@]+"${results[@]}"}; do
    IFS='|' read -r -a row <<< "$r"
    count_total=$((count_total + 1))
    case "${row[2]}" in
        ok) count_ok=$((count_ok + 1)) ;;
        agent-failed) count_agent_failed=$((count_agent_failed + 1)) ;;
        gate-failed) count_gate_failed=$((count_gate_failed + 1)) ;;
        commit-failed) count_commit_failed=$((count_commit_failed + 1)) ;;
        error) count_error=$((count_error + 1)) ;;
    esac
done
[[ "$count_agent_failed" -gt 0 ]] && printf '  %-14s %s\n' 'agent-failed' "$count_agent_failed"
[[ "$count_commit_failed" -gt 0 ]] && printf '  %-14s %s\n' 'commit-failed' "$count_commit_failed"
[[ "$count_error" -gt 0 ]] && printf '  %-14s %s\n' 'error' "$count_error"
[[ "$count_gate_failed" -gt 0 ]] && printf '  %-14s %s\n' 'gate-failed' "$count_gate_failed"
[[ "$count_ok" -gt 0 ]] && printf '  %-14s %s\n' 'ok' "$count_ok"
printf '  %-14s %s\n' 'total' "$count_total"

# Exit non-zero when the run aborted or any file failed - including a rejected
# checkpoint commit (commit-failed) - so luma-all.sh and CI see the failure
# instead of reading an aborted run as a clean pass. Derive failures as every
# non-ok row (total - ok) rather than summing the named counters, so a newly
# added status counts as a failure automatically and this stays in step with
# luma-fix.sh's "row[3] != ok" check.
run_failures=$((count_total - count_ok))
if [[ "$aborted" == true ]]; then
    printf '%sRun stopped early on a failure.%s\n' "$LUMA_CLR_WARN" "$LUMA_CLR_OFF"
    exit 1
fi
if [[ "$run_failures" -gt 0 ]]; then
    printf '%sCompleted with %d failed file(s).%s\n' "$LUMA_CLR_WARN" "$run_failures" "$LUMA_CLR_OFF"
    exit 1
fi
luma_ok 'All selected files were processed.'
exit 0
