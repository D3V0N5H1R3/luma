#!/usr/bin/env python3
"""Run the Luma prompt pipeline: read-only audits, gated fixes, or both.

Drives the improvement prompts in ``.github/prompts/`` through an agent CLI —
the GitHub Copilot CLI (default) or the Claude Code CLI — in a fixed order:

* ``audit`` runs the read-only audit prompts and saves one ranked Markdown
  report per phase under ``pipeline-artifacts/audit-<timestamp>/``.
* ``fix`` applies the fixer prompts on a dedicated ``pipeline/fix-<timestamp>``
  branch, gating every phase on build + test + lint and committing a
  checkpoint only when the tree is green.
* ``all`` runs ``audit`` and then ``fix`` with the same agent settings.

Every side effect goes through :class:`pipeline_core.Pipeline`, so ``--dry-run``
previews the exact agent, ``cmake``, ``ctest``, and ``git`` commands without
running any of them. See ``scripts/pipeline/DIRECTORY.md`` for the safety model.

Usage:
    python scripts/pipeline/luma_pipeline.py audit [options]
    python scripts/pipeline/luma_pipeline.py fix [options]
    python scripts/pipeline/luma_pipeline.py all [options] [-- stage-args...]
"""

from __future__ import annotations

import argparse
import contextlib
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import TypeVar

from pipeline_core import (
    AGENTS,
    COPILOT_EFFORTS,
    CheckpointError,
    Console,
    Pipeline,
    PipelineError,
    Status,
    TerminationRequested,
    find_repo_root,
    require_agent,
    should_abort,
    termination_handlers,
)

# The pipeline is self-contained (it does not import scripts/_common.py), so it
# keeps its own copy of the shared version gate. Nothing above runs 3.10-only
# code, and this module avoids 3.10-only syntax, so the guard still fires with a
# clear message on an older interpreter.
if sys.version_info < (3, 10):  # noqa: UP036
    sys.exit("Error: Python 3.10 or later is required to run the Luma prompt pipeline.")

SCRIPT_DIR = Path(__file__).resolve().parent
SCRIPT_PATH = "scripts/pipeline/luma_pipeline.py"
DEFAULT_ALL_MODELS = {"copilot": "claude-opus-4.6", "claude": ""}
DEFAULT_ALL_EFFORT = "medium"
MAX_CONVERGENCE_PASSES = 10


# ── Phase tables ──────────────────────────────────────────────────


@dataclass(frozen=True)
class AuditPhase:
    """A read-only audit phase that produces one ranked report."""

    phase_id: str
    name: str
    prompt: str
    default_scope: str = ""


@dataclass(frozen=True)
class FixPhase:
    """A mutating fix phase.

    Attributes:
        input_report: Audit report file it consumes, if any.
        has_gate: Run the build + test + lint gate after the phase.
        should_commit: Checkpoint the phase when it leaves the tree green.
        self_verifies: The prompt builds and tests exhaustively itself, so the
            script-side gate is skipped.
        limits_passes: The prompt loops; cap it at ``--convergence-max-passes``.
        cleans_ignored_files: The prompt runs ``git clean -Xdf``, which would
            delete the git-ignored artifact root, so that root is set aside.
    """

    phase_id: str
    name: str
    prompt: str
    input_report: str = ""
    has_gate: bool = True
    should_commit: bool = True
    self_verifies: bool = False
    limits_passes: bool = False
    cleans_ignored_files: bool = False


AUDIT_PHASES = (
    AuditPhase("01-bug-search-core", "Bug search (interpreter + stdlib)", "bug-search.prompt.md"),
    AuditPhase("02-bug-search-debugger", "Bug search (debugger)", "bug-search-debugger.prompt.md"),
    AuditPhase(
        "03-bug-search-language-server",
        "Bug search (language server)",
        "bug-search-language-server.prompt.md",
    ),
    AuditPhase(
        "04-bug-search-editor-extension",
        "Bug search (editor extensions)",
        "bug-search-editor-extension.prompt.md",
    ),
    AuditPhase("05-consistency-check", "Consistency check", "consistency-check.prompt.md"),
    AuditPhase(
        "06-code-review",
        "Code review (deep net)",
        "code-review.prompt.md",
        "core/ shared/ language-server/source/ debugger/source/",
    ),
    AuditPhase("07-ux-audit", "UX audit", "ux-audit.prompt.md"),
    AuditPhase("08-refactor-audit", "Refactor audit", "refactor-audit.prompt.md"),
    AuditPhase("09-performance-audit", "Performance audit", "performance-audit.prompt.md"),
)

FIX_PHASES = (
    FixPhase(
        "bug-fix-core",
        "Bug fix (interpreter + stdlib)",
        "bug-fix.prompt.md",
        "01-bug-search-core.md",
    ),
    FixPhase(
        "bug-fix-debugger",
        "Bug fix (debugger)",
        "bug-fix-debugger.prompt.md",
        "02-bug-search-debugger.md",
    ),
    FixPhase(
        "bug-fix-language-server",
        "Bug fix (language server)",
        "bug-fix-language-server.prompt.md",
        "03-bug-search-language-server.md",
    ),
    FixPhase(
        "bug-fix-editor-extension",
        "Bug fix (editor extensions)",
        "bug-fix-editor-extension.prompt.md",
        "04-bug-search-editor-extension.md",
    ),
    FixPhase("refactor", "Refactor", "refactor.prompt.md", "08-refactor-audit.md"),
    FixPhase("optimize", "Optimize", "optimize.prompt.md", "09-performance-audit.md"),
    FixPhase("ux-improve", "UX improvement", "ux-improve.prompt.md", "07-ux-audit.md"),
    FixPhase(
        "iterative-improvement",
        "Iterative improvement (loop)",
        "iterative-improvement.prompt.md",
        has_gate=False,
        self_verifies=True,
        limits_passes=True,
    ),
    FixPhase(
        "release-verification",
        "Release verification (gate)",
        "release-verification.prompt.md",
        has_gate=False,
        should_commit=False,
        self_verifies=True,
        cleans_ignored_files=True,
    ),
    FixPhase("update-learnings", "Update learnings", "update-learnings.prompt.md", has_gate=False),
)

