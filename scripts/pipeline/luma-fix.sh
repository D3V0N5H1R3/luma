#!/usr/bin/env bash
#
# luma-fix.sh - mutating, gated pass of the Luma prompt pipeline.
#
# Runs the fixer prompts (bug fixes, refactor, optimize, cleanup, iterative
# improvement, release verification, learnings capture) through the selected
# agent CLI (Copilot or Claude) in agent mode. This is the bash counterpart of
# Invoke-LumaFix.ps1 and keeps the same safety model:
#
#   * a clean working tree is required before starting (unless --allow-dirty);
#   * a green build + test baseline is established first (unless --skip-baseline);
#   * work happens on a dedicated pipeline/fix-<stamp> branch (unless --no-branch);
#   * after each phase the build + test gate runs, then formatters are applied and
#     the lint checks (including clang-tidy) must pass, and only a green,
#     lint-clean tree is committed (unless --no-commit / --skip-lint-format);
#   * the run stops at the first red gate (unless --continue-on-failure).
#
# Feed it the reports produced by luma-audit.sh (auto-detected from the newest
# audit-* run, or pass --report-dir explicitly).

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/pipeline/luma-pipeline.sh
# shellcheck disable=SC1091
source "$SCRIPT_DIR/luma-pipeline.sh"

usage() {
    cat <<'USAGE'
Usage: luma-fix.sh [options]

Run the mutating (agent-mode) fix phases of the Luma prompt pipeline, gated on a
green build + test after every phase. Work happens on a dedicated branch and is
committed phase by phase.

Options:
  --phase <filter>            Only run phases whose id or name contains <filter>.
                              Repeatable, and comma-separated values are accepted.
  --report-dir <dir>          Audit reports directory (the reports/ folder of an
                              audit run). Defaults to the newest
                              <artifact-root>/audit-*/reports.
  --artifact-root <dir>       Base directory for run output
                              (default: <repo>/pipeline-artifacts).
  --preset <name>             CMake preset for the gate (default: default).
  --convergence-max-passes <n>  Passes for iterative-improvement (1-10, default 3).
  --agent <name>              Agent CLI backend: copilot (default) or claude.
  --model <name>              Value for the agent's model flag.
  --effort <level>            Value for the agent's effort flag (low, medium,
                              high, xhigh, max).
  --allow-dirty               Do not require a clean working tree before starting.
  --skip-baseline             Skip the initial green build + test baseline.
  --skip-build                Skip the build step of every gate (test only).
  --skip-test                 Skip the test step of every gate (build only).
  --skip-lint-format          Skip the lint/format gate (scripts/format.py apply
                              + scripts/lint.py verify, including clang-tidy) at
                              the baseline and after every phase.
  --no-branch                 Run on the current branch instead of a new one.
  --no-commit                 Do not commit after each green phase.
  --continue-on-failure       Keep going after a red gate instead of stopping.
  --revert-on-failure         On a red gate, git reset --hard to the last
                              checkpoint. This discards tracked changes only;
                              untracked files a failed phase created are left in
                              place (run git clean yourself to remove them). With
                              --no-commit there are no checkpoints, so the reset
                              falls back to the branch's starting point and
                              discards every phase's work so far.
  --list                      List the fix phases in order and exit.
  --dry-run                   Print every command without executing it.
  -h, --help                  Show this help and exit.

Examples:
  luma-fix.sh --list
  luma-fix.sh --dry-run
  luma-fix.sh --phase bug-fix
USAGE
}

