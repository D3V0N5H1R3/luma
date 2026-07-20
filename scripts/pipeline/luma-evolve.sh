#!/usr/bin/env bash
#
# luma-evolve.sh - mutating, gated language-evolution pass of the Luma pipeline.
#
# Implements new language capabilities through the selected agent CLI (Copilot
# or Claude) in agent mode. Each candidate is a (kind, goal) pair; the kind
# routes to the implementer prompt that builds it:
#
#     function -> new-stdlib-function.prompt.md
#     type     -> new-stdlib-type.prompt.md
#     module   -> new-stdlib-module.prompt.md
#     feature  -> new-language-feature.prompt.md
#
# Candidates come from --goal + --kind (a single candidate) or a --goals-file
# queue (one 'kind|goal' line per candidate). Routing is deterministic - the
# kind selects the prompt - so a candidate is never misrouted by parsing agent
# output. This is the bash counterpart of Invoke-LumaEvolve.ps1 and keeps the
# same safety model as luma-fix.sh:
#
#   * a clean working tree is required before starting (unless --allow-dirty);
#   * a green build + test baseline is established first (unless --skip-baseline);
#   * work happens on a dedicated pipeline/evolve-<stamp> branch (unless
#     --no-branch);
#   * after each candidate the build + test gate runs, then formatters are
#     applied and the lint checks (including clang-tidy) must pass, and only a
#     green, lint-clean tree is committed (unless --no-commit / --skip-lint-format);
#   * the run stops at the first red gate (unless --continue-on-failure).
#
# This is the evolve half of the discover -> evolve pipeline; triage the report
# luma-discover.sh produces, then feed chosen candidates here.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/pipeline/luma-pipeline.sh
# shellcheck disable=SC1091
source "$SCRIPT_DIR/luma-pipeline.sh"

usage() {
    cat <<'USAGE'
Usage: luma-evolve.sh [options]

Implement new language capabilities through the agent CLI, gated on a green
build + test after each candidate. Work happens on a dedicated branch and is
committed candidate by candidate.

Candidate input (choose one):
  --goal <text>              The candidate to implement, in prose. Requires --kind.
  --kind <kind>              Kind of --goal: function, type, module, or feature.
  --goals-file <path>        A queue of candidates, one 'kind|goal' line each.
                             Blank lines and lines starting with '#' are ignored.

Options:
  --artifact-root <dir>      Base directory for run output
                             (default: <repo>/pipeline-artifacts).
  --preset <name>            CMake preset for the gate (default: default).
  --agent <name>             Agent CLI backend: copilot (default) or claude.
  --model <name>             Value for the agent's model flag
                             (default: claude-opus-4.8; '' lets the agent choose).
  --effort <level>           Value for the agent's effort flag (low, medium,
                             high, xhigh, max; default: max; '' omits it).
  --allow-dirty              Do not require a clean working tree before starting.
  --skip-baseline            Skip the initial green build + test baseline.
  --skip-build               Skip the build step of every gate (test only).
  --skip-test                Skip the test step of every gate (build only).
  --skip-lint-format         Skip the lint/format gate (scripts/format.py apply
                             + scripts/lint.py verify, including clang-tidy) at
                             the baseline and after every candidate.
  --no-branch                Run on the current branch instead of a new one.
  --no-commit                Do not commit after each green candidate.
  --continue-on-failure      Keep going after a red gate instead of stopping.
  --revert-on-failure        On a red gate, git reset --hard to the last
                             checkpoint. This discards tracked changes only;
                             untracked files a failed candidate created are left
                             in place (run git clean yourself to remove them).
                             With --no-commit there are no checkpoints, so the
                             reset falls back to the branch's starting point and
                             discards every candidate's work so far.
  --list-kinds               List the evolution kinds and their prompts and exit.
  --dry-run                  Print every command without executing it.
  -h, --help                 Show this help and exit.

Examples:
  luma-evolve.sh --list-kinds
  luma-evolve.sh --goal 'Add String.center' --kind function --dry-run
  luma-evolve.sh --goals-file candidates.txt
USAGE
}