_LINT_DIRECTIVE = (
    "This phase is gated on a clean lint result. After your primary changes, run "
    "`python scripts/format.py` to apply formatting and the safe auto-fixes, then run "
    "`python scripts/lint.py` and fix every issue it reports - including clang-tidy, "
    "shellcheck, cmakelint, tsc, and clippy findings - repeating until it passes. Correct the "
    "underlying code rather than suppressing the warnings. The pipeline re-runs format.py then "
    "lint.py as a gate and will not commit this phase unless both pass."
)

PhaseT = TypeVar("PhaseT", AuditPhase, FixPhase)


def select_phases(phases: Sequence[PhaseT], filters: Sequence[str]) -> list[PhaseT]:
    """Keep phases whose ``"<id> <name>"`` contains any filter (case-insensitive).

    Args:
        phases: The phase table to filter.
        filters: Filters, each of which may hold several comma-separated values.

    Returns:
        The matching phases in table order; all of them when there are no filters.
    """
    needles = [part.strip().lower() for raw in filters for part in raw.split(",") if part.strip()]
    if not needles:
        return list(phases)
    return [
        phase
        for phase in phases
        if any(needle in f"{phase.phase_id} {phase.name}".lower() for needle in needles)
    ]


def build_audit_instruction(phase: AuditPhase, scope: str) -> str:
    """Build the strictly read-only instruction for one audit phase.

    Args:
        phase: The audit phase.
        scope: Paths to restrict the analysis to; empty for the prompt's default.

    Returns:
        The instruction text.
    """
    if scope:
        scope_clause = f"Restrict the analysis to the following scope: {scope}."
    else:
        scope_clause = "Use the default scope defined by the prompt."
    return "\n".join(
        [
            f'You are running the Luma project audit: "{phase.name}".',
            "",
            f'Follow the workflow in the prompt file ".github/prompts/{phase.prompt}" exactly - '
            "its ground rules, what-to-look-for, prioritisation, exclusions, and Output Format "
            "sections all apply.",
            "",
            scope_clause,
            "",
            "This is a STRICTLY READ-ONLY analysis: do not modify, create, build, run, format, or "
            "stage any files. Read every source you cite and verify each location before "
            "reporting it.",
            "",
            'Produce the complete, prioritised report exactly as that prompt\'s "Output Format" '
            "section specifies. Output only the report as your final message, with no preamble "
            "and no closing chatter, and do not wrap the report or any table within it in a "
            "Markdown code fence, so it can be saved verbatim as a Markdown file.",
        ]
    )


def build_fix_instruction(
    phase: FixPhase,
    *,
    report_path: Path | None,
    enforce_lint: bool,
    max_passes: int,
) -> str:
    """Build the mutating instruction for one fix phase.

    Args:
        phase: The fix phase.
        report_path: The audit report the phase should work from, if any.
        enforce_lint: Tell the agent that the phase is gated on lint.
        max_passes: The pass cap for a looping phase.

    Returns:
        The instruction text.
    """
    lines = [f'You are running the Luma project fix phase: "{phase.name}".', ""]
    if report_path is not None:
        lines += [
            "A prioritised audit report for this phase is available at:",
            f"    {report_path}",
            "Read it first and work through its findings in priority order (highest severity "
            "times confidence first). Skip any finding you cannot confirm against the current "
            "source.",
            "",
        ]
    lines += [
        f'Follow the workflow in the prompt file ".github/prompts/{phase.prompt}" exactly - its '
        "ground rules, procedure, verification, and Output Format sections all apply.",
        "",
        "Obey every relevant guide under instructions/ for the languages you touch. Make the "
        "smallest correct change for each item, add or update the matching regression test, and "
        "keep the full test suite green.",
    ]
    if enforce_lint:
        lines += ["", _LINT_DIRECTIVE]
    if phase.limits_passes:
        lines += [
            "",
            f"Perform at most {max_passes} full improvement passes. After the final pass, stop "
            "and summarise any issues that remain.",
        ]
    lines += [
        "",
        "Do not push to any remote. Do not amend, rebase, or rewrite existing commits. Leave "
        "staging to the pipeline unless the prompt says otherwise.",
    ]
    return "\n".join(lines)


# ── Typed options ─────────────────────────────────────────────────
#
# argparse yields an untyped Namespace; it is converted once, at the edge, so
# everything below works with typed, immutable settings.


def _absolute_or_none(path: str | None) -> Path | None:
    """Resolve a user-supplied path against the caller's working directory."""
    return Path(path).expanduser().resolve() if path else None


@dataclass(frozen=True)
class AgentSettings:
    """The agent CLI to drive and how to configure it."""

    name: str
    model: str = ""
    effort: str = ""


@dataclass(frozen=True)
class AuditOptions:
    """Settings for one ``audit`` run; see ``luma_pipeline.py audit --help``."""

    agent: AgentSettings
    phase_filters: tuple[str, ...] = ()
    scope: str = ""
    artifact_root: Path | None = None
    list_only: bool = False
    is_dry_run: bool = False

    @classmethod
    def from_namespace(cls, namespace: argparse.Namespace) -> AuditOptions:
        """Build the options from parsed ``audit`` arguments."""
        return cls(
            agent=AgentSettings(namespace.agent, namespace.model or "", namespace.effort or ""),
            phase_filters=tuple(namespace.phase),
            scope=namespace.scope,
            artifact_root=_absolute_or_none(namespace.artifact_root),
            list_only=namespace.list,
            is_dry_run=namespace.dry_run,
        )


@dataclass(frozen=True)
class FixOptions:
    """Settings for one ``fix`` run; see ``luma_pipeline.py fix --help``.

    The boolean fields mirror the command-line flags of the same name.
    """

    agent: AgentSettings
    phase_filters: tuple[str, ...] = ()
    artifact_root: Path | None = None
    report_dir: Path | None = None
    preset: str = "default"
    max_passes: int = 3
    allow_dirty: bool = False
    skip_baseline: bool = False
    skip_build: bool = False
    skip_test: bool = False
    skip_lint_format: bool = False
    no_branch: bool = False
    no_commit: bool = False
    continue_on_failure: bool = False
    revert_on_failure: bool = False
    list_only: bool = False
    is_dry_run: bool = False

    @classmethod
    def from_namespace(cls, namespace: argparse.Namespace) -> FixOptions:
        """Build the options from parsed ``fix`` arguments."""
        return cls(
            agent=AgentSettings(namespace.agent, namespace.model or "", namespace.effort or ""),
            phase_filters=tuple(namespace.phase),
            artifact_root=_absolute_or_none(namespace.artifact_root),
            report_dir=_absolute_or_none(namespace.report_dir),
            preset=namespace.preset,
            max_passes=namespace.convergence_max_passes,
            allow_dirty=namespace.allow_dirty,
            skip_baseline=namespace.skip_baseline,
            skip_build=namespace.skip_build,
            skip_test=namespace.skip_test,
            skip_lint_format=namespace.skip_lint_format,
            no_branch=namespace.no_branch,
            no_commit=namespace.no_commit,
            continue_on_failure=namespace.continue_on_failure,
            revert_on_failure=namespace.revert_on_failure,
            list_only=namespace.list,
            is_dry_run=namespace.dry_run,
        )