# Build the mutating instruction for a single fix phase.
build_fix_instruction() {
    local name="$1"
    local prompt="$2"
    local report_path="$3"
    local is_iterative="$4"
    local max_passes="$5"
    local enforce_lint="$6"

    printf 'You are running the Luma project fix phase: "%s".\n\n' "$name"

    if [[ -n "$report_path" ]]; then
        cat <<EOF
A prioritised audit report for this phase is available at:
    $report_path
Read it first and work through its findings in priority order (highest severity times confidence first). Skip any finding you cannot confirm against the current source.

EOF
    fi

    cat <<EOF
Follow the workflow in the prompt file ".github/prompts/$prompt" exactly - its ground rules, procedure, verification, and Output Format sections all apply.

Obey every relevant guide under instructions/ for the languages you touch. Make the smallest correct change for each item, add or update the matching regression test, and keep the full test suite green.
EOF

    if [[ "$enforce_lint" == true ]]; then
        cat <<'EOF'

This phase is gated on a clean lint result. After your primary changes, run `python scripts/format.py` to apply formatting and the safe auto-fixes, then run `python scripts/lint.py` and fix every issue it reports - including clang-tidy, shellcheck, cmakelint, tsc, and clippy findings - repeating until it passes. Correct the underlying code rather than suppressing the warnings. The pipeline re-runs format.py then lint.py as a gate and will not commit this phase unless both pass.
EOF
    fi

    if [[ "$is_iterative" == true ]]; then
        printf '\nPerform at most %s full improvement passes. After the final pass, stop and summarise any issues that remain.\n' \
            "$max_passes"
    fi

    printf '\nDo not push to any remote. Do not amend, rebase, or rewrite existing commits. Leave staging to the pipeline unless the prompt says otherwise.\n'
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

# Render a 1/0 boolean field as yes/no for the --list table.
yesno() {
    if [[ "$1" == 1 ]]; then
        printf 'yes'
    else
        printf 'no'
    fi
}

report_dir=''
artifact_root=''
preset='default'
convergence_max_passes=3
agent='copilot'
model=''
effort=''
allow_dirty=false
skip_baseline=false
skip_build=false
skip_test=false
skip_lint_format=false
no_branch=false
no_commit=false
continue_on_failure=false
revert_on_failure=false
is_list=false
is_dry_run=false
filters=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --phase) shift; [[ $# -gt 0 ]] || need_value --phase; add_filters "$1" ;;
        --phase=*) add_filters "${1#*=}" ;;
        --report-dir) shift; [[ $# -gt 0 ]] || need_value --report-dir; report_dir="$1" ;;
        --report-dir=*) report_dir="${1#*=}" ;;
        --artifact-root) shift; [[ $# -gt 0 ]] || need_value --artifact-root; artifact_root="$1" ;;
        --artifact-root=*) artifact_root="${1#*=}" ;;
        --preset) shift; [[ $# -gt 0 ]] || need_value --preset; preset="$1" ;;
        --preset=*) preset="${1#*=}" ;;
        --convergence-max-passes)
            shift; [[ $# -gt 0 ]] || need_value --convergence-max-passes
            convergence_max_passes="$1" ;;
        --convergence-max-passes=*) convergence_max_passes="${1#*=}" ;;
        --agent) shift; [[ $# -gt 0 ]] || need_value --agent; agent="$1" ;;
        --agent=*) agent="${1#*=}" ;;
        --model) shift; [[ $# -gt 0 ]] || need_value --model; model="$1" ;;
        --model=*) model="${1#*=}" ;;
        --effort) shift; [[ $# -gt 0 ]] || need_value --effort; effort="$1" ;;
        --effort=*) effort="${1#*=}" ;;
        --allow-dirty) allow_dirty=true ;;
        --skip-baseline) skip_baseline=true ;;
        --skip-build) skip_build=true ;;
        --skip-test) skip_test=true ;;
        --skip-lint-format) skip_lint_format=true ;;
        --no-branch) no_branch=true ;;
        --no-commit) no_commit=true ;;
        --continue-on-failure) continue_on_failure=true ;;
        --revert-on-failure) revert_on_failure=true ;;
        --list) is_list=true ;;
        --dry-run) is_dry_run=true ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        -*) printf 'Error: unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
        *) printf 'Error: unexpected argument: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

luma_validate_agent_and_effort

case "$convergence_max_passes" in
    ''|*[!0-9]*)
        printf 'Error: --convergence-max-passes must be an integer between 1 and 10.\n' >&2
        exit 2
        ;;