# Build the mutating instruction for a single evolution candidate.
build_evolve_instruction() {
    local name="$1"
    local prompt="$2"
    local goal="$3"
    local enforce_lint="$4"

    cat <<EOF
You are evolving the Luma language by implementing a new $name.

Goal:
    $goal

Follow the workflow in the prompt file ".github/prompts/$prompt" exactly - its ground rules, procedure across every interpreter phase, verification, and Output Format sections all apply.

Obey every relevant guide under instructions/ (and documents/ for Luma) for the languages you touch. Implement the goal completely, add or update the matching tests and documentation, and keep the full build + test suite green.
EOF

    if [[ "$enforce_lint" == true ]]; then
        cat <<'EOF'

This work is gated on a clean lint result. After your primary changes, run `python scripts/format.py` to apply formatting and the safe auto-fixes, then run `python scripts/lint.py` and fix every issue it reports - including clang-tidy, shellcheck, cmakelint, tsc, and clippy findings - repeating until it passes. Correct the underlying code rather than suppressing the warnings. The pipeline re-runs format.py then lint.py as a gate and will not commit this candidate unless both pass.
EOF
    fi

    printf '\nDo not push to any remote. Do not amend, rebase, or rewrite existing commits. Leave staging to the pipeline unless the prompt says otherwise.\n'
}

need_value() {
    printf 'Error: %s requires a value.\n' "$1" >&2
    exit 2
}

# Trim leading and trailing whitespace from $1 and echo the result.
trim() {
    local s="$1"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "$s"
}

goal_arg=''
kind_arg=''
goals_file=''
artifact_root=''
preset='default'
agent='copilot'
model='claude-opus-4.8'
effort='max'
allow_dirty=false
skip_baseline=false
skip_build=false
skip_test=false
skip_lint_format=false
no_branch=false
no_commit=false
continue_on_failure=false
revert_on_failure=false
is_list_kinds=false
is_dry_run=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --goal) shift; [[ $# -gt 0 ]] || need_value --goal; goal_arg="$1" ;;
        --goal=*) goal_arg="${1#*=}" ;;
        --kind) shift; [[ $# -gt 0 ]] || need_value --kind; kind_arg="$1" ;;
        --kind=*) kind_arg="${1#*=}" ;;
        --goals-file) shift; [[ $# -gt 0 ]] || need_value --goals-file; goals_file="$1" ;;
        --goals-file=*) goals_file="${1#*=}" ;;
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
        --allow-dirty) allow_dirty=true ;;
        --skip-baseline) skip_baseline=true ;;
        --skip-build) skip_build=true ;;
        --skip-test) skip_test=true ;;
        --skip-lint-format) skip_lint_format=true ;;
        --no-branch) no_branch=true ;;
        --no-commit) no_commit=true ;;
        --continue-on-failure) continue_on_failure=true ;;
        --revert-on-failure) revert_on_failure=true ;;
        --list-kinds) is_list_kinds=true ;;
        --dry-run) is_dry_run=true ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        -*) printf 'Error: unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
        *) printf 'Error: unexpected argument: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

# Load the kind -> (name, prompt) routing table, keeping a CSV of valid kinds for
# error messages. Kept in lock-step with Get-EvolveKind in LumaPipeline.psm1.
declare -A kind_name kind_prompt
valid_kinds_csv=''
while IFS='|' read -r k n p; do
    [[ -n "$k" ]] || continue
    kind_name["$k"]="$n"
    kind_prompt["$k"]="$p"
    if [[ -z "$valid_kinds_csv" ]]; then
        valid_kinds_csv="$k"
    else
        valid_kinds_csv="$valid_kinds_csv, $k"
    fi
done < <(luma_evolve_kinds)

if [[ "$is_list_kinds" == true ]]; then
    printf '%-10s %-22s %s\n' "Kind" "Name" "Prompt"
    while IFS='|' read -r k n p; do
        [[ -n "$k" ]] || continue
        printf '%-10s %-22s %s\n' "$k" "$n" "$p"
    done < <(luma_evolve_kinds)
    exit 0
fi