@dataclass(frozen=True)
class AllOptions:
    """Settings for one ``all`` run; see ``luma_pipeline.py all --help``."""

    agent: AgentSettings
    is_dry_run: bool = False
    skip_audit: bool = False
    skip_fix: bool = False
    audit_args: tuple[str, ...] = ()
    fix_args: tuple[str, ...] = ()

    @classmethod
    def from_namespace(cls, namespace: argparse.Namespace) -> AllOptions:
        """Build the options from parsed ``all`` arguments, applying its defaults."""
        if namespace.model is not None:
            model = namespace.model
        else:
            model = DEFAULT_ALL_MODELS[namespace.agent]
        return cls(
            agent=AgentSettings(namespace.agent, model, namespace.effort or ""),
            is_dry_run=namespace.dry_run,
            skip_audit=namespace.skip_audit,
            skip_fix=namespace.skip_fix,
            audit_args=tuple(namespace.audit_arg),
            fix_args=tuple(namespace.fix_arg),
        )


# ── Shared run plumbing ───────────────────────────────────────────


def _timestamp() -> str:
    return datetime.now().strftime("%Y%m%d-%H%M%S")


def _resolve_artifact_root(artifact_root: Path | None, repo_root: Path) -> Path:
    return artifact_root if artifact_root is not None else repo_root / "pipeline-artifacts"


def _select_or_fail(phases: Sequence[PhaseT], filters: Sequence[str], kind: str) -> list[PhaseT]:
    """Select phases by filter, or raise a usage error (exit 2) when none match."""
    selected = select_phases(phases, filters)
    if not selected:
        raise PipelineError(
            f"no {kind} phase matched: {', '.join(filters)}. Use --list to see the phases.",
            exit_code=2,
        )
    return selected


def _print_table(console: Console, headers: Sequence[str], rows: Sequence[Sequence[str]]) -> None:
    """Print rows as left-aligned, space-separated columns."""
    widths = [len(header) for header in headers]
    for row in rows:
        widths = [max(width, len(cell)) for width, cell in zip(widths, row, strict=True)]
    for row in (headers, *rows):
        cells = [cell.ljust(width) for cell, width in zip(row, widths, strict=True)]
        console.line("  ".join(cells).rstrip())


def _yes_no(flag: bool) -> str:
    return "yes" if flag else "no"


def _make_pipeline(
    *,
    is_dry_run: bool,
    console: Console,
    model_cache: dict[tuple[str, str], str] | None,
) -> Pipeline:
    return Pipeline(
        find_repo_root(SCRIPT_DIR), is_dry_run=is_dry_run, console=console, model_cache=model_cache
    )


# ── audit ─────────────────────────────────────────────────────────


@dataclass(frozen=True)
class AuditResult:
    """The outcome of one audit phase, as listed in the run index."""

    order: int
    phase: AuditPhase
    status: Status


def run_audit(
    options: AuditOptions,
    console: Console,
    model_cache: dict[tuple[str, str], str] | None = None,
) -> int:
    """Run the read-only audit phases.

    Args:
        options: The audit settings.
        console: Where progress goes.
        model_cache: Resolved Copilot models shared with other stages of the run.

    Returns:
        The process exit code: 0 when every phase succeeded, 1 otherwise.

    Raises:
        PipelineError: On a usage error or a missing agent CLI.
    """
    if options.list_only:
        rows = [
            [str(order), phase.phase_id, phase.name, phase.prompt]
            for order, phase in enumerate(AUDIT_PHASES, start=1)
        ]
        _print_table(console, ["Order", "Id", "Name", "Prompt"], rows)
        return 0

    phases = _select_or_fail(AUDIT_PHASES, options.phase_filters, "audit")
    agent = options.agent
    if not options.is_dry_run:
        require_agent(agent.name)
    pipeline = _make_pipeline(
        is_dry_run=options.is_dry_run, console=console, model_cache=model_cache
    )
    model = pipeline.resolve_copilot_model(agent.name, agent.model)

    repo_root = pipeline.repo_root
    timestamp = _timestamp()
    run_dir = _resolve_artifact_root(options.artifact_root, repo_root) / f"audit-{timestamp}"
    report_dir = run_dir / "reports"
    log_dir = run_dir / "logs"
    pipeline.make_dirs(report_dir)
    pipeline.make_dirs(log_dir)

    console.banner("Luma audit pipeline (read-only)", f"Repo: {repo_root}")
    console.line(f"  Phases : {len(phases)}")
    console.line(f"  Agent  : {agent.name}")
    console.line(f"  Model  : {model or '(agent default)'}")
    console.line(f"  Output : {run_dir}")
    if options.is_dry_run:
        console.note("  Mode   : DRY RUN (no agent invoked)")

    results: list[AuditResult] = []
    for order, phase in enumerate(phases, start=1):
        console.banner(f"[{order}/{len(phases)}] {phase.name}", f"Prompt: {phase.prompt}")
        instruction = build_audit_instruction(phase, options.scope or phase.default_scope)
        try:
            outcome = pipeline.invoke_agent_phase(
                agent=agent.name,
                mode="plan",
                instruction=instruction,
                output_file=report_dir / f"{phase.phase_id}.md",
                log_dir=log_dir,
                model=model,
                effort=agent.effort,
            )
        except (PipelineError, OSError) as err:
            console.warn(f"phase '{phase.phase_id}' errored: {err}")
            status = Status.ERROR
        else:
            status = Status.OK if outcome.success else Status.FAILED
            if not outcome.success:
                console.warn(f"phase '{phase.phase_id}' failed (agent exit {outcome.exit_code}).")
        results.append(AuditResult(order, phase, status))

    if not options.is_dry_run:
        index_path = run_dir / "INDEX.md"
        pipeline.write_text(index_path, _audit_index(timestamp, repo_root, results))
        console.line()
        console.ok(f"Index written: {index_path}")

    console.banner("Audit summary")
    rows = [[str(r.order), r.phase.phase_id, r.status.value] for r in results]
    _print_table(console, ["Order", "Id", "Status"], rows)
    if any(result.status is not Status.OK for result in results):
        console.warn("completed with one or more failed phases.")
        return 1
    console.ok("All selected phases completed.")
    return 0


