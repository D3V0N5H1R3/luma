#!/usr/bin/env python3
"""Validate editor tree-sitter highlight queries against the canonical source.

The canonical highlight queries live in ``extensions/shared/queries/``. Zed
keeps its own hand-adapted copy: it renames capture groups to the editor's
native highlight conventions (``@string.special``, ``@constant.builtin`` …) and
may reorder or reformat patterns (spreading keyword lists across multiple lines,
for instance). Those adaptations are intentional and must not be reported as
problems.

What *is* a problem is **structural** drift: a node type, anonymous terminal, or
predicate that exists in one file but not the other. That means the grammar
coverage has diverged and a real highlight is missing or stale.

This script compares the *unordered multiset* of structural tokens — named node
types, quoted terminals, and ``#predicate`` names — after stripping capture
groups (``@…``), comments, whitespace, and ordering. So capture renames,
reordering, and reformatting pass, while genuine node/terminal add/remove drift
is caught.

Usage:
    python sync-queries.py --check   Report structural drift; exit 1 if any.
                                     (Also the default with no arguments.)
    python sync-queries.py --force   Create a *missing* editor copy from the
                                     canonical file. Existing copies are only
                                     checked, never overwritten — blindly
                                     copying would destroy the hand-adapted
                                     capture names. To re-seed a copy, delete
                                     it first, then run --force.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from collections import Counter
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent

CANONICAL_DIR = SCRIPT_DIR / "queries"

TARGETS = [
    ROOT_DIR / "zed" / "languages" / "luma",
]


def get_query_files() -> list[str]:
    """List all .scm files in the canonical queries directory."""
    return [f.name for f in CANONICAL_DIR.glob("*.scm")]


def tokenize(content: str) -> list[str]:
    """Return the structural tokens of a tree-sitter query.

    Emits named nodes/fields (``ID:``), quoted anonymous terminals (``STR:``)
    and predicate names (``PRED:``). Capture groups (``@…``), comments and all
    punctuation/whitespace are dropped, so only grammar-meaningful structure
    remains — the part that must match across editor copies.
    """
    tokens: list[str] = []
    i, n = 0, len(content)
    while i < n:
        c = content[i]
        if c == '"':
            # Quoted terminal (anonymous node), honouring backslash escapes.
            j = i + 1
            buf = ['"']
            while j < n:
                if content[j] == "\\" and j + 1 < n:
                    buf.append(content[j : j + 2])
                    j += 2
                    continue
                buf.append(content[j])
                if content[j] == '"':
                    j += 1
                    break
                j += 1
            tokens.append("STR:" + "".join(buf))
            i = j
        elif c == ";":
            # Comment — skip to end of line.
            while i < n and content[i] != "\n":
                i += 1
        elif c == "@":
            # Capture group — the per-editor name we deliberately ignore.
            i += 1
            while i < n and (content[i].isalnum() or content[i] in "._"):
                i += 1
        elif c == "#":
            # Predicate name, e.g. #match? #eq? #any-of?
            j = i + 1
            while j < n and (content[j].isalnum() or content[j] in "?!-._"):
                j += 1
            tokens.append("PRED:" + content[i:j])
            i = j
        elif c.isalpha() or c == "_":
            # Named node type or field name.
            j = i
            while j < n and (content[j].isalnum() or content[j] == "_"):
                j += 1
            tokens.append("ID:" + content[i:j])
            i = j
        else:
            # Punctuation, anchors, quantifiers, whitespace — not structural.
            i += 1
    return tokens


def _describe(token: str) -> str:
    """Turn a tagged token into a human-readable label."""
    kind, _, value = token.partition(":")
    if kind == "STR":
        return f"terminal {value}"
    if kind == "PRED":
        return f"predicate {value}"
    return f"node/field `{value}`"


def compare_structural(canonical: str, target: str) -> list[str]:
    """Return structural differences between two query files.

    Ignores capture-group names, comments, ordering and formatting; compares the
    unordered multiset of node types, terminals and predicates. An empty list
    means the two files are structurally equivalent.
    """
    canon = Counter(tokenize(canonical))
    tgt = Counter(tokenize(target))

    missing = canon - tgt
    extra = tgt - canon
    if not missing and not extra:
        return []

    lines: list[str] = []
    for token, count in sorted(missing.items()):
        suffix = f" (x{count})" if count > 1 else ""
        lines.append(f"- missing from target: {_describe(token)}{suffix}")
    for token, count in sorted(extra.items()):
        suffix = f" (x{count})" if count > 1 else ""
        lines.append(f"+ extra in target:     {_describe(token)}{suffix}")
    return lines


def sync_file(canonical_file: Path, target_dir: Path, create_missing: bool) -> tuple[bool, bool]:
    """Check one query file against its canonical source.

    Returns ``(created, drift)``. A missing target counts as drift unless it was
    created. Existing targets are only compared, never overwritten.
    """
    target_file = target_dir / canonical_file.name

    if not target_file.exists():
        if create_missing:
            shutil.copy2(canonical_file, target_file)
            print(f"  Created: {target_file}")
            return (True, False)
        print(f"  MISSING: {target_file}")
        return (False, True)

    diffs = compare_structural(
        canonical_file.read_text(encoding="utf-8"),
        target_file.read_text(encoding="utf-8"),
    )

    if not diffs:
        print(f"  OK:      {target_file}")
        return (False, False)

    print(f"  DRIFT:   {target_file}")
    for line in diffs[:40]:
        print(f"    {line}")
    if len(diffs) > 40:
        print(f"    ... ({len(diffs) - 40} more)")
    return (False, True)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Validate editor tree-sitter queries against the canonical source",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Report structural drift without writing (default behaviour); exit 1 if drift",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Create a missing editor copy from the canonical file (never overwrites)",
    )

    args = parser.parse_args()
    create_missing = args.force

    query_files = get_query_files()
    if not query_files:
        print("No query files found in shared/queries/")
        sys.exit(1)

    print(f"Canonical queries: {', '.join(query_files)}")
    print()

    any_drift = False
    any_created = False

    for target_dir in TARGETS:
        print(f"Target: {target_dir}")
        if not target_dir.exists():
            print("  WARNING: Directory does not exist, skipping")
            print()
            continue

        for filename in query_files:
            created, drift = sync_file(CANONICAL_DIR / filename, target_dir, create_missing)
            any_created = any_created or created
            any_drift = any_drift or drift

        print()

    if any_created:
        print("Created missing editor copies from the canonical source.")

    if any_drift:
        print("Structural drift detected: a node type, terminal or predicate differs.")
        print("Capture-group renames and reordering are expected and ignored — reconcile")
        print("the target query by hand so its grammar coverage matches the canonical file.")
        sys.exit(1)

    print("No structural drift. Editor copies match the canonical query structurally.")


if __name__ == "__main__":
    main()
