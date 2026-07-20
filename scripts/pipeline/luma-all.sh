#!/usr/bin/env bash
#
# luma-all.sh - run the audit then the fix pipeline back-to-back.
#
# Thin orchestrator that invokes the pipeline entry points in order:
#
#   1. luma-audit.sh        (read-only, plan mode) - produces ranked reports.
#   2. luma-fix.sh          (mutating, agent mode) - applies gated fixes.
#   3. luma-conformance.sh  (mutating, agent mode) - per-file conformance pass,
#      only when --include-conformance is given.
#
# Defaults to the requested configuration: the GitHub Copilot CLI backend
# driving Claude Opus 4.8 (model 'claude-opus-4.8') at 'max' reasoning effort.
# Every knob is forwarded to each runner. The audit runs first; if it exits
# non-zero the remaining phases are skipped. The conformance phase is opt-in
# (--include-conformance): it walks every tracked file of each selected type and
# runs one agent session per file, so a full run is expensive - scope it with
# --conformance-arg (for example --conformance-arg --target=cpp). This is the
# bash counterpart of Invoke-LumaAll.ps1 and targets bash 3.2 (the stock
# macOS shell).

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<'USAGE'
Usage: luma-all.sh [options] [-- passthrough-args...]

Run the read-only audit and then the mutating fix pipeline in sequence through
one agent CLI, and optionally the per-file conformance pass afterwards. Defaults
to the Copilot CLI driving Claude Opus 4.8 at max effort.

Options:
  --agent <name>          Agent CLI backend: copilot (default) or claude.
  --model <name>          Model for the agent's --model flag (default: claude-opus-4.8).
                          Pass an empty string to let the agent choose.
  --effort <level>        Reasoning effort (default: max). One of low, medium, high,
                          xhigh, max for copilot; pass an empty string to omit.
  --skip-audit            Skip the audit phase.
  --skip-fix              Skip the fix phase.
  --include-conformance   Also run luma-conformance.sh as a third phase, after the
                          fix. Opt-in rather than a --skip- flag: a full run is one
                          agent session per tracked file (900+ for C++ alone), so it
                          stays off by default. Scope it with --conformance-arg
                          (e.g. --conformance-arg --target=cpp).
  --dry-run               Preview every runner without invoking, building, or committing.
  --audit-arg <arg>       Extra argument forwarded to luma-audit.sh (repeatable).
  --fix-arg <arg>         Extra argument forwarded to luma-fix.sh (repeatable).
  --conformance-arg <arg> Extra argument forwarded to luma-conformance.sh
                          (repeatable; only used with --include-conformance).
  -h, --help              Show this help and exit.

Anything after a literal -- is forwarded to EVERY runner.

Examples:
  luma-all.sh --dry-run
  luma-all.sh
  luma-all.sh --fix-arg --revert-on-failure
  luma-all.sh --include-conformance --conformance-arg --target=cpp
USAGE
}

need_value() {
    printf 'Error: %s requires a value.\n' "$1" >&2
    exit 2
}

agent='copilot'
model='claude-opus-4.8'
effort='max'
skip_audit=false
skip_fix=false
include_conformance=false
dry_run=false
audit_extra=()
fix_extra=()
conformance_extra=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --agent) shift; [[ $# -gt 0 ]] || need_value --agent; agent="$1" ;;
        --agent=*) agent="${1#*=}" ;;
        --model) shift; [[ $# -gt 0 ]] || need_value --model; model="$1" ;;
        --model=*) model="${1#*=}" ;;
        --effort) shift; [[ $# -gt 0 ]] || need_value --effort; effort="$1" ;;
        --effort=*) effort="${1#*=}" ;;
        --skip-audit) skip_audit=true ;;
        --skip-fix) skip_fix=true ;;
        --include-conformance) include_conformance=true ;;
        --dry-run) dry_run=true ;;
        --audit-arg) shift; [[ $# -gt 0 ]] || need_value --audit-arg; audit_extra+=("$1") ;;
        --audit-arg=*) audit_extra+=("${1#*=}") ;;
        --fix-arg) shift; [[ $# -gt 0 ]] || need_value --fix-arg; fix_extra+=("$1") ;;
        --fix-arg=*) fix_extra+=("${1#*=}") ;;
        --conformance-arg) shift; [[ $# -gt 0 ]] || need_value --conformance-arg; conformance_extra+=("$1") ;;
        --conformance-arg=*) conformance_extra+=("${1#*=}") ;;
        --) shift; break ;;
        -h|--help) usage; exit 0 ;;
        *)
            printf 'Error: unknown option %s\n\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

# Anything left after a literal -- is forwarded to every runner.
passthrough=("$@")

case "$agent" in
    copilot|claude) ;;
    *)
        printf 'Error: invalid --agent %s (use copilot or claude).\n' "$agent" >&2
        exit 2
        ;;
esac

if [[ "$skip_audit" == true && "$skip_fix" == true && "$include_conformance" != true ]]; then
    printf 'Error: with --skip-audit and --skip-fix you must also pass --include-conformance.\n' >&2
    exit 2