def _audit_index(timestamp: str, repo_root: Path, results: Sequence[AuditResult]) -> str:
    """Build the Markdown index of an audit run."""
    lines = [
        "# Luma audit run",
        "",
        f"- Generated: {timestamp}",
        f"- Repository: {repo_root}",
        "",
        "| Order | Phase | Status | Report |",
        "| ----- | ----- | ------ | ------ |",
    ]
    for result in results:
        report = f"{result.phase.phase_id}.md"
        lines.append(
            f"| {result.order} | {result.phase.name} | {result.status.value} | "
            f"[{report}](reports/{report}) |"
        )
    lines += ["", f"Triage these reports, then run `python {SCRIPT_PATH} fix` to apply fixes."]
    return "\n".join(lines) + "\n"


# ── fix ───────────────────────────────────────────────────────────


@dataclass(frozen=True)
class FixResult:
    """The outcome of one fix phase, as listed in the run summary."""

    order: int
    phase: FixPhase
    status: Status
    commit: str


@dataclass(frozen=True)
class FixContext:
    """Per-run state shared by every fix phase."""

    options: FixOptions
    pipeline: Pipeline
    model: str
    artifact_root: Path
    log_dir: Path


@dataclass(frozen=True)
class PhaseRun:
    """What one fix phase did, before any revert.

    Attributes:
        status: The phase's outcome.
        commit: Short SHA of the commit the phase ended on, ``-`` when it
            created none.
        start: Full SHA of HEAD when the phase started; None in a dry run or
            when git could not resolve it.
    """

    status: Status
    commit: str
    start: str | None


def _newest_report_dir(artifact_root: Path) -> Path | None:
    """Return ``reports/`` of the newest ``audit-*`` run that has one."""
    if not artifact_root.is_dir():
        return None
    runs = sorted(
        (entry for entry in artifact_root.glob("audit-*") if (entry / "reports").is_dir()),
        key=lambda entry: entry.name,
        reverse=True,
    )
    return runs[0] / "reports" if runs else None


def _run_fix_agent(context: FixContext, phase: FixPhase, instruction: str) -> Status:
    """Run the phase's agent, shielding the artifact tree when the phase wipes it."""
    pipeline = context.pipeline
    if phase.cleans_ignored_files:
        protection = pipeline.protect_artifacts(context.artifact_root, context.log_dir)
    else:
        protection = contextlib.nullcontext(context.log_dir)
    with protection as phase_log_dir:
        outcome = pipeline.invoke_agent_phase(
            agent=context.options.agent.name,
            mode="agent",
            instruction=instruction,
            log_file=phase_log_dir / f"{phase.phase_id}.log",
            log_dir=phase_log_dir,
            model=context.model,
            effort=context.options.agent.effort,
        )
    if outcome.success:
        return Status.OK
    pipeline.console.warn(
        f"agent reported a non-zero exit ({outcome.exit_code}) for '{phase.phase_id}'."
    )
    return Status.AGENT_FAILED


def _run_fix_gate(context: FixContext, phase: FixPhase) -> Status:
    """Build + test, then format + lint."""
    options = context.options
    pipeline = context.pipeline
    if not pipeline.build_and_test(
        preset=options.preset, skip_build=options.skip_build, skip_test=options.skip_test
    ):
        pipeline.console.warn(f"gate failed after '{phase.phase_id}'.")
        return Status.GATE_FAILED
    # Lint after build + test so a broken build fails fast. lint_and_format
    # refreshes build/compile_commands.json when it is missing or older than the
    # configure the gate just ran. Formatting changes fold into the phase's commit.
    if not options.skip_lint_format and not pipeline.lint_and_format(check_only=False):
        pipeline.console.warn(f"lint/format gate failed after '{phase.phase_id}'.")
        return Status.GATE_FAILED
    return Status.OK


def _check_agent_commits(context: FixContext, phase: FixPhase, start: str | None) -> Status:
    """Report commits the agent made itself; fail the phase if it rewrote history.

    The prompts leave committing to the pipeline. Commits the agent adds anyway
    are kept, gated with the phase, and discarded by a revert; a starting
    commit that is no longer an ancestor of HEAD means history was rewritten,
    which fails the phase.
    """
    pipeline = context.pipeline
    head = pipeline.head_commit()
    if start is None or head is None or head == start:
        return Status.OK
    count = pipeline.count_commits(start, head)
    span = f"{pipeline.short_sha(start)}..{pipeline.short_sha(head)}"
    if count is None:
        pipeline.console.warn(
            f"the agent rewrote history during '{phase.phase_id}' ({span}): the phase's "
            "starting commit is no longer an ancestor of HEAD. Failing the phase."
        )
        return Status.ERROR
    pipeline.console.warn(
        f"the agent made {count} commit(s) of its own during '{phase.phase_id}' ({span}). "
        "They are gated with the phase; if it fails, --revert-on-failure discards them."
    )
    return Status.OK