# Resolve a raw kind to its canonical key. Echoes the key on success; prints an
# error and returns 1 on failure so the caller can exit at top level (an exit
# inside the `$(...)` capture would only leave the subshell).
resolve_kind() {
    local raw="$1" ctx="$2" key
    key="$(printf '%s' "$raw" | tr '[:upper:]' '[:lower:]')"
    key="$(trim "$key")"
    if [[ -z "${kind_name[$key]+x}" ]]; then
        printf 'Error: unknown kind '\''%s'\''%s. Valid kinds: %s (see --list-kinds).\n' \
            "$raw" "$ctx" "$valid_kinds_csv" >&2
        return 1
    fi
    printf '%s' "$key"
}

luma_validate_agent_and_effort

repo_root="$(luma_repo_root "$SCRIPT_DIR")"

# --- Build the candidate list (deterministic kind -> prompt routing) ----------
# Each record is TAB-delimited (key<TAB>name<TAB>prompt<TAB>goal); a goal is free
# text that may contain '|', so a tab keeps the fields unambiguous.
using_file=false
[[ -n "$goals_file" ]] && using_file=true
using_goal=false
[[ -n "$goal_arg" || -n "$kind_arg" ]] && using_goal=true
if [[ "$using_file" == true && "$using_goal" == true ]]; then
    printf 'Error: use either --goals-file or --goal/--kind, not both.\n' >&2
    exit 2
fi

