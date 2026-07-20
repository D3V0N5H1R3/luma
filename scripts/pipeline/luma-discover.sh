#!/usr/bin/env bash
#
# luma-discover.sh - read-only language-evolution discovery of the Luma pipeline.
#
# Runs the new-requirements prompt through the selected agent CLI in plan mode,
# so the agent surveys other languages and libraries and writes a ranked,
# routed Markdown report of candidate additions without touching a single file.
# This is the bash counterpart of scripts/pipeline/Invoke-LumaDiscover.ps1; the
# two are kept behaviourally in step.
#
# Nothing here mutates the repository: no builds, no commits, no edits. Triage
# the report it produces, then run luma-evolve.sh to implement chosen
# candidates. This is the discover -> evolve counterpart of audit -> fix.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/pipeline/luma-pipeline.sh
# shellcheck disable=SC1091
source "$SCRIPT_DIR/luma-pipeline.sh"

# The single read-only discovery prompt. Held as constants (not a phase table)
# because discovery is one prompt; the routed candidates it produces become the
# evolve runner's many phases.
DISCOVER_ID='new-requirements'
DISCOVER_NAME='Language-evolution discovery'
DISCOVER_PROMPT='new-requirements.prompt.md'

usage() {
    cat <<'USAGE'
Usage: luma-discover.sh [options]

Run the read-only (plan-mode) language-evolution discovery of the Luma prompt
pipeline. It writes a ranked, routed Markdown report of candidate additions
under pipeline-artifacts/discover-<stamp>/.

Options:
  --focus <area>         Focus the survey on an area (e.g. 'string handling').
                         Omit for a broad survey.
  --artifact-root <dir>  Base directory for run output
                         (default: <repo>/pipeline-artifacts).
  --agent <name>         Agent CLI backend: copilot (default) or claude.
  --model <name>         Value for the agent's model flag.
  --effort <level>       Value for the agent's effort flag. One of:
                         low, medium, high, xhigh, max.
  --dry-run              Print what would run without invoking the agent.
  -h, --help             Show this help and exit.

Examples:
  luma-discover.sh --dry-run
  luma-discover.sh --focus 'string handling'
  luma-discover.sh --agent claude --effort max
USAGE
}

# Build the read-only instruction for the discovery phase.
build_discover_instruction() {
    local name="$1"
    local prompt="$2"
    local focus_clause="$3"
    cat <<EOF
You are running the Luma language-evolution discovery: candidate additions that fit the language.

Follow the workflow in the prompt file ".github/prompts/$prompt" exactly - its ground rules, research method, philosophy-fit criteria, prioritisation, exclusions, and Output Format sections all apply.

$focus_clause

This is a STRICTLY READ-ONLY analysis: do not modify, create, build, run, format, or stage any files. Verify every claim about the current language and stdlib against the actual sources before reporting it.

Produce the complete, prioritised report exactly as that prompt's "Output Format" section specifies, including a Handoff line for each candidate naming its kind (function, type, module, or feature). Output only the report as your final message, with no preamble and no closing chatter, and do not wrap the report or any table within it in a Markdown code fence, so it can be saved verbatim as a Markdown file.

(This is running as "$name".)
EOF
}

need_value() {
    printf 'Error: %s requires a value.\n' "$1" >&2
    exit 2
}

focus=''
artifact_root=''
agent='copilot'
model=''
effort=''
is_dry_run=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --focus) shift; [[ $# -gt 0 ]] || need_value --focus; focus="$1" ;;
        --focus=*) focus="${1#*=}" ;;
        --artifact-root) shift; [[ $# -gt 0 ]] || need_value --artifact-root; artifact_root="$1" ;;
        --artifact-root=*) artifact_root="${1#*=}" ;;
        --agent) shift; [[ $# -gt 0 ]] || need_value --agent; agent="$1" ;;
        --agent=*) agent="${1#*=}" ;;
        --model) shift; [[ $# -gt 0 ]] || need_value --model; model="$1" ;;
        --model=*) model="${1#*=}" ;;
        --effort) shift; [[ $# -gt 0 ]] || need_value --effort; effort="$1" ;;
        --effort=*) effort="${1#*=}" ;;
        --dry-run) is_dry_run=true ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        -*) printf 'Error: unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
        *) printf 'Error: unexpected argument: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