def _execute_fix_phase(context: FixContext, phase: FixPhase, instruction: str) -> PhaseRun:
    """Run one fix phase end to end: agent, gate, then checkpoint."""
    options = context.options
    pipeline = context.pipeline
    console = pipeline.console
    commit = "-"
    start: str | None = None
    try:
        start = pipeline.head_commit()
        status = _run_fix_agent(context, phase, instruction)
        if status is Status.OK:
            status = _check_agent_commits(context, phase, start)
        if status is Status.OK and phase.has_gate and not phase.self_verifies:
            status = _run_fix_gate(context, phase)
        if status is Status.OK and phase.should_commit and not options.no_commit:
            try:
                pipeline.git_checkpoint(
                    subject=f"chore(pipeline): {phase.phase_id}",
                    agent=options.agent.name,
                    artifact_root=context.artifact_root,
                )
            except CheckpointError as err:
                # Never let a rejected commit look like success: git add -A would
                # re-stage it into every later checkpoint.
                console.warn(f"checkpoint commit failed for '{phase.phase_id}': {err}")
                status = Status.COMMIT_FAILED
        if status is Status.OK and start is not None:
            # The checkpoint or the agent's own commits; either way, HEAD moved.
            head = pipeline.head_commit()
            if head is not None and head != start:
                commit = pipeline.short_sha(head)
    except (PipelineError, OSError) as err:
        console.warn(f"phase '{phase.phase_id}' errored: {err}")
        status = Status.ERROR
    return PhaseRun(status, commit, start)


def _revert_failed_phase(context: FixContext, phase: FixPhase, start: str | None) -> bool:
    """Reset the tree and branch to the commit the failed phase started from.

    Returns:
        True when the revert succeeded.
    """
    pipeline = context.pipeline
    console = pipeline.console
    target = start or "HEAD"
    console.note(f"Reverting the working tree to the start of '{phase.phase_id}'...")
    try:
        head = pipeline.head_commit()
        if start is not None and head is not None and head != start:
            console.warn(
                f"discarding the commit(s) the agent made during '{phase.phase_id}'. They "
                f"stay recoverable: git branch <name> {head}"
            )
        is_reverted = pipeline.revert_to(target, remove_untracked=True, keep=context.artifact_root)
    except PipelineError as err:
        console.warn(str(err))
        is_reverted = False
    if not is_reverted:
        console.warn(f"revert to {target} failed.")
    return is_reverted


def _abort_message(
    phase: FixPhase, run: PhaseRun, options: FixOptions, *, is_revert_failed: bool
) -> str:
    """Explain why the run stops after *phase* and how to avoid it."""
    if is_revert_failed:
        target = run.start or "HEAD"
        return (
            f"stopping after '{phase.phase_id}': the revert failed, so the working tree may "
            "still hold the failed phase's changes and later phases cannot run on a known "
            f"state. Inspect it with 'git status' and reset it yourself (git reset --hard "
            f"{target} && git clean -fd)."
        )
    if run.status is not Status.COMMIT_FAILED or not options.continue_on_failure:
        return f"stopping after '{phase.phase_id}' (pass --continue-on-failure to keep going)."
    return (
        f"stopping after '{phase.phase_id}': the commit was rejected and cannot be skipped "
        "(git add -A would re-stage it). Fix the blocker (e.g. the pre-commit hook), or pass "
        "--revert-on-failure to discard failed phases and keep going."
    )


def _verify_baseline(options: FixOptions, pipeline: Pipeline) -> None:
    """Require a green build + test and a lint-clean tree before any phase runs."""
    pipeline.console.banner("Baseline gate", "Verifying a green build + test before making changes")
    if not pipeline.build_and_test(
        preset=options.preset, skip_build=options.skip_build, skip_test=options.skip_test
    ):
        raise PipelineError(
            "baseline build + test is not green. Fix the baseline first, or pass --skip-baseline."
        )
    # Check-only: verify the starting tree without changing it, so a later lint
    # failure is attributable to the phase rather than to pre-existing drift.
    if not options.skip_lint_format and not pipeline.lint_and_format(check_only=True):
        raise PipelineError(
            'baseline lint is not clean. Run "python scripts/format.py" and fix any remaining '
            "lint issues, or pass --skip-lint-format."
        )


def _check_fix_preconditions(options: FixOptions, pipeline: Pipeline, branch: str) -> None:
    """Check the tree, create the run branch, and verify the baseline.

    Raises:
        PipelineError: When a precondition fails.
    """
    console = pipeline.console
    if not options.allow_dirty and not pipeline.is_clean_working_tree():
        raise PipelineError(
            "working tree is not clean. Commit or stash your changes, or pass --allow-dirty."
        )
    is_on_run_branch = False
    if not options.no_branch:
        console.line(f"Creating branch: {branch}")
        if not pipeline.create_branch(branch):
            raise PipelineError(f"failed to create branch '{branch}'.")
        is_on_run_branch = not pipeline.is_dry_run
    if options.skip_baseline:
        return
    try:
        _verify_baseline(options, pipeline)
    except PipelineError as err:
        if not is_on_run_branch:
            raise
        raise PipelineError(
            f"{err} You are still on the new branch '{branch}'; run 'git switch -' to return "
            "to your previous branch.",
            exit_code=err.exit_code,
        ) from err


