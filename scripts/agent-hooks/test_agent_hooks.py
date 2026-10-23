#!/usr/bin/env python3
"""Unit tests for the agent hooks in ``scripts/agent-hooks/``.

Covers the contract each hook has with the agent runtimes:

* ``hook_common`` — payload normalisation across the PascalCase (snake_case)
  and camelCase formats, patch-format (``apply_patch``) path extraction, and
  the duplicate-registration check.
* ``protect_vendored_paths.py`` — run end-to-end as a subprocess, exactly as the
  runtime runs it: deny decisions on stdout, silence otherwise, always exit 0.
* ``format_cpp_on_edit.py`` — run in-process with ``clang-format`` mocked, so
  the tests need no formatter installed.
* The registrations in ``.github/hooks/*.json`` and ``.claude/settings.json``.

Run from the repository root or from this directory:
    python -m unittest discover -s scripts/agent-hooks -p "test_*.py"
"""

from __future__ import annotations

import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HOOKS_DIR = Path(__file__).resolve().parent
REPO_ROOT = HOOKS_DIR.parent.parent
sys.path.insert(0, str(HOOKS_DIR))

import format_cpp_on_edit  # noqa: E402
import hook_common  # noqa: E402

GUARD_SCRIPT = HOOKS_DIR / "protect_vendored_paths.py"

ADD_PATCH = "*** Begin Patch\n*** Add File: external/new.c\n+int x;\n*** End Patch\n"
UPDATE_PATCH = (
    "*** Begin Patch\n"
    "*** Update File: core/a.cpp\n"
    "*** Move to: core/b.cpp\n"
    "@@\n-old\n+new\n"
    "*** Delete File: core/c.cpp\n"
    "*** End Patch\n"
)


class HookCommonTest(unittest.TestCase):
    def test_tool_name_reads_both_formats(self) -> None:
        self.assertEqual(hook_common.get_tool_name({"tool_name": "Edit"}), "Edit")
        self.assertEqual(hook_common.get_tool_name({"toolName": "edit"}), "edit")
        self.assertIsNone(hook_common.get_tool_name({"tool_name": 3}))

    def test_tool_input_decodes_json_string_tool_args(self) -> None:
        payload = {"toolArgs": json.dumps({"path": "a.cpp"})}
        self.assertEqual(hook_common.get_tool_input(payload), {"path": "a.cpp"})

    def test_tool_input_keeps_raw_patch_string(self) -> None:
        self.assertEqual(hook_common.get_tool_input({"tool_input": ADD_PATCH}), ADD_PATCH)

    def test_patch_paths_cover_every_header(self) -> None:
        self.assertEqual(
            hook_common.patch_paths(UPDATE_PATCH), ["core/a.cpp", "core/b.cpp", "core/c.cpp"]
        )

    def test_patch_paths_require_begin_marker(self) -> None:
        self.assertEqual(hook_common.patch_paths("*** Add File: external/x.c\n"), [])

    def test_candidate_paths_from_path_keys_and_edits(self) -> None:
        tool_input = {"file_path": "a.cpp", "edits": [{"path": "b.cpp"}, {"other": 1}]}
        self.assertEqual(hook_common.candidate_paths(tool_input), ["a.cpp", "b.cpp"])

    def test_candidate_paths_from_patch_string_and_field(self) -> None:
        self.assertEqual(hook_common.candidate_paths(ADD_PATCH), ["external/new.c"])
        self.assertEqual(hook_common.candidate_paths({"input": ADD_PATCH}), ["external/new.c"])

    def test_candidate_paths_ignore_patch_like_file_content(self) -> None:
        tool_input = {"path": "notes.md", "file_text": "*** Add File: external/x.c\n"}
        self.assertEqual(hook_common.candidate_paths(tool_input), ["notes.md"])

    def test_shadowed_only_for_claude_copy_under_copilot(self) -> None:
        copilot_env = {"COPILOT_PROJECT_DIR": "/repo"}
        flag = [hook_common.CLAUDE_SETTINGS_FLAG]
        shadowed = hook_common.is_shadowed_registration
        self.assertTrue(shadowed({}, flag, environ=copilot_env))
        self.assertFalse(shadowed({}, [], environ=copilot_env))
        self.assertFalse(shadowed({}, flag, environ={}))
        self.assertFalse(shadowed({"transcript_path": "/t.jsonl"}, flag, environ=copilot_env))

    def test_read_payload_decodes_utf8_whatever_the_stdin_codec(self) -> None:
        # Windows decodes piped stdin with the ANSI code page; the runtime sends UTF-8.
        payload = {"path": "external/café.c"}
        raw = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        stdin = io.TextIOWrapper(io.BytesIO(raw), encoding="cp1252", errors="surrogateescape")
        with mock.patch.object(sys, "stdin", stdin):
            self.assertEqual(hook_common.read_payload(), payload)

    def test_read_only_call_is_only_the_view_command(self) -> None:
        self.assertTrue(hook_common.is_read_only_call({"command": "view", "path": "a.cpp"}))
        self.assertFalse(hook_common.is_read_only_call({"command": "create", "path": "a.cpp"}))
        self.assertFalse(hook_common.is_read_only_call({"path": "a.cpp"}))
        self.assertFalse(hook_common.is_read_only_call({"command": ["git", "status"]}))
        self.assertFalse(hook_common.is_read_only_call(ADD_PATCH))