esac
# Force base-10 so a leading-zero value (e.g. 08 or 010) is not misread as octal
# by the arithmetic evaluation, matching the PowerShell [int] decimal parse. The
# digit-only guard above guarantees every character here is a decimal digit.
if (( 10#$convergence_max_passes < 1 || 10#$convergence_max_passes > 10 )); then
    printf 'Error: --convergence-max-passes must be between 1 and 10.\n' >&2
    exit 2
fi
# Normalise away any leading zeros so the value echoed into the prompt matches
# PowerShell's [int] rendering (e.g. 010 -> 10).
convergence_max_passes=$(( 10#$convergence_max_passes ))

repo_root="$(luma_repo_root "$SCRIPT_DIR")"

records=()
while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    records+=("$line")
done < <(luma_fix_phases)

if [[ "$is_list" == true ]]; then
    printf '%-6s %-26s %-34s %-6s %-8s %-8s %s\n' \
        "Order" "Id" "Name" "Gate" "Commit" "Verify" "Prompt"
    order=0
    for rec in "${records[@]}"; do
        order=$((order + 1))
        IFS='|' read -r -a fields <<< "$rec"
        printf '%-6s %-26s %-34s %-6s %-8s %-8s %s\n' \
            "$order" "${fields[0]}" "${fields[1]}" \
            "$(yesno "${fields[4]}")" "$(yesno "${fields[5]}")" \
            "$(yesno "${fields[6]}")" "${fields[2]}"
    done
    exit 0
fi

selected=()
if [[ ${#filters[@]} -gt 0 ]]; then
    for rec in "${records[@]}"; do
        IFS='|' read -r -a fields <<< "$rec"
        haystack="$(printf '%s %s' "${fields[0]}" "${fields[1]}" | tr '[:upper:]' '[:lower:]')"
        for filter in "${filters[@]}"; do
            needle="$(printf '%s' "$filter" | tr '[:upper:]' '[:lower:]')"
            if [[ "$haystack" == *"$needle"* ]]; then
                selected+=("$rec")
                break
            fi
        done
    done
    if [[ ${#selected[@]} -eq 0 ]]; then
        printf 'Error: no fix phase matched: %s. Use --list to see the phases.\n' \
            "${filters[*]}" >&2
        exit 2
    fi
else
    selected=("${records[@]}")
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

# Auto-detect the newest audit reports directory when one was not given.
if [[ -z "$report_dir" && -d "$artifact_root" ]]; then
    newest=''
    for candidate in "$artifact_root"/audit-*/; do
        [[ -d "$candidate" ]] || continue
        newest="$candidate"
    done
    if [[ -n "$newest" && -d "${newest%/}/reports" ]]; then
        report_dir="${newest%/}/reports"
    fi
fi

if [[ -n "$report_dir" ]]; then
    printf 'Audit reports: %s\n' "$report_dir"
else
    printf '%sAudit reports: none found - phases will self-discover findings.%s\n' \
        "$LUMA_CLR_WARN" "$LUMA_CLR_OFF"
fi

timestamp="$(date +%Y%m%d-%H%M%S)"
run_dir="$artifact_root/fix-$timestamp"
log_dir="$run_dir/logs"
if [[ "$is_dry_run" != true ]]; then
    mkdir -p "$log_dir"
fi

total=${#selected[@]}

luma_write_banner "Luma fix pipeline (mutating, gated)" "Repo: $repo_root"
printf '  Phases  : %s\n' "$total"
printf '  Preset  : %s\n' "$preset"
printf '  Agent   : %s\n' "$agent"
printf '  Output  : %s\n' "$run_dir"
if [[ "$is_dry_run" == true ]]; then
    printf '%s  Mode    : DRY RUN (nothing invoked, built, or committed)%s\n' \
        "$LUMA_CLR_WARN" "$LUMA_CLR_OFF"
fi

# --- Safety: clean tree, dedicated branch, green baseline ---------------------

if [[ "$allow_dirty" != true ]]; then
    if ! luma_clean_working_tree "$repo_root" "$is_dry_run"; then
        printf 'Error: working tree is not clean. Commit or stash your changes, or pass --allow-dirty.\n' >&2
        exit 1
    fi
fi

branch_name="pipeline/fix-$timestamp"
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

if [[ "$skip_baseline" != true ]]; then
    luma_write_banner "Baseline gate" "Verifying a green build + test before making changes"
    if ! luma_build_and_test "$repo_root" "$preset" "$skip_build" "$skip_test" "$is_dry_run"; then
        if [[ "$is_dry_run" != true ]]; then
            printf 'Error: baseline build + test is not green. Fix the baseline first, or pass --skip-baseline.\n' >&2
            exit 1
        fi
    fi
    if [[ "$skip_lint_format" != true ]]; then
        # Check-only: verify the starting tree is already lint-clean without
        # mutating it, so a per-phase lint failure is attributable to that phase
        # rather than to pre-existing drift.
        if ! luma_lint_and_format "$repo_root" true "$is_dry_run"; then
            if [[ "$is_dry_run" != true ]]; then
                printf 'Error: baseline lint is not clean. Run "python scripts/format.py" and fix any remaining lint issues, or pass --skip-lint-format.\n' >&2
                exit 1
            fi
        fi
    fi
fi

# --- Run phases ---------------------------------------------------------------

results=()
position=0
aborted=false

for rec in "${selected[@]}"; do
    position=$((position + 1))
    IFS='|' read -r -a fields <<< "$rec"
    id="${fields[0]}"
    name="${fields[1]}"
    prompt="${fields[2]}"
    input_report="${fields[3]:-}"
    gate="${fields[4]}"
    commit="${fields[5]}"
    self_verifies="${fields[6]}"

    luma_write_banner "[$position/$total] $name" "Prompt: $prompt"

    # Resolve the input report, if the audit produced one.
    report_clause=''
    if [[ -n "$report_dir" && -n "$input_report" ]]; then
        report_path="$report_dir/$input_report"
        if [[ -f "$report_path" ]]; then
            report_clause="$report_path"
        fi
    fi

    is_iterative=false
    if [[ "$id" == *iterative-improvement* ]]; then
        is_iterative=true
    fi

    # Instruct the agent to fix lint findings (incl. clang-tidy) whenever this
    # phase will be lint-gated below, so the deterministic gate only has to
    # verify. Mirrors the gate condition: gated, not self-verifying, not skipped.
    enforce_lint=false
    if [[ "$gate" == 1 && "$self_verifies" == 0 && "$skip_lint_format" != true ]]; then
        enforce_lint=true
    fi

    instruction="$(build_fix_instruction \
        "$name" "$prompt" "$report_clause" "$is_iterative" "$convergence_max_passes" "$enforce_lint")"

    # The release-verification prompt wipes the workspace with `git clean -Xdf`,
    # which would delete the git-ignored pipeline-artifacts/ directory holding
    # this run's logs. Protect it deterministically: stream this phase's
    # transcript to a temp dir *outside* the artifact tree (on Windows a directory
    # cannot be moved while a file inside it is open, which would otherwise defeat
    # the set-aside), and move the artifact root into .git/ for the phase's
    # duration. The set-aside no-ops for a dry run or an out-of-tree artifact root.
    protect_clean=false
    phase_log_dir="$log_dir"
    if [[ "$is_dry_run" != true && "$id" == *release-verification* ]]; then
        protect_clean=true
        phase_log_dir="$(mktemp -d "${TMPDIR:-/tmp}/luma-relverify-XXXXXX")"
    fi
    phase_log="$phase_log_dir/$id.log"

    artifact_backup=''
    if [[ "$protect_clean" == true ]]; then
        artifact_backup="$(luma_save_pipeline_artifacts "$artifact_root" "$repo_root")"
        # Guarantee the set-aside is undone even if this phase is interrupted or an
        # unexpected error trips `set -e` before the explicit restore below - the
        # bash counterpart of the PowerShell runner's `finally`. EXIT runs the
        # restore; INT/TERM convert the signal into a normal exit so it fires too.
        trap 'luma_restore_pipeline_artifacts "$artifact_backup" "$artifact_root" 2>/dev/null || true' EXIT
        trap 'exit 130' INT
        trap 'exit 143' TERM
    fi

    if luma_invoke_agent_phase "$agent" agent "$instruction" "$repo_root" \
        "$phase_log" "$phase_log_dir" "$model" "$effort" "$is_dry_run"; then
        status='ok'
    else
        # Exit 127 means the agent CLI was not found (the helper already warned);
        # treat that infrastructure failure as 'error' to match the PowerShell
        # runner. Any other non-zero means the agent ran and failed its task.
        agent_rc=$?
        if [[ "$agent_rc" -eq 127 ]]; then
            status='error'
        else
            luma_warn "agent reported a non-zero exit for '$id'."
            status='agent-failed'
        fi
    fi

    # Put the artifact root back, then fold this phase's temp transcript(s) into
    # the run's log dir so SUMMARY.md links them exactly as for any other phase.
    # Runs regardless of the phase's status so nothing is stranded in .git/.
    if [[ "$protect_clean" == true ]]; then
        luma_restore_pipeline_artifacts "$artifact_backup" "$artifact_root" || \
            luma_warn "failed to restore $artifact_root after '$id'."
        trap - EXIT INT TERM   # window closed - drop the safety-net trap
        mkdir -p "$log_dir"
        if [[ -d "$phase_log_dir" ]]; then
            find "$phase_log_dir" -mindepth 1 -maxdepth 1 -exec mv -f {} "$log_dir/" \; || true
            rmdir "$phase_log_dir" 2>/dev/null || true
        fi
    fi

    # Build + test gate (unless the prompt self-verifies or the phase opts out).
    if [[ "$status" == ok && "$gate" == 1 && "$self_verifies" == 0 ]]; then
        if ! luma_build_and_test "$repo_root" "$preset" "$skip_build" "$skip_test" "$is_dry_run"; then
            if [[ "$is_dry_run" != true ]]; then
                luma_warn "gate failed after '$id'."
                status='gate-failed'
            fi
        fi

        # Lint/format gate: apply formatters, then verify lint (incl. clang-tidy).
        # Runs after build + test so clang-tidy sees the freshest compile database
        # and a broken build fails fast first. Any formatting changes fold into
        # this phase's commit below.
        if [[ "$status" == ok && "$skip_lint_format" != true ]]; then
            if ! luma_lint_and_format "$repo_root" false "$is_dry_run"; then
                if [[ "$is_dry_run" != true ]]; then
                    luma_warn "lint/format gate failed after '$id'."
                    status='gate-failed'
                fi
            fi
        fi
    fi

    # Commit a green phase.
    commit_sha='-'
    if [[ "$status" == ok && "$commit" == 1 && "$no_commit" != true ]]; then
        # A rejected commit (e.g. the pre-commit hook) must not look like success.
        # Mark it commit-failed so the failure handling below reverts or stops,
        # instead of silently re-staging the un-committable change into every later
        # checkpoint (git add -A) and accumulating uncommitted edits reported "ok".
        if sha="$(luma_git_checkpoint "$repo_root" "chore(pipeline): $id" "$agent" "$artifact_root" "$is_dry_run")"; then
            [[ -n "$sha" ]] && commit_sha="$sha"
        else
            luma_warn "checkpoint commit failed for '$id' (a pre-commit hook or git rejected it)."
            status='commit-failed'
        fi
    fi

    # Handle failure: optional revert, optional stop.
    if [[ "$status" != ok ]]; then
        if [[ "$revert_on_failure" == true ]]; then
            printf '%sReverting working tree to last checkpoint...%s\n' \
                "$LUMA_CLR_WARN" "$LUMA_CLR_OFF"
            if [[ "$is_dry_run" == true ]]; then
                printf '%s  [dry-run] git -C %s reset --hard HEAD%s\n' \
                    "$LUMA_CLR_DIM" "$repo_root" "$LUMA_CLR_OFF"
            else
                # Best-effort: a failed reset must warn, not abort the run under
                # set -e, so the summary and any remaining phases still run. This
                # mirrors the exit-code check on the PowerShell revert.
                git -C "$repo_root" reset --hard HEAD || luma_warn 'revert failed.'
            fi
        fi
        if luma_should_abort "$status" "$continue_on_failure" "$revert_on_failure"; then
            aborted=true
        fi
    fi

    results+=("$position|$id|$name|$status|$commit_sha")

    if [[ "$aborted" == true ]]; then
        if [[ "$status" == commit-failed && "$continue_on_failure" == true ]]; then
            luma_warn "stopping after '$id': the commit was rejected and cannot be skipped (git add -A would re-stage it). Fix the blocker (e.g. the pre-commit hook), or pass --revert-on-failure to discard failed phases and keep going."
        else
            luma_warn "stopping after '$id' (pass --continue-on-failure to keep going)."
        fi
        break
    fi
done

# --- Summary ------------------------------------------------------------------

# Persist a run summary (mirrors the audit runner's INDEX.md) so a completed run
# is auditable from disk even when the console scrollback is lost. A phase whose
# Commit is "-" recorded no checkpoint of its own; if the tree was dirty that
# work can land in a later phase's commit, so a "-" next to an ok status is worth
# a look.
if [[ "$is_dry_run" != true ]]; then
    summary_file="$run_dir/SUMMARY.md"
    {
        printf '# Luma fix run\n\n'
        printf -- '- Generated: %s\n' "$timestamp"
        printf -- '- Repository: %s\n' "$repo_root"
        if [[ "$no_branch" != true ]]; then
            printf -- '- Branch: %s\n' "$branch_name"
        fi
        printf '\n| Order | Id | Phase | Status | Commit | Log |\n'
        printf '| ----- | -- | ----- | ------ | ------ | --- |\n'
        for r in ${results[@]+"${results[@]}"}; do
            IFS='|' read -r -a row <<< "$r"
            # Link the phase's transcript when it exists so the summary doubles as
            # a navigation index; a phase that failed before the agent ran (e.g. a
            # missing CLI) records no log, so guard the link on the file.
            phase_id="${row[1]}"
            if [[ -f "$log_dir/$phase_id.log" ]]; then
                log_cell="[$phase_id.log](logs/$phase_id.log)"
            else
                log_cell='-'
            fi
            printf '| %s | %s | %s | %s | %s | %s |\n' \
                "${row[0]}" "${row[1]}" "${row[2]}" "${row[3]}" "${row[4]}" "$log_cell"
        done
        if [[ "$aborted" == true ]]; then
            printf '\nThe pipeline stopped early on a failed phase.\n'
        else
            printf '\nAll selected phases completed.\n'
        fi
        # Backticks are literal Markdown here, not a command substitution.
        # shellcheck disable=SC2016
        printf '\nA `-` in the Commit column means the phase created no commit of its own; a `-` in the Log column means no transcript was captured.\n'
    } > "$summary_file"
    luma_ok "Summary written: $summary_file"
fi

luma_write_banner "Fix summary"
if [[ "$no_branch" != true && "$is_dry_run" != true ]]; then
    printf 'Branch: %s\n' "$branch_name"
fi
printf '%-6s %-26s %-14s %s\n' "Order" "Id" "Status" "Commit"
for r in ${results[@]+"${results[@]}"}; do
    IFS='|' read -r -a row <<< "$r"
    printf '%-6s %-26s %-14s %s\n' "${row[0]}" "${row[1]}" "${row[3]}" "${row[4]}"
done

run_failures=0
for r in ${results[@]+"${results[@]}"}; do
    IFS='|' read -r -a row <<< "$r"
    [[ "${row[3]}" != ok ]] && run_failures=$((run_failures + 1))
done

# Exit non-zero when the pipeline aborted or any phase failed - including a
# rejected checkpoint commit (commit-failed) - so luma-all.sh and CI see the
# failure instead of reading an aborted run as a clean pass.
if [[ "$aborted" == true ]]; then
    printf '%sPipeline stopped early on a failed phase.%s\n' \
        "$LUMA_CLR_WARN" "$LUMA_CLR_OFF"
    exit 1
fi
if [[ "$run_failures" -gt 0 ]]; then
    printf '%sCompleted with %d failed phase(s).%s\n' \
        "$LUMA_CLR_WARN" "$run_failures" "$LUMA_CLR_OFF"
    exit 1
fi
luma_ok 'All selected phases completed.'
exit 0