fi

for runner in luma-audit.sh luma-fix.sh luma-conformance.sh; do
    if [[ ! -f "$SCRIPT_DIR/$runner" ]]; then
        printf 'Error: runner not found: %s\n' "$SCRIPT_DIR/$runner" >&2
        exit 2
    fi
done

# Common agent flags forwarded to every runner.
common_args=(--agent "$agent")
if [[ -n "$model" ]]; then common_args+=(--model "$model"); fi
if [[ -n "$effort" ]]; then common_args+=(--effort "$effort"); fi
if [[ "$dry_run" == true ]]; then common_args+=(--dry-run); fi

# Minimal, opt-out colour. Disabled when stdout is not a TTY or NO_COLOR is set.
if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
    clr_banner=$'\033[36m'; clr_warn=$'\033[33m'; clr_dim=$'\033[90m'; clr_off=$'\033[0m'
else
    clr_banner=''; clr_warn=''; clr_dim=''; clr_off=''
fi

banner() {
    local rule
    printf -v rule '%*s' 72 ''
    rule="${rule// /=}"
    printf '\n%s%s%s\n' "$clr_banner" "$rule" "$clr_off"
    printf '  %s\n' "$1"
    printf '%s%s%s\n' "$clr_banner" "$rule" "$clr_off"
}

# Run one runner with the common flags, any global passthrough, and its own
# per-phase extras. All array expansions are guarded for bash 3.2 + set -u.
run_runner() {
    local script="$1"; shift
    bash "$SCRIPT_DIR/$script" \
        "${common_args[@]}" \
        ${passthrough[@]+"${passthrough[@]}"} \
        "$@"
}

[[ -n "$model" ]] && model_label="$model" || model_label='(agent default)'
[[ -n "$effort" ]] && effort_label="$effort" || effort_label='(agent default)'
banner "Luma audit + fix pipeline  |  agent: $agent  model: $model_label  effort: $effort_label"
if [[ "$dry_run" == true ]]; then
    printf '%s  Mode : DRY RUN (nothing invoked, built, or committed)%s\n' "$clr_warn" "$clr_off"
fi

# Number the banners over exactly the phases that will run, so a skipped phase or
# an added conformance phase keeps the "Phase i/N" labels accurate.
total_phases=0
[[ "$skip_audit" != true ]] && total_phases=$((total_phases + 1))
[[ "$skip_fix" != true ]] && total_phases=$((total_phases + 1))
[[ "$include_conformance" == true ]] && total_phases=$((total_phases + 1))
phase_index=0

# Exit code of the last phase actually run; becomes this orchestrator's own.
final_rc=0

if [[ "$skip_audit" != true ]]; then
    phase_index=$((phase_index + 1))
    banner "Phase $phase_index/$total_phases - Audit (read-only)"
    audit_rc=0
    set +e
    run_runner luma-audit.sh ${audit_extra[@]+"${audit_extra[@]}"}
    audit_rc=$?
    set -e
    if [[ "$audit_rc" -ne 0 ]]; then
        printf '%sAudit runner exited with code %d; skipping the remaining phases.%s\n' \
            "$clr_warn" "$audit_rc" "$clr_off" >&2
        exit "$audit_rc"
    fi
else
    printf '%sSkipping audit phase (--skip-audit).%s\n' "$clr_dim" "$clr_off"
fi

if [[ "$skip_fix" != true ]]; then
    phase_index=$((phase_index + 1))
    banner "Phase $phase_index/$total_phases - Fix (mutating, gated)"
    fix_rc=0
    set +e
    run_runner luma-fix.sh ${fix_extra[@]+"${fix_extra[@]}"}
    fix_rc=$?
    set -e
    final_rc=$fix_rc
    if [[ "$fix_rc" -ne 0 ]]; then
        printf '%sFix runner exited with code %d.%s\n' "$clr_warn" "$fix_rc" "$clr_off" >&2
        if [[ "$include_conformance" == true ]]; then
            printf '%sSkipping the conformance phase because the fix phase failed.%s\n' \
                "$clr_warn" "$clr_off" >&2
        fi
        banner 'Pipeline complete'
        exit "$fix_rc"
    fi
else
    printf '%sSkipping fix phase (--skip-fix).%s\n' "$clr_dim" "$clr_off"
fi

if [[ "$include_conformance" == true ]]; then
    phase_index=$((phase_index + 1))
    banner "Phase $phase_index/$total_phases - Conformance (mutating, gated)"
    conformance_rc=0
    set +e
    run_runner luma-conformance.sh ${conformance_extra[@]+"${conformance_extra[@]}"}
    conformance_rc=$?
    set -e
    final_rc=$conformance_rc
    if [[ "$conformance_rc" -ne 0 ]]; then
        printf '%sConformance runner exited with code %d.%s\n' \
            "$clr_warn" "$conformance_rc" "$clr_off" >&2
    fi
else
    printf '%sSkipping conformance phase (pass --include-conformance to enable).%s\n' \
        "$clr_dim" "$clr_off"
fi

banner 'Pipeline complete'
exit "$final_rc"