def run_fix(
    options: FixOptions,
    console: Console,
    model_cache: dict[tuple[str, str], str] | None = None,
) -> int:
    """Run the mutating, gated fix phases.

    Args:
        options: The fix settings.
        console: Where progress goes.
        model_cache: Resolved Copilot models shared with other stages of the run.

    Returns:
        The process exit code: 0 when every phase succeeded, 1 otherwise.

    Raises:
        PipelineError: On a usage error, a missing agent CLI, or a failed precondition.
    """
    if options.list_only:
        rows = [
            [
                str(order),
                phase.phase_id,
                phase.name,
                _yes_no(phase.has_gate),
                _yes_no(phase.should_commit),
                _yes_no(phase.self_verifies),
                phase.prompt,
            ]
            for order, phase in enumerate(FIX_PHASES, start=1)
        ]
        _print_table(console, ["Order", "Id", "Name", "Gate", "Commit", "Verify", "Prompt"], rows)
        return 0

    phases = _select_or_fail(FIX_PHASES, options.phase_filters, "fix")
    agent = options.agent
    if not options.is_dry_run:
        require_agent(agent.name)
    pipeline = _make_pipeline(
        is_dry_run=options.is_dry_run, console=console, model_cache=model_cache
    )
    model = pipeline.resolve_copilot_model(agent.name, agent.model)

    repo_root = pipeline.repo_root
    artifact_root = _resolve_artifact_root(options.artifact_root, repo_root)
    report_dir = options.report_dir or _newest_report_dir(artifact_root)
    if report_dir is not None:
        console.line(f"Audit reports: {report_dir}")
    else:
        console.note("Audit reports: none found - phases will self-discover findings.")

    timestamp = _timestamp()
    run_dir = artifact_root / f"fix-{timestamp}"
    log_dir = run_dir / "logs"
    branch = f"pipeline/fix-{timestamp}"

    console.banner("Luma fix pipeline (mutating, gated)", f"Repo: {repo_root}")
    console.line(f"  Phases  : {len(phases)}")
    console.line(f"  Preset  : {options.preset}")
    console.line(f"  Agent   : {agent.name}")
    console.line(f"  Model   : {model or '(agent default)'}")
    console.line(f"  Output  : {run_dir}")
    if options.is_dry_run:
        console.note("  Mode    : DRY RUN (nothing invoked, built, or committed)")

    _check_fix_preconditions(options, pipeline, branch)
    pipeline.make_dirs(log_dir)

    context = FixContext(options, pipeline, model, artifact_root, log_dir)
    results: list[FixResult] = []
    is_aborted = False
    for order, phase in enumerate(phases, start=1):
        console.banner(f"[{order}/{len(phases)}] {phase.name}", f"Prompt: {phase.prompt}")
        report_path = None
        if report_dir is not None and phase.input_report:
            candidate = report_dir / phase.input_report
            report_path = candidate if candidate.is_file() else None
        instruction = build_fix_instruction(
            phase,
            report_path=report_path,
            enforce_lint=phase.has_gate
            and not phase.self_verifies
            and not options.skip_lint_format,
            max_passes=options.max_passes,
        )
        run = _execute_fix_phase(context, phase, instruction)
        results.append(FixResult(order, phase, run.status, run.commit))
        if run.status is Status.OK:
            continue

        # --revert-on-failure implies a clean start (it is refused with
        # --allow-dirty), so a successful revert leaves a clean tree.
        is_revert_failed = options.revert_on_failure and not _revert_failed_phase(
            context, phase, run.start
        )
        is_aborted = is_revert_failed or should_abort(
            run.status,
            continue_on_failure=options.continue_on_failure,
            reverts_cleanly=options.revert_on_failure,
        )
        if is_aborted:
            console.warn(_abort_message(phase, run, options, is_revert_failed=is_revert_failed))
            break

    if not options.is_dry_run:
        summary_path = run_dir / "SUMMARY.md"
        summary = _fix_summary(
            timestamp,
            repo_root,
            None if options.no_branch else branch,
            results,
            log_dir,
            is_aborted=is_aborted,
        )
        pipeline.write_text(summary_path, summary)
        console.line()
        console.ok(f"Summary written: {summary_path}")

    console.banner("Fix summary")
    if not options.no_branch and not options.is_dry_run:
        console.line(f"Branch: {branch}")
    rows = [[str(r.order), r.phase.phase_id, r.status.value, r.commit] for r in results]
    _print_table(console, ["Order", "Id", "Status", "Commit"], rows)

    failures = sum(1 for result in results if result.status is not Status.OK)
    if is_aborted:
        console.note("Pipeline stopped early on a failed phase.")
        return 1
    if failures:
        console.note(f"Completed with {failures} failed phase(s).")
        return 1
    console.ok("All selected phases completed.")
    return 0


def _fix_summary(
    timestamp: str,
    repo_root: Path,
    branch: str | None,
    results: Sequence[FixResult],
    log_dir: Path,
    *,
    is_aborted: bool,
) -> str:
    """Build the Markdown summary of a fix run."""
    lines = ["# Luma fix run", "", f"- Generated: {timestamp}", f"- Repository: {repo_root}"]
    if branch is not None:
        lines.append(f"- Branch: {branch}")
    lines += [
        "",
        "| Order | Id | Phase | Status | Commit | Log |",
        "| ----- | -- | ----- | ------ | ------ | --- |",
    ]
    for result in results:
        log_name = f"{result.phase.phase_id}.log"
        log_cell = f"[{log_name}](logs/{log_name})" if (log_dir / log_name).is_file() else "-"
        lines.append(
            f"| {result.order} | {result.phase.phase_id} | {result.phase.name} | "
            f"{result.status.value} | {result.commit} | {log_cell} |"
        )
    lines += [
        "",
        "The pipeline stopped early on a failed phase."
        if is_aborted
        else "All selected phases completed.",
        "",
        "A `-` in the Commit column means the phase created no commit of its own; a `-` in the "
        "Log column means no transcript was captured.",
    ]
    return "\n".join(lines) + "\n"


# ── all ───────────────────────────────────────────────────────────


def run_all(
    options: AllOptions,
    passthrough: Sequence[str],
    parser: argparse.ArgumentParser,
    console: Console,
) -> int:
    """Run the audit and then the fix with shared agent settings.

    Every stage's arguments are parsed before anything runs, so a typo in
    ``--fix-arg`` fails in seconds rather than after a long audit.

    Args:
        options: The ``all`` settings.
        passthrough: Arguments after a literal ``--``, forwarded to every stage.
        parser: The command-line parser, used to parse each stage's arguments.
        console: Where progress goes.

    Returns:
        The exit code of the last stage that ran.
    """
    agent = options.agent
    common = ["--agent", agent.name]
    if agent.model:
        common += ["--model", agent.model]
    if agent.effort:
        common += ["--effort", agent.effort]
    if options.is_dry_run:
        common.append("--dry-run")

    stages: list[tuple[str, str, Sequence[str]]] = []
    if not options.skip_audit:
        stages.append(("Audit (read-only)", "audit", options.audit_args))
    if not options.skip_fix:
        stages.append(("Fix (mutating, gated)", "fix", options.fix_args))

    planned: list[tuple[str, AuditOptions | FixOptions]] = []
    for title, command, extra in stages:
        namespace = parser.parse_args([command, *common, *passthrough, *extra])
        validate_options(parser, namespace)
        if command == "audit":
            planned.append((title, AuditOptions.from_namespace(namespace)))
        else:
            planned.append((title, FixOptions.from_namespace(namespace)))

    console.banner(
        "Luma audit + fix pipeline",
        f"Agent: {agent.name}   Model: {agent.model or '(agent default)'}   "
        f"Effort: {agent.effort or '(agent default)'}",
    )
    if options.is_dry_run:
        console.note("  Mode : DRY RUN (nothing invoked, built, or committed)")
    if options.skip_audit:
        console.dim("Skipping audit stage (--skip-audit).")

    model_cache: dict[tuple[str, str], str] = {}
    exit_code = 0
    for index, (title, stage) in enumerate(planned, start=1):
        console.banner(f"Stage {index}/{len(planned)} - {title}")
        if isinstance(stage, AuditOptions):
            exit_code = run_audit(stage, console, model_cache)
            if exit_code != 0:
                console.warn(f"audit stage exited with code {exit_code}; skipping the fix stage.")
                return exit_code
        else:
            exit_code = run_fix(stage, console, model_cache)
            if exit_code != 0:
                console.warn(f"fix stage exited with code {exit_code}.")

    if options.skip_fix:
        console.dim("Skipping fix stage (--skip-fix).")
    console.banner("Pipeline complete")
    return exit_code