candidates=()
if [[ "$using_file" == true ]]; then
    if [[ ! -f "$goals_file" ]]; then
        printf 'Error: goals file not found: %s\n' "$goals_file" >&2
        exit 2
    fi
    lineno=0
    while IFS= read -r raw || [[ -n "$raw" ]]; do
        lineno=$((lineno + 1))
        line="$(trim "$raw")"
        [[ -z "$line" || "${line:0:1}" == '#' ]] && continue
        if [[ "$line" != *'|'* ]]; then
            printf "Error: malformed line %s in %s: expected 'kind|goal', got '%s'.\n" \
                "$lineno" "$goals_file" "$raw" >&2
            exit 2
        fi
        rawkind="$(trim "${line%%|*}")"
        goal="$(trim "${line#*|}")"
        if [[ -z "$goal" ]]; then
            printf 'Error: malformed line %s in %s: empty goal.\n' "$lineno" "$goals_file" >&2
            exit 2
        fi
        if ! key="$(resolve_kind "$rawkind" " on line $lineno of $goals_file")"; then
            exit 2
        fi
        candidates+=("$key"$'\t'"${kind_name[$key]}"$'\t'"${kind_prompt[$key]}"$'\t'"$goal")
    done < "$goals_file"
    if [[ ${#candidates[@]} -eq 0 ]]; then
        printf "Error: no candidates found in %s (expected 'kind|goal' lines).\n" "$goals_file" >&2
        exit 2
    fi
elif [[ -n "$goal_arg" ]]; then
    if [[ -z "$kind_arg" ]]; then
        printf 'Error: provide --kind with --goal (function, type, module, or feature). Use --list-kinds to see the kinds.\n' >&2
        exit 2
    fi
    if ! key="$(resolve_kind "$kind_arg" '')"; then
        exit 2
    fi
    candidates+=("$key"$'\t'"${kind_name[$key]}"$'\t'"${kind_prompt[$key]}"$'\t'"$(trim "$goal_arg")")
else
    printf 'Error: nothing to do. Provide --goal + --kind, or --goals-file. Use --list-kinds to see the kinds.\n' >&2
    exit 2
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
run_dir="$artifact_root/evolve-$timestamp"
log_dir="$run_dir/logs"
if [[ "$is_dry_run" != true ]]; then
    mkdir -p "$log_dir"
fi

total=${#candidates[@]}

luma_write_banner "Luma evolve pipeline (mutating, gated)" "Repo: $repo_root"
printf '  Candidates : %s\n' "$total"
printf '  Preset     : %s\n' "$preset"
printf '  Agent      : %s\n' "$agent"
printf '  Model      : %s\n' "${model:-(agent default)}"
printf '  Effort     : %s\n' "${effort:-(agent default)}"
printf '  Output     : %s\n' "$run_dir"
if [[ "$is_dry_run" == true ]]; then
    printf '%s  Mode       : DRY RUN (nothing invoked, built, or committed)%s\n' \
        "$LUMA_CLR_WARN" "$LUMA_CLR_OFF"
fi

# --- Safety: clean tree, dedicated branch, green baseline ---------------------

if [[ "$allow_dirty" != true ]]; then
    if ! luma_clean_working_tree "$repo_root" "$is_dry_run"; then
        printf 'Error: working tree is not clean. Commit or stash your changes, or pass --allow-dirty.\n' >&2
        exit 1
    fi
fi

branch_name="pipeline/evolve-$timestamp"
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
        # mutating it, so a per-candidate lint failure is attributable to that
        # candidate rather than to pre-existing drift.
        if ! luma_lint_and_format "$repo_root" true "$is_dry_run"; then
            if [[ "$is_dry_run" != true ]]; then
                printf 'Error: baseline lint is not clean. Run "python scripts/format.py" and fix any remaining lint issues, or pass --skip-lint-format.\n' >&2
                exit 1
            fi
        fi
    fi
fi

# --- Run candidates -----------------------------------------------------------

results=()
position=0
aborted=false

for rec in "${candidates[@]}"; do
    position=$((position + 1))
    IFS=$'\t' read -r key name prompt goal <<< "$rec"
    id="$(printf '%02d-%s' "$position" "$key")"

    # A subject-safe, single-line, length-capped goal for the checkpoint commit.
    short_goal="$(printf '%s' "$goal" | tr -s '[:space:]' ' ')"
    short_goal="$(trim "$short_goal")"
    if (( ${#short_goal} > 60 )); then
        short_goal="${short_goal:0:57}..."
    fi

    luma_write_banner "[$position/$total] $name" "Prompt: $prompt"
    printf '  Goal : %s\n' "$goal"

    # Every candidate is build- and lint-gated, so instruct the agent to fix lint
    # findings whenever the lint gate is enabled (mirrors the gate below).
    enforce_lint=false
    if [[ "$skip_lint_format" != true ]]; then
        enforce_lint=true
    fi

    instruction="$(build_evolve_instruction "$name" "$prompt" "$goal" "$enforce_lint")"

    phase_log="$log_dir/$id.log"

    if luma_invoke_agent_phase "$agent" agent "$instruction" "$repo_root" \
        "$phase_log" "$log_dir" "$model" "$effort" "$is_dry_run"; then
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

    # Build + test gate, then lint/format gate.
    if [[ "$status" == ok ]]; then
        if ! luma_build_and_test "$repo_root" "$preset" "$skip_build" "$skip_test" "$is_dry_run"; then
            if [[ "$is_dry_run" != true ]]; then
                luma_warn "gate failed after '$id'."
                status='gate-failed'
            fi
        fi

        if [[ "$status" == ok && "$skip_lint_format" != true ]]; then
            if ! luma_lint_and_format "$repo_root" false "$is_dry_run"; then
                if [[ "$is_dry_run" != true ]]; then
                    luma_warn "lint/format gate failed after '$id'."
                    status='gate-failed'
                fi
            fi
        fi
    fi

    # Commit a green candidate.
    commit_sha='-'
    if [[ "$status" == ok && "$no_commit" != true ]]; then
        # A rejected commit (e.g. the pre-commit hook) must not look like success.
        # Mark it commit-failed so the failure handling below reverts or stops,
        # instead of re-staging the un-committable change into every later
        # checkpoint (git add -A) and accumulating uncommitted edits reported "ok".
        if sha="$(luma_git_checkpoint "$repo_root" "chore(evolve): $key - $short_goal" "$agent" "$artifact_root" "$is_dry_run")"; then
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
                # set -e, so the summary and any remaining candidates still run.
                git -C "$repo_root" reset --hard HEAD || luma_warn 'revert failed.'
            fi
        fi
        if luma_should_abort "$status" "$continue_on_failure" "$revert_on_failure"; then
            aborted=true
        fi
    fi

    results+=("$position"$'\t'"$id"$'\t'"$key"$'\t'"$goal"$'\t'"$status"$'\t'"$commit_sha")

    if [[ "$aborted" == true ]]; then
        if [[ "$status" == commit-failed && "$continue_on_failure" == true ]]; then
            luma_warn "stopping after '$id': the commit was rejected and cannot be skipped (git add -A would re-stage it). Fix the blocker (e.g. the pre-commit hook), or pass --revert-on-failure to discard failed candidates and keep going."
        else
            luma_warn "stopping after '$id' (pass --continue-on-failure to keep going)."
        fi
        break
    fi
done

# --- Summary ------------------------------------------------------------------

if [[ "$is_dry_run" != true ]]; then
    summary_file="$run_dir/SUMMARY.md"
    {
        printf '# Luma evolve run\n\n'
        printf -- '- Generated: %s\n' "$timestamp"
        printf -- '- Repository: %s\n' "$repo_root"
        if [[ "$no_branch" != true ]]; then
            printf -- '- Branch: %s\n' "$branch_name"
        fi
        printf '\n| Order | Id | Kind | Goal | Status | Commit | Log |\n'
        printf '| ----- | -- | ---- | ---- | ------ | ------ | --- |\n'
        for r in ${results[@]+"${results[@]}"}; do
            IFS=$'\t' read -r r_order r_id r_kind r_goal r_status r_commit <<< "$r"
            if [[ -f "$log_dir/$r_id.log" ]]; then
                log_cell="[$r_id.log](logs/$r_id.log)"
            else
                log_cell='-'
            fi
            # Keep the goal from breaking the table: collapse whitespace, escape pipes.
            goal_cell="$(printf '%s' "$r_goal" | tr -s '[:space:]' ' ' | sed 's/|/\\|/g')"
            goal_cell="$(trim "$goal_cell")"
            printf '| %s | %s | %s | %s | %s | %s | %s |\n' \
                "$r_order" "$r_id" "$r_kind" "$goal_cell" "$r_status" "$r_commit" "$log_cell"
        done
        if [[ "$aborted" == true ]]; then
            printf '\nThe pipeline stopped early on a failed candidate.\n'
        else
            printf '\nAll selected candidates completed.\n'
        fi
        # Backticks are literal Markdown here, not a command substitution.
        # shellcheck disable=SC2016
        printf '\nA `-` in the Commit column means the candidate created no commit of its own; a `-` in the Log column means no transcript was captured.\n'
    } > "$summary_file"
    luma_ok "Summary written: $summary_file"
fi

luma_write_banner "Evolve summary"
if [[ "$no_branch" != true && "$is_dry_run" != true ]]; then
    printf 'Branch: %s\n' "$branch_name"
fi
printf '%-6s %-16s %-10s %-14s %s\n' "Order" "Id" "Kind" "Status" "Commit"
for r in ${results[@]+"${results[@]}"}; do
    IFS=$'\t' read -r r_order r_id r_kind r_goal r_status r_commit <<< "$r"
    printf '%-6s %-16s %-10s %-14s %s\n' "$r_order" "$r_id" "$r_kind" "$r_status" "$r_commit"
done

run_failures=0
for r in ${results[@]+"${results[@]}"}; do
    IFS=$'\t' read -r r_order r_id r_kind r_goal r_status r_commit <<< "$r"
    [[ "$r_status" != ok ]] && run_failures=$((run_failures + 1))
done

# Exit non-zero when the pipeline aborted or any candidate failed - including a
# rejected checkpoint commit (commit-failed) - so callers and CI see the failure
# instead of reading an aborted run as a clean pass.
if [[ "$aborted" == true ]]; then
    printf '%sPipeline stopped early on a failed candidate.%s\n' \
        "$LUMA_CLR_WARN" "$LUMA_CLR_OFF"
    exit 1
fi
if [[ "$run_failures" -gt 0 ]]; then
    printf '%sCompleted with %d failed candidate(s).%s\n' \
        "$LUMA_CLR_WARN" "$run_failures" "$LUMA_CLR_OFF"
    exit 1
fi
luma_ok 'All selected candidates completed.'
exit 0
