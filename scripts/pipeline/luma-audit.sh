#!/usr/bin/env bash
#
# luma-audit.sh - read-only pass of the Luma prompt pipeline.
#
# Runs the audit prompts through the selected agent CLI in plan mode, so the
# agent analyses the codebase and writes a ranked Markdown report per phase
# without touching a single file. This is the bash counterpart of
# scripts/pipeline/Invoke-LumaAudit.ps1; the two are kept behaviourally in step.
#
# Nothing here mutates the repository: no builds, no commits, no edits. Triage
# the reports it produces, then run luma-fix.sh to apply changes.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/pipeline/luma-pipeline.sh
# shellcheck disable=SC1091
source "$SCRIPT_DIR/luma-pipeline.sh"

usage() {
    cat <<'USAGE'
Usage: luma-audit.sh [options]

Run the read-only (plan-mode) audit phases of the Luma prompt pipeline. Each
phase writes a ranked Markdown report under pipeline-artifacts/audit-<stamp>/.

Options:
  --scope <paths>        Override the analysis scope passed to every phase.
  --phase <filter>       Only run phases whose id or name contains <filter>.
                         Repeatable, and comma-separated values are accepted.
  --artifact-root <dir>  Base directory for run output
                         (default: <repo>/pipeline-artifacts).
  --agent <name>         Agent CLI backend: copilot (default) or claude.
  --model <name>         Value for the agent's model flag.
  --effort <level>       Value for the agent's effort flag. One of:
                         low, medium, high, xhigh, max.
  --list                 List the audit phases in order and exit.
  --dry-run              Print what would run without invoking the agent.
  -h, --help             Show this help and exit.

Examples:
  luma-audit.sh --list
  luma-audit.sh --dry-run
  luma-audit.sh --phase bug-search --scope core/runtime/vm/
USAGE
}