# ── Command line ──────────────────────────────────────────────────


def _agent_name(value: str) -> str:
    """argparse type: a supported agent name, case-insensitive."""
    name = value.strip().lower()
    if name not in AGENTS:
        raise argparse.ArgumentTypeError(f"invalid agent '{value}' (use copilot or claude)")
    return name


def _pass_count(value: str) -> int:
    """argparse type: a convergence pass count within the allowed range."""
    try:
        count = int(value, 10)
    except ValueError:
        count = 0
    if not 1 <= count <= MAX_CONVERGENCE_PASSES:
        raise argparse.ArgumentTypeError(
            f"must be an integer between 1 and {MAX_CONVERGENCE_PASSES}, got '{value}'"
        )
    return count


def _add_agent_options(
    parser: argparse.ArgumentParser, *, model_help: str, effort_help: str
) -> None:
    """Add ``--agent`` / ``--model`` / ``--effort`` to *parser*."""
    group = parser.add_argument_group("agent")
    group.add_argument(
        "--agent",
        type=_agent_name,
        default="copilot",
        metavar="{copilot,claude}",
        help="agent CLI backend (default: copilot)",
    )
    group.add_argument("--model", metavar="NAME", help=model_help)
    group.add_argument("--effort", metavar="LEVEL", help=effort_help)


def _add_stage_options(parser: argparse.ArgumentParser, kind: str) -> None:
    """Add the options shared by ``audit`` and ``fix`` to *parser*."""
    parser.add_argument(
        "--phase",
        action="append",
        default=[],
        metavar="FILTER",
        help=f"only run {kind} phases whose id or name contains FILTER (case-insensitive; "
        "repeatable; comma-separated values accepted)",
    )
    parser.add_argument(
        "--artifact-root",
        metavar="DIR",
        help="base directory for run output (default: <repo>/pipeline-artifacts)",
    )
    _add_agent_options(
        parser,
        model_help="model for the agent's --model flag (omit to let the agent choose; Copilot "
        "falls back to 'auto' when the CLI cannot select it)",
        effort_help=f"reasoning effort; validated for copilot ({', '.join(COPILOT_EFFORTS)}), "
        "passed through for claude",
    )
    parser.add_argument("--list", action="store_true", help=f"list the {kind} phases and exit")


_DESCRIPTION = """\
Drive the Luma improvement prompts (.github/prompts/) through an agent CLI:
the GitHub Copilot CLI (default) or the Claude Code CLI.

  audit  read-only audits; one ranked report per phase
  fix    gated fixes on a dedicated branch, checkpointed when green
  all    audit, then fix, with shared agent settings
"""

_EPILOG = f"""\
examples:
  python {SCRIPT_PATH} audit --list
  python {SCRIPT_PATH} audit --dry-run --agent claude
  python {SCRIPT_PATH} fix --phase bug-fix
  python {SCRIPT_PATH} all --dry-run
"""