luma_validate_agent_and_effort

repo_root="$(luma_repo_root "$SCRIPT_DIR")"

if [[ -z "$artifact_root" ]]; then
    artifact_root="$repo_root/pipeline-artifacts"
fi
# Make the artifact root absolute so output paths are CWD-independent, keeping
# the runners in lock-step (the PowerShell discover runner must absolutise
# because it writes the report after cd-ing into the repo root).
case "$artifact_root" in
    /*) ;;
    *) artifact_root="$PWD/$artifact_root" ;;
esac
timestamp="$(date +%Y%m%d-%H%M%S)"
run_dir="$artifact_root/discover-$timestamp"
report_dir="$run_dir/reports"
log_dir="$run_dir/logs"

if [[ "$is_dry_run" != true ]]; then
    mkdir -p "$report_dir" "$log_dir"
fi

luma_write_banner "Luma discover pipeline (read-only)" "Repo: $repo_root"
printf '  Agent  : %s\n' "$agent"
printf '  Focus  : %s\n' "${focus:-(broad survey)}"
printf '  Output : %s\n' "$run_dir"
if [[ "$is_dry_run" == true ]]; then
    printf '%s  Mode   : DRY RUN (no agent invoked)%s\n' "$LUMA_CLR_WARN" "$LUMA_CLR_OFF"
fi

report_file="$report_dir/$DISCOVER_ID.md"
luma_write_banner "$DISCOVER_NAME" "Prompt: $DISCOVER_PROMPT"

if [[ -n "$focus" ]]; then
    focus_clause="Focus the survey on: $focus."
else
    focus_clause="Survey broadly across the standard library and language surface; do not narrow to a single area."
fi

instruction="$(build_discover_instruction "$DISCOVER_NAME" "$DISCOVER_PROMPT" "$focus_clause")"

if luma_invoke_agent_phase "$agent" plan "$instruction" "$repo_root" \
    "$report_file" "$log_dir" "$model" "$effort" "$is_dry_run"; then
    status='ok'
else
    # Exit 127 means the agent CLI was not found (the helper already warned);
    # match the PowerShell runner and label that infrastructure failure 'error',
    # reserving 'failed' for a phase the agent actually ran.
    agent_rc=$?
    if [[ "$agent_rc" -eq 127 ]]; then
        status='error'
    else
        luma_warn "discovery failed"
        status='failed'
    fi
fi
results=("1|$DISCOVER_ID|$DISCOVER_NAME|$status")

if [[ "$is_dry_run" != true ]]; then
    index_file="$run_dir/INDEX.md"
    {
        printf '# Luma discover run\n\n'
        printf -- '- Generated: %s\n' "$timestamp"
        printf -- '- Repository: %s\n' "$repo_root"
        if [[ -n "$focus" ]]; then
            printf -- '- Focus: %s\n' "$focus"
        fi
        printf '\n| Order | Phase | Status | Report |\n'
        printf '| ----- | ----- | ------ | ------ |\n'
        for r in ${results[@]+"${results[@]}"}; do
            IFS='|' read -r -a row <<< "$r"
            printf '| %s | %s | %s | [%s.md](reports/%s.md) |\n' \
                "${row[0]}" "${row[2]}" "${row[3]}" "${row[1]}" "${row[1]}"
        done
        # Backticks are literal Markdown for INDEX.md, not a command substitution.
        # shellcheck disable=SC2016
        printf '\nTriage this report, then run `luma-evolve.sh` to implement chosen candidates.\n'
    } > "$index_file"
    printf '\n'
    luma_ok "Index written: $index_file"
fi

luma_write_banner "Discover summary"
printf '%-6s %-32s %s\n' "Order" "Id" "Status"
for r in ${results[@]+"${results[@]}"}; do
    IFS='|' read -r -a row <<< "$r"
    printf '%-6s %-32s %s\n' "${row[0]}" "${row[1]}" "${row[3]}"
done