class ProtectVendoredPathsTest(unittest.TestCase):
    def setUp(self) -> None:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.repo = Path(temp.name).resolve()
        (self.repo / ".git").mkdir()
        (self.repo / "external").mkdir()
        (self.repo / "core").mkdir()

    def run_guard(
        self,
        payload: object,
        *,
        args: list[str] | None = None,
        env: dict[str, str] | None = None,
    ) -> str:
        """Run the guard like the runtime does — UTF-8 JSON on stdin — and return its stdout."""
        stdin = payload if isinstance(payload, str) else json.dumps(payload, ensure_ascii=False)
        child_env = {k: v for k, v in os.environ.items() if k != "COPILOT_PROJECT_DIR"}
        child_env.update(env or {})
        result = subprocess.run(
            [sys.executable, str(GUARD_SCRIPT), *(args or [])],
            input=stdin.encode("utf-8"),
            capture_output=True,
            env=child_env,
            timeout=30,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.decode("utf-8").strip()

    def payload(self, tool_name: str, tool_input: object) -> dict[str, object]:
        return {"cwd": str(self.repo), "tool_name": tool_name, "tool_input": tool_input}

    def assert_denied(self, stdout: str) -> None:
        decision = json.loads(stdout)
        self.assertEqual(decision["permissionDecision"], "deny")
        self.assertTrue(decision["permissionDecisionReason"])
        self.assertEqual(decision["hookSpecificOutput"]["permissionDecision"], "deny")

    def test_denies_write_into_external(self) -> None:
        self.assert_denied(self.run_guard(self.payload("Write", {"path": "external/x.c"})))

    def test_denial_names_non_ascii_target_intact(self) -> None:
        stdout = self.run_guard(self.payload("Write", {"path": "external/café.c"}))
        self.assert_denied(stdout)
        self.assertIn("external/café.c", json.loads(stdout)["permissionDecisionReason"])

    def test_denies_traversal_and_case_variants(self) -> None:
        stdout = self.run_guard(self.payload("Edit", {"path": "core/../External/x.c"}))
        self.assert_denied(stdout)

    def test_allows_edits_elsewhere(self) -> None:
        self.assertEqual(self.run_guard(self.payload("Edit", {"path": "core/x.cpp"})), "")

    def test_allows_reads_of_external(self) -> None:
        self.assertEqual(self.run_guard(self.payload("Read", {"path": "external/x.c"})), "")

    def test_allows_editor_view_command_on_external(self) -> None:
        tool_input = {"command": "view", "path": "external/x.c"}
        self.assertEqual(self.run_guard(self.payload("str_replace_editor", tool_input)), "")
        self.assertEqual(self.run_guard(self.payload("Edit", tool_input)), "")

    def test_denies_editor_create_command_into_external(self) -> None:
        tool_input = {"command": "create", "path": "external/x.c", "file_text": "int x;\n"}
        self.assert_denied(self.run_guard(self.payload("str_replace_editor", tool_input)))

    def test_denies_patch_string_into_external(self) -> None:
        self.assert_denied(self.run_guard(self.payload("Edit", ADD_PATCH)))

    def test_denies_patch_field_into_external(self) -> None:
        self.assert_denied(self.run_guard(self.payload("apply_patch", {"input": ADD_PATCH})))

    def test_denies_camel_case_payload(self) -> None:
        payload = {
            "cwd": str(self.repo),
            "toolName": "create",
            "toolArgs": json.dumps({"path": "external/x.c"}),
        }
        self.assert_denied(self.run_guard(payload))

    def test_claude_copy_stands_down_under_copilot(self) -> None:
        payload = self.payload("Write", {"path": "external/x.c"})
        args = [hook_common.CLAUDE_SETTINGS_FLAG]
        env = {"COPILOT_PROJECT_DIR": str(self.repo)}
        self.assertEqual(self.run_guard(payload, args=args, env=env), "")

    def test_claude_copy_still_guards_under_claude_code(self) -> None:
        payload = self.payload("Write", {"path": "external/x.c"})
        payload["transcript_path"] = str(self.repo / "t.jsonl")
        self.assert_denied(self.run_guard(payload, args=[hook_common.CLAUDE_SETTINGS_FLAG]))

    def test_malformed_payload_is_a_no_op(self) -> None:
        self.assertEqual(self.run_guard("not json"), "")
        self.assertEqual(self.run_guard(""), "")


class FormatCppOnEditTest(unittest.TestCase):
    def setUp(self) -> None:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        (self.root / ".git").mkdir()
        self.source = self.root / "a.cpp"
        self.source.write_text("int  x;\n", encoding="utf-8")

    def run_formatter(
        self,
        payload: dict[str, object],
        formatted_text: str | None,
        *,
        args: list[str] | None = None,
    ) -> tuple[str, mock.Mock]:
        """Run ``main`` with a fake clang-format that writes ``formatted_text``.

        The payload arrives as UTF-8 bytes behind a cp1252 text layer, as on Windows.
        """

        def fake_run(command: list[str], **_kwargs: object) -> subprocess.CompletedProcess[str]:
            if formatted_text is not None:
                Path(command[-1]).write_text(formatted_text, encoding="utf-8")
            return subprocess.CompletedProcess(command, 0, "", "")

        raw = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        stdin = io.TextIOWrapper(io.BytesIO(raw), encoding="cp1252", errors="surrogateescape")
        stdout = io.StringIO()
        with (
            mock.patch.object(format_cpp_on_edit.shutil, "which", return_value="clang-format"),
            mock.patch.object(format_cpp_on_edit, "clang_format_is_usable", return_value=True),
            mock.patch.object(
                format_cpp_on_edit.subprocess, "run", side_effect=fake_run
            ) as run_mock,
            mock.patch.object(sys, "stdin", stdin),
            mock.patch.dict(os.environ, {"COPILOT_PROJECT_DIR": str(self.root)}),
            contextlib.redirect_stdout(stdout),
        ):
            self.assertEqual(format_cpp_on_edit.main(args or []), 0)
        return stdout.getvalue().strip(), run_mock

    def payload(self, tool_input: object) -> dict[str, object]:
        return {"cwd": str(self.root), "tool_name": "Edit", "tool_input": tool_input}

    def test_reports_reformatted_file_to_the_model(self) -> None:
        stdout, _ = self.run_formatter(self.payload({"path": "a.cpp"}), "int x;\n")
        output = json.loads(stdout)
        self.assertIn(str(self.source), output["additionalContext"])
        self.assertEqual(
            output["hookSpecificOutput"]["additionalContext"], output["additionalContext"]
        )
        self.assertEqual(self.source.read_text(encoding="utf-8"), "int x;\n")

    def test_silent_when_already_formatted(self) -> None:
        stdout, run_mock = self.run_formatter(self.payload({"path": "a.cpp"}), None)
        self.assertEqual(stdout, "")
        run_mock.assert_called_once()

    def test_formats_patch_targets(self) -> None:
        patch = "*** Begin Patch\n*** Update File: a.cpp\n@@\n-a\n+b\n*** End Patch\n"
        stdout, _ = self.run_formatter(self.payload(patch), "int x;\n")
        self.assertIn(str(self.source), json.loads(stdout)["additionalContext"])

    def test_ignores_non_cpp_files(self) -> None:
        (self.root / "notes.md").write_text("x\n", encoding="utf-8")
        stdout, run_mock = self.run_formatter(self.payload({"path": "notes.md"}), "y\n")
        self.assertEqual(stdout, "")
        run_mock.assert_not_called()

    def test_formats_non_ascii_file_name(self) -> None:
        source = self.root / "café.cpp"
        source.write_text("int  x;\n", encoding="utf-8")
        stdout, _ = self.run_formatter(self.payload({"path": "café.cpp"}), "int x;\n")
        self.assertIn(str(source), json.loads(stdout)["additionalContext"])

    def test_ignores_editor_view_command(self) -> None:
        tool_input = {"command": "view", "path": "a.cpp"}
        stdout, run_mock = self.run_formatter(self.payload(tool_input), "int x;\n")
        self.assertEqual(stdout, "")
        run_mock.assert_not_called()

    def test_ignores_files_outside_the_repository(self) -> None:
        other = tempfile.TemporaryDirectory()
        self.addCleanup(other.cleanup)
        outside = Path(other.name).resolve() / "b.cpp"
        outside.write_text("int  y;\n", encoding="utf-8")
        stdout, run_mock = self.run_formatter(self.payload({"path": str(outside)}), "int y;\n")
        self.assertEqual(stdout, "")
        run_mock.assert_not_called()

    def test_ignores_vendored_files(self) -> None:
        (self.root / "external").mkdir()
        (self.root / "external" / "v.cpp").write_text("int  v;\n", encoding="utf-8")
        stdout, run_mock = self.run_formatter(self.payload({"path": "external/v.cpp"}), "int v;\n")
        self.assertEqual(stdout, "")
        run_mock.assert_not_called()

    def test_claude_copy_stands_down_under_copilot(self) -> None:
        stdout, run_mock = self.run_formatter(
            self.payload({"path": "a.cpp"}), "int x;\n", args=[hook_common.CLAUDE_SETTINGS_FLAG]
        )
        self.assertEqual(stdout, "")
        run_mock.assert_not_called()

    def test_missing_clang_format_is_a_no_op(self) -> None:
        stdout = io.StringIO()
        payload = json.dumps(self.payload({"path": "a.cpp"}))
        with (
            mock.patch.object(format_cpp_on_edit.shutil, "which", return_value=None),
            mock.patch.object(sys, "stdin", io.StringIO(payload)),
            contextlib.redirect_stdout(stdout),
        ):
            self.assertEqual(format_cpp_on_edit.main([]), 0)
        self.assertEqual(stdout.getvalue(), "")


class RegistrationTest(unittest.TestCase):
    def test_github_hook_configs(self) -> None:
        configs = sorted((REPO_ROOT / ".github" / "hooks").glob("*.json"))
        self.assertTrue(configs)
        for config in configs:
            with self.subTest(config=config.name):
                data = json.loads(config.read_text(encoding="utf-8"))
                self.assertEqual(data["version"], 1)
                for entries in data["hooks"].values():
                    for entry in entries:
                        self.assertEqual(entry["cwd"], ".")
                        self.assertIn("timeoutSec", entry)
                        matcher = entry["matcher"].split("|")
                        for tool in ("Edit", "Write", "edit", "create", "apply_patch"):
                            self.assertIn(tool, matcher)
                        for shell in ("bash", "powershell"):
                            command = entry[shell]
                            # Fail open when the interpreter is missing: a non-zero
                            # PreToolUse exit would deny every matching tool call.
                            self.assertTrue(command.endswith("; exit 0"), command)
                            script = command.split()[1].rstrip(";")
                            self.assertTrue((REPO_ROOT / script).is_file(), script)

    def test_claude_settings_mark_their_registrations(self) -> None:
        settings = json.loads((REPO_ROOT / ".claude" / "settings.json").read_text("utf-8"))
        for groups in settings["hooks"].values():
            for group in groups:
                for hook in group["hooks"]:
                    command = hook["command"]
                    self.assertEqual(command.count(hook_common.CLAUDE_SETTINGS_FLAG), 2, command)


if __name__ == "__main__":
    unittest.main()