def build_parser() -> argparse.ArgumentParser:
    """Build the ``audit`` / ``fix`` / ``all`` command-line parser."""
    formatter = argparse.RawDescriptionHelpFormatter
    parser = argparse.ArgumentParser(
        prog=SCRIPT_PATH,
        description=_DESCRIPTION,
        epilog=_EPILOG,
        formatter_class=formatter,
    )
    commands = parser.add_subparsers(dest="command", required=True, metavar="{audit,fix,all}")

    audit = commands.add_parser(
        "audit",
        help="run the read-only audit phases",
        description="Run the read-only audit phases. Each writes a ranked Markdown report\n"
        "under <artifact-root>/audit-<timestamp>/reports/.",
        epilog=f"examples:\n  python {SCRIPT_PATH} audit --list\n"
        f"  python {SCRIPT_PATH} audit --dry-run\n"
        f"  python {SCRIPT_PATH} audit --phase bug-search --scope core/runtime/vm/\n",
        formatter_class=formatter,
    )
    _add_stage_options(audit, "audit")
    audit.add_argument(
        "--scope",
        default="",
        metavar="PATHS",
        help="restrict every phase to this scope, overriding each phase's default",
    )
    audit.add_argument(
        "--dry-run", action="store_true", help="print what would run; invoke nothing"
    )

    fix = commands.add_parser(
        "fix",
        help="run the mutating fix phases, gated on build + test + lint",
        description="Run the mutating fix phases on a dedicated branch, gating each phase on\n"
        "build + test + lint and committing a checkpoint only when the tree is green.",
        epilog=f"examples:\n  python {SCRIPT_PATH} fix --list\n"
        f"  python {SCRIPT_PATH} fix --dry-run\n"
        f"  python {SCRIPT_PATH} fix --phase bug-fix --continue-on-failure\n",
        formatter_class=formatter,
    )
    _add_stage_options(fix, "fix")
    fix.add_argument(
        "--report-dir",
        metavar="DIR",
        help="audit reports/ directory to consume "
        "(default: newest <artifact-root>/audit-*/reports)",
    )
    fix.add_argument(
        "--preset",
        default="default",
        metavar="NAME",
        help="CMake preset for the gate (default: default)",
    )
    fix.add_argument(
        "--convergence-max-passes",
        type=_pass_count,
        default=3,
        metavar="N",
        help=f"passes for iterative-improvement (1-{MAX_CONVERGENCE_PASSES}, default 3)",
    )
    safety = fix.add_argument_group("safety overrides")
    safety.add_argument(
        "--allow-dirty",
        action="store_true",
        help="do not require a clean working tree (cannot be combined with --revert-on-failure)",
    )
    safety.add_argument(
        "--skip-baseline", action="store_true", help="skip the initial green baseline"
    )
    safety.add_argument(
        "--skip-build",
        action="store_true",
        help="skip the configure and build steps of every gate",
    )
    safety.add_argument("--skip-test", action="store_true", help="skip the test step of every gate")
    safety.add_argument(
        "--skip-lint-format",
        action="store_true",
        help="skip the format.py + lint.py gate (incl. clang-tidy) at baseline and after phases",
    )
    safety.add_argument("--no-branch", action="store_true", help="run on the current branch")
    safety.add_argument("--no-commit", action="store_true", help="do not commit green phases")
    safety.add_argument(
        "--continue-on-failure",
        action="store_true",
        help="keep going past a failed phase (a rejected commit still stops the run unless it "
        "is reverted from a clean start)",
    )
    safety.add_argument(
        "--revert-on-failure",
        action="store_true",
        help="after a failed phase, reset the branch and tree to the commit the phase started "
        "from (dropping any commit the agent made) and delete the untracked files it created; "
        "requires a clean start, so it cannot be combined with --allow-dirty; with --no-commit "
        "this discards every phase's work",
    )
    fix.add_argument(
        "--dry-run",
        action="store_true",
        help="print every command; invoke, build, and commit nothing",
    )

    run_both = commands.add_parser(
        "all",
        help="run audit, then fix",
        usage=f"{SCRIPT_PATH} all [options] [-- stage-args...]",
        description="Run the audit and then the fix with the same agent settings. Defaults to\n"
        "the Copilot CLI driving claude-opus-4.6 at medium effort. The fix is skipped\n"
        "when the audit fails. Anything after a literal -- is forwarded to every stage.",
        epilog=f"examples:\n  python {SCRIPT_PATH} all --dry-run\n"
        f"  python {SCRIPT_PATH} all --agent claude --skip-fix\n"
        f"  python {SCRIPT_PATH} all --fix-arg --revert-on-failure\n",
        formatter_class=formatter,
    )
    _add_agent_options(
        run_both,
        model_help="model for every stage (default: claude-opus-4.6 for copilot, the agent's "
        "own default for claude; pass --model= to let the agent choose)",
        effort_help=f"reasoning effort for every stage (default: {DEFAULT_ALL_EFFORT}; "
        "pass --effort= to omit)",
    )
    run_both.set_defaults(effort=DEFAULT_ALL_EFFORT)
    run_both.add_argument("--dry-run", action="store_true", help="preview every stage")
    run_both.add_argument("--skip-audit", action="store_true", help="skip the audit stage")
    run_both.add_argument("--skip-fix", action="store_true", help="skip the fix stage")
    run_both.add_argument(
        "--audit-arg",
        action="append",
        default=[],
        metavar="ARG",
        help="extra argument for the audit stage (repeatable)",
    )
    run_both.add_argument(
        "--fix-arg",
        action="append",
        default=[],
        metavar="ARG",
        help="extra argument for the fix stage (repeatable), e.g. --fix-arg --no-commit",
    )
    return parser


def validate_options(parser: argparse.ArgumentParser, namespace: argparse.Namespace) -> None:
    """Cross-option checks argparse cannot express; exits 2 on a usage error."""
    effort = namespace.effort or ""
    if namespace.agent == "copilot" and effort and effort not in COPILOT_EFFORTS:
        parser.error(f"invalid --effort '{effort}' for copilot (use {', '.join(COPILOT_EFFORTS)})")
    if namespace.command == "all" and namespace.skip_audit and namespace.skip_fix:
        parser.error("both --skip-audit and --skip-fix are set; nothing to do")
    if namespace.command == "fix" and namespace.allow_dirty and namespace.revert_on_failure:
        # The revert runs git reset --hard, which would destroy the very
        # uncommitted changes --allow-dirty let the run start with.
        parser.error(
            "--revert-on-failure cannot be combined with --allow-dirty: the revert runs "
            "'git reset --hard', which would destroy your uncommitted changes. Commit or stash "
            "them first."
        )


def _split_all_argv(argv: Sequence[str]) -> tuple[list[str], list[str]]:
    """Separate ``all``'s own options from the ``--`` passthrough.

    ``--audit-arg X`` / ``--fix-arg X`` are rewritten as ``--audit-arg=X`` so an
    option-like value such as ``--no-commit`` is not mistaken for an option.

    Returns:
        ``all``'s own arguments and the passthrough arguments.
    """
    own: list[str] = []
    index = 0
    while index < len(argv):
        token = argv[index]
        if token == "--":
            return own, list(argv[index + 1 :])
        if token in ("--audit-arg", "--fix-arg") and index + 1 < len(argv):
            own.append(f"{token}={argv[index + 1]}")
            index += 2
            continue
        own.append(token)
        index += 1
    return own, []


def _reconfigure_streams() -> None:
    """Make stdout and stderr UTF-8, so agent output never fails to print."""
    for stream in (sys.stdout, sys.stderr):
        with contextlib.suppress(AttributeError, ValueError):
            stream.reconfigure(encoding="utf-8", errors="replace")


def main(argv: Sequence[str] | None = None) -> int:
    """Parse the command line and run the chosen subcommand.

    Args:
        argv: The arguments (default: ``sys.argv[1:]``).

    Returns:
        The process exit code.
    """
    _reconfigure_streams()
    args = list(sys.argv[1:] if argv is None else argv)
    passthrough: list[str] = []
    if args and args[0] == "all":
        args, passthrough = _split_all_argv(args)

    parser = build_parser()
    namespace = parser.parse_args(args)
    validate_options(parser, namespace)

    console = Console()
    try:
        # SIGTERM / SIGHUP unwind like Ctrl+C, so the agent's process tree is
        # killed and the artifact tree restored on the way out.
        with termination_handlers():
            if namespace.command == "audit":
                return run_audit(AuditOptions.from_namespace(namespace), console)
            if namespace.command == "fix":
                return run_fix(FixOptions.from_namespace(namespace), console)
            return run_all(AllOptions.from_namespace(namespace), passthrough, parser, console)
    except PipelineError as err:
        console.error(str(err))
        return err.exit_code
    except KeyboardInterrupt:
        console.error("interrupted.")
        return 130
    except TerminationRequested as err:
        console.error(f"terminated ({err}).")
        return err.exit_code


if __name__ == "__main__":
    sys.exit(main())