# Build the read-only instruction for a single audit phase.
build_audit_instruction() {
    local name="$1"
    local prompt="$2"
    local scope_clause="$3"
    cat <<EOF
You are running the Luma project audit: "$name".

Follow the workflow in the prompt file ".github/prompts/$prompt" exactly - its ground rules, what-to-look-for, prioritisation, exclusions, and Output Format sections all apply.

$scope_clause

This is a STRICTLY READ-ONLY analysis: do not modify, create, build, run, format, or stage any files. Read every source you cite and verify each location before reporting it.

Produce the complete, prioritised report exactly as that prompt's "Output Format" section specifies. Output only the report as your final message, with no preamble and no closing chatter, and do not wrap the report or any table within it in a Markdown code fence, so it can be saved verbatim as a Markdown file.
EOF
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

scope=''
artifact_root=''
agent='copilot'
model=''
effort=''
is_list=false
is_dry_run=false
filters=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --scope) shift; [[ $# -gt 0 ]] || need_value --scope; scope="$1" ;;
        --scope=*) scope="${1#*=}" ;;
        --phase) shift; [[ $# -gt 0 ]] || need_value --phase; add_filters "$1" ;;
        --phase=*) add_filters "${1#*=}" ;;
        --artifact-root) shift; [[ $# -gt 0 ]] || need_value --artifact-root; artifact_root="$1" ;;
        --artifact-root=*) artifact_root="${1#*=}" ;;
        --agent) shift; [[ $# -gt 0 ]] || need_value --agent; agent="$1" ;;
        --agent=*) agent="${1#*=}" ;;
        --model) shift; [[ $# -gt 0 ]] || need_value --model; model="$1" ;;
        --model=*) model="${1#*=}" ;;
        --effort) shift; [[ $# -gt 0 ]] || need_value --effort; effort="$1" ;;
        --effort=*) effort="${1#*=}" ;;
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
if [[ "$is_dry_run" != true ]]; then
    luma_require_agent "$agent"
fi

repo_root="$(luma_repo_root "$SCRIPT_DIR")"

records=()
while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    records+=("$line")
done < <(luma_audit_phases)

if [[ "$is_list" == true ]]; then
    printf '%-6s %-32s %-36s %s\n' "Order" "Id" "Name" "Prompt"
    order=0
    for rec in "${records[@]}"; do
        order=$((order + 1))
        IFS='|' read -r -a fields <<< "$rec"
        printf '%-6s %-32s %-36s %s\n' "$order" "${fields[0]}" "${fields[1]}" "${fields[2]}"
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
        printf 'Error: no audit phase matched: %s. Use --list to see the phases.\n' \
            "${filters[*]}" >&2
        exit 2
    fi
else
    selected=("${records[@]}")
fi

if [[ -z "$artifact_root" ]]; then
    artifact_root="$repo_root/pipeline-artifacts"
fi
# Make the artifact root absolute so output paths are CWD-independent, keeping
# the runners in lock-step (the PowerShell audit runner must absolutise because
# it writes each report after cd-ing into the repo root).
case "$artifact_root" in
    /*) ;;
    *) artifact_root="$PWD/$artifact_root" ;;
esac
timestamp="$(date +%Y%m%d-%H%M%S)"
run_dir="$artifact_root/audit-$timestamp"
report_dir="$run_dir/reports"
log_dir="$run_dir/logs"

if [[ "$is_dry_run" != true ]]; then
    mkdir -p "$report_dir" "$log_dir"
fi

total=${#selected[@]}

luma_write_banner "Luma audit pipeline (read-only)" "Repo: $repo_root"
printf '  Phases : %s\n' "$total"
printf '  Agent  : %s\n' "$agent"
printf '  Output : %s\n' "$run_dir"
if [[ "$is_dry_run" == true ]]; then
    printf '%s  Mode   : DRY RUN (no agent invoked)%s\n' "$LUMA_CLR_WARN" "$LUMA_CLR_OFF"
fi

results=()
position=0
for rec in "${selected[@]}"; do
    position=$((position + 1))
    IFS='|' read -r -a fields <<< "$rec"
    id="${fields[0]}"
    name="${fields[1]}"
    prompt="${fields[2]}"
    default_scope="${fields[3]:-}"

    if [[ -n "$scope" ]]; then
        effective_scope="$scope"
    else
        effective_scope="$default_scope"
    fi

    report_file="$report_dir/$id.md"
    luma_write_banner "[$position/$total] $name" "Prompt: $prompt"

    if [[ -n "$effective_scope" ]]; then
        scope_clause="Restrict the analysis to the following scope: $effective_scope."
    else
        scope_clause="Use the default scope defined by the prompt."
    fi

    instruction="$(build_audit_instruction "$name" "$prompt" "$scope_clause")"

    if luma_invoke_agent_phase "$agent" plan "$instruction" "$repo_root" \
        "$report_file" "$log_dir" "$model" "$effort" "$is_dry_run"; then
        status='ok'
    else
        # Exit 127 means the agent CLI was not found (the helper already warned);
        # match the PowerShell runner and label that infrastructure failure
        # 'error', reserving 'failed' for a phase the agent actually ran.
        agent_rc=$?
        if [[ "$agent_rc" -eq 127 ]]; then
            status='error'
        else
            luma_warn "phase '$id' failed"
            status='failed'
        fi
    fi
    results+=("$position|$id|$name|$status")
done

if [[ "$is_dry_run" != true ]]; then
    index_file="$run_dir/INDEX.md"
    {
        printf '# Luma audit run\n\n'
        printf -- '- Generated: %s\n' "$timestamp"
        printf -- '- Repository: %s\n\n' "$repo_root"
        printf '| Order | Phase | Status | Report |\n'
        printf '| ----- | ----- | ------ | ------ |\n'
        for r in ${results[@]+"${results[@]}"}; do
            IFS='|' read -r -a row <<< "$r"
            printf '| %s | %s | %s | [%s.md](reports/%s.md) |\n' \
                "${row[0]}" "${row[2]}" "${row[3]}" "${row[1]}" "${row[1]}"
        done
        # Backticks are literal Markdown for INDEX.md, not a command substitution.
        # shellcheck disable=SC2016
        printf '\nTriage these reports, then run `luma-fix.sh` to apply fixes.\n'
    } > "$index_file"
    printf '\n'
    luma_ok "Index written: $index_file"
fi

luma_write_banner "Audit summary"
printf '%-6s %-32s %s\n' "Order" "Id" "Status"
has_failures=0
for r in ${results[@]+"${results[@]}"}; do
    IFS='|' read -r -a row <<< "$r"
    printf '%-6s %-32s %s\n' "${row[0]}" "${row[1]}" "${row[3]}"
    case "${row[3]}" in
        error|failed) has_failures=1 ;;
    esac
done

if [[ "$has_failures" -ne 0 ]]; then
    printf '%sCompleted with one or more failed phases.%s\n' \
        "$LUMA_CLR_WARN" "$LUMA_CLR_OFF"
    exit 1
fi
luma_ok 'All selected phases completed.'
exit 0
