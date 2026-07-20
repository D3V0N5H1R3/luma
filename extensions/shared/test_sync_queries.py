#!/usr/bin/env python3
"""Unit tests for the structural query differ in ``sync-queries.py``.

``sync-queries.py`` guards the editor tree-sitter highlight queries against the
canonical source. Its correctness hinges on two hand-written routines with no
other coverage:

* ``tokenize`` — a small lexer that must keep grammar-meaningful structure
  (named nodes/fields, quoted terminals honouring escapes, ``#predicate``
  names) while dropping capture groups (``@…``), comments and punctuation.
* ``compare_structural`` — the unordered multiset diff that must ignore capture
  renames, reordering and reformatting yet still flag a genuinely added or
  removed node/terminal/predicate.

The script is not exercised by any CI workflow, so these tests are its only
automated guard.

Run:
    python -m unittest test_sync_queries
"""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).resolve().parent / "sync-queries.py"
_spec = importlib.util.spec_from_file_location("sync_queries", _SCRIPT)
assert _spec and _spec.loader
sync_queries = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sync_queries)


class Tokenize(unittest.TestCase):
    def test_named_node_and_field(self) -> None:
        # Punctuation is dropped; the field name and node name both survive.
        self.assertEqual(
            sync_queries.tokenize("(call name: (identifier))"),
            ["ID:call", "ID:name", "ID:identifier"],
        )

    def test_quoted_terminal(self) -> None:
        self.assertEqual(sync_queries.tokenize('"fn"'), ['STR:"fn"'])

    def test_escaped_quote_does_not_terminate_terminal(self) -> None:
        # A backslash-escaped quote stays inside the single terminal token.
        self.assertEqual(sync_queries.tokenize(r'"a\"b"'), [r'STR:"a\"b"'])

    def test_capture_groups_are_dropped(self) -> None:
        # The @capture name is the deliberately-ignored per-editor detail.
        self.assertEqual(
            sync_queries.tokenize("(identifier) @variable.builtin"),
            ["ID:identifier"],
        )

    def test_comments_are_dropped(self) -> None:
        self.assertEqual(
            sync_queries.tokenize("; a comment\n(comment)"),
            ["ID:comment"],
        )

    def test_predicate_name_is_kept(self) -> None:
        self.assertEqual(
            sync_queries.tokenize('(#match? @x "^[A-Z]")'),
            ["PRED:#match?", 'STR:"^[A-Z]"'],
        )


class CompareStructural(unittest.TestCase):
    def test_capture_rename_is_ignored(self) -> None:
        self.assertEqual(
            sync_queries.compare_structural("(identifier) @a", "(identifier) @b"),
            [],
        )

    def test_reordering_and_reformatting_is_ignored(self) -> None:
        canonical = '[\n  "fn"\n  "let"\n] @keyword'
        target = '["let" "fn"] @keyword.special'
        self.assertEqual(sync_queries.compare_structural(canonical, target), [])

    def test_node_missing_from_target_is_flagged(self) -> None:
        diffs = sync_queries.compare_structural("(identifier) (comment)", "(identifier)")
        self.assertEqual(diffs, ["- missing from target: node/field `comment`"])

    def test_terminal_extra_in_target_is_flagged(self) -> None:
        diffs = sync_queries.compare_structural('"fn"', '"fn" "let"')
        self.assertEqual(diffs, ['+ extra in target:     terminal "let"'])

    def test_predicate_drift_is_flagged(self) -> None:
        diffs = sync_queries.compare_structural("(#match? @x)", "(#eq? @x)")
        self.assertIn("- missing from target: predicate #match?", diffs)
        self.assertIn("+ extra in target:     predicate #eq?", diffs)

    def test_repeated_token_count_is_reported(self) -> None:
        diffs = sync_queries.compare_structural('"fn" "fn"', "")
        self.assertEqual(diffs, ['- missing from target: terminal "fn" (x2)'])


if __name__ == "__main__":
    unittest.main()
