#!/usr/bin/env python3
"""Unit tests for the individual editor code generators.

``test_codegen_common`` pins the shared helper functions; these tests pin the
per-generator logic layered on top of them:

* the ``generate_vscode`` / ``generate_zed`` output-assembly functions and the
  small private helpers they use (``_vscode_property``, ``_vscode_keybindings``,
  ``_regex_literal``, ``_checksums_filename``), fed both the real canonical JSON
  and synthetic inputs that exercise the interesting branches;
* the ``package.json`` mutation logic, captured through a patched
  ``update_package_json`` so nothing touches disk;
* each generator's ``main()`` argument-dispatch wrapper (which outputs it
  writes for ``--all``; that an empty selection exits with the help text);
* the batch runners ``generate-all.py`` and ``ci-check-generated.py`` via a
  patched ``subprocess.run``;
* ``codegen_common.generated_files`` de-duplication.

These paths were previously covered only indirectly by the CI staleness gate,
which cannot localise a fault to a specific generator.

Run:
    python -m unittest test_generators
"""

from __future__ import annotations

import contextlib
import io
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import codegen_common as cc

# ── Load every generator module and its canonical input once ─────────────────

gen_config = cc.load_generator("generate-config.py")
gen_config_code = cc.load_generator("generate-config-code.py")
gen_download = cc.load_generator("generate-download-code.py")
gen_platform = cc.load_generator("generate-platform-code.py")
gen_keys = cc.load_generator("generate-keybindings.py")
gen_testdisc = cc.load_generator("generate-test-discovery.py")
gen_builtin = cc.load_generator("generate-builtin-types.py")
gen_all = cc.load_generator("generate-all.py")
ci_check = cc.load_generator("ci-check-generated.py")

DEFAULTS = cc.load_json(gen_config.DEFAULTS_PATH)
PLATFORM_MAP = cc.load_json(gen_platform.PLATFORM_MAP_PATH)
DOWNLOAD = cc.load_json(gen_download.DOWNLOAD_CONSTANTS_PATH)
KEYBINDINGS = cc.load_json(gen_keys.KEYBINDINGS_PATH)
TESTDISC = cc.load_json(gen_testdisc.PATTERN_PATH)
BUILTIN = cc.load_json(gen_builtin.TYPES_PATH)


class ConfigGenerator(unittest.TestCase):
    def test_vscode_constants(self) -> None:
        ts = gen_config.generate_vscode(DEFAULTS)
        self.assertTrue(ts.startswith("// AUTO-GENERATED from extensions/shared/defaults.json"))
        self.assertIn('export const GITHUB_REPO = "d3v0n5h1r3/luma";', ts)
        # Binary keys are upper-cased.
        self.assertIn('LSP: "luma_lsp",', ts)
        self.assertIn('INTERPRETER: "luma",', ts)
        # CONFIG_DEFAULTS is keyed by the luma-relative property name, so the
        # interpreter.path -> luma.path override collapses to "path".
        self.assertIn('"lsp.path":', ts)
        self.assertIn('"path":', ts)

    def test_zed_constants(self) -> None:
        rs = gen_config.generate_zed(DEFAULTS)
        self.assertIn('pub const GITHUB_REPO: &str = "d3v0n5h1r3/luma";', rs)
        self.assertIn('pub const BINARY_LSP: &str = "luma_lsp";', rs)
        # The unused interpreter constant carries a dead-code allow.
        self.assertIn('#[allow(dead_code)]\npub const BINARY_INTERPRETER: &str = "luma";', rs)
        self.assertIn("pub const AUTO_DOWNLOAD_ENABLED: bool = true;", rs)

    def test_zed_auto_download_flag_follows_json(self) -> None:
        data = {
            "github_repo": "x/y",
            "binaries": {"lsp": "luma_lsp"},
            "auto_download": {"enabled": {"zed": False}},
        }
        rs = gen_config.generate_zed(data)
        self.assertIn("pub const AUTO_DOWNLOAD_ENABLED: bool = false;", rs)


class ConfigCodeGenerator(unittest.TestCase):
    def test_vscode_property_minimal(self) -> None:
        spec = {"type": "boolean", "default": True, "description": "d", "vscode": {}}
        entry = gen_config_code._vscode_property(spec)
        self.assertEqual(entry, {"type": "boolean", "default": True, "markdownDescription": "d"})
        # Field order matches the manifest convention.
        self.assertEqual(list(entry), ["type", "default", "markdownDescription"])

    def test_vscode_property_overrides_and_fallbacks(self) -> None:
        spec = {
            "type": "string",
            "default": "off",
            "description": "canon",
            "minimum": 1,
            "enum": ["off", "on"],
            "vscode": {
                "type": "number",
                "default": 5,
                "maximum": 9,
                "markdownDescription": "md",
            },
        }
        entry = gen_config_code._vscode_property(spec)
        # vscode overrides win; minimum/enum fall back to the canonical spec.
        self.assertEqual(entry["type"], "number")
        self.assertEqual(entry["default"], 5)
        self.assertEqual(entry["minimum"], 1)
        self.assertEqual(entry["maximum"], 9)
        self.assertEqual(entry["enum"], ["off", "on"])
        self.assertEqual(entry["markdownDescription"], "md")
        self.assertEqual(
            list(entry), ["type", "default", "minimum", "maximum", "enum", "markdownDescription"]
        )

    def test_vscode_mutation_splices_properties_and_drops_comment(self) -> None:
        captured: dict = {}

        def fake_update(_root: Path, mutate) -> None:
            manifest = {"contributes": {"configuration": {"$comment": "TODO", "properties": {}}}}
            mutate(manifest)
            captured["manifest"] = manifest

        with mock.patch("codegen_common.update_package_json", side_effect=fake_update):
            gen_config_code.generate_vscode(DEFAULTS)

        configuration = captured["manifest"]["contributes"]["configuration"]
        self.assertNotIn("$comment", configuration)
        props = configuration["properties"]
        self.assertIn("luma.lsp.path", props)
        self.assertIn("luma.path", props)  # interpreter.path override
        self.assertEqual(props["luma.lsp.path"]["type"], "string")

    def test_config_accessor(self) -> None:
        ts = gen_config_code.generate_vscode_config_accessor(DEFAULTS)
        self.assertIn("export class LumaConfig {", ts)
        self.assertIn("export const luma_config = new LumaConfig();", ts)
        # Getter name is the snake_case canonical key; the lookup key is the
        # luma-relative property name.
        self.assertIn("get lsp_path(): string {", ts)
        self.assertIn(
            'return this.config.get<string>("lsp.path", CONFIG_DEFAULTS["lsp.path"]);', ts
        )
        # interpreter.path -> getter interpreter_path, lookup key "path".
        self.assertIn("get interpreter_path(): string {", ts)
        self.assertIn('return this.config.get<string>("path", CONFIG_DEFAULTS["path"]);', ts)

    def test_zed_default_constants(self) -> None:
        rs = gen_config_code.generate_zed(DEFAULTS)
        self.assertIn("#![allow(dead_code)]", rs)
        self.assertIn('pub const DEFAULT_LSP_PATH: &str = "";', rs)
        self.assertIn("pub const DEFAULT_LSP_AUTO_UPDATE: bool = true;", rs)
        self.assertIn("pub const DEFAULT_INLAY_HINTS_ENABLED: bool = false;", rs)
        # Schema-only settings (generated:false) are excluded.
        self.assertNotIn("DEFAULT_LSP_ENABLED", rs)


class DownloadCodeGenerator(unittest.TestCase):
    def test_checksums_filename_helper(self) -> None:
        self.assertEqual(gen_download._checksums_filename(DOWNLOAD), "SHA256SUMS")

    def test_vscode(self) -> None:
        ts = gen_download.generate_vscode(DOWNLOAD)
        self.assertIn('export const CHECKSUMS_FILENAME = "SHA256SUMS";', ts)

    def test_zed(self) -> None:
        rs = gen_download.generate_zed(DOWNLOAD)
        self.assertIn('pub const CHECKSUMS_FILENAME: &str = "SHA256SUMS";', rs)
        self.assertIn("#![allow(dead_code)]", rs)


class PlatformGenerator(unittest.TestCase):
    def test_vscode(self) -> None:
        ts = gen_platform.generate_vscode(PLATFORM_MAP)
        self.assertIn('"linux": {', ts)
        self.assertIn('"x86_64": "linux-x86_64.tar.gz",', ts)
        self.assertIn('darwin: "macos",', ts)
        self.assertIn('arm64: "aarch64",', ts)
        self.assertIn("export function getPlatformSuffix(): string | undefined {", ts)

    def test_zed_match_arms(self) -> None:
        rs = gen_platform.generate_zed(PLATFORM_MAP)
        self.assertIn('("linux", "x86_64") => Some("linux-x86_64.tar.gz"),', rs)
        self.assertIn('("windows", "aarch64") => Some("windows-aarch64.zip"),', rs)
        # The match must stay exhaustive with a wildcard arm.
        self.assertIn("_ => None,", rs)


class KeybindingsGenerator(unittest.TestCase):
    def test_vscode_keybindings_filters(self) -> None:
        data = {
            "keybindings": {
                "cat": [
                    {"action": "a", "vscode": {"key": None, "when": "x"}},  # only when -> dropped
                    {"action": "b"},  # no vscode block -> skipped
                    {"action": "c", "vscode": {"command": "luma.c"}},  # command only -> kept
                    {"action": "d", "vscode": {"key": "ctrl+d"}},  # key only -> kept
                    {
                        "action": "e",
                        "vscode": {
                            "command": "luma.e",
                            "key": "ctrl+e",
                            "mac": "cmd+e",
                            "when": "w",
                        },
                    },
                ]
            }
        }
        out = gen_keys._vscode_keybindings(data)
        self.assertEqual(len(out), 3)
        self.assertIn({"command": "luma.c"}, out)
        self.assertIn({"key": "ctrl+d"}, out)
        self.assertIn({"command": "luma.e", "key": "ctrl+e", "mac": "cmd+e", "when": "w"}, out)

    def test_vscode_mutation_from_canonical(self) -> None:
        captured: dict = {}

        def fake_update(_root: Path, mutate) -> None:
            manifest = {"contributes": {"keybindings": None}}
            mutate(manifest)
            captured["manifest"] = manifest

        with mock.patch("codegen_common.update_package_json", side_effect=fake_update):
            gen_keys.generate_vscode(KEYBINDINGS)

        bindings = captured["manifest"]["contributes"]["keybindings"]
        # Only the runner entries carry a command+key in the canonical file.
        commands = {b["command"] for b in bindings}
        self.assertEqual(commands, {"luma.runFile", "luma.runTests"})

    def test_zed_markdown_table(self) -> None:
        md = gen_keys.generate_zed(KEYBINDINGS)
        self.assertTrue(md.startswith("<!-- AUTO-GENERATED"))
        self.assertIn("| `goToDefinition` | Go to definition | lsp |", md)
        self.assertIn("| `runFile` | Run current Luma file | runner |", md)


class TestDiscoveryGenerator(unittest.TestCase):
    def test_regex_literal(self) -> None:
        self.assertEqual(
            gen_testdisc._regex_literal({"regex": "^@main\\s*$", "flags": "m"}),
            "/^@main\\s*$/m",
        )

    def test_vscode_factories(self) -> None:
        ts = gen_testdisc.generate_vscode(TESTDISC)
        self.assertIn("export function testFunctionPattern(): RegExp {", ts)
        self.assertIn("export function testAnnotationPattern(): RegExp {", ts)
        self.assertIn("export function mainAnnotationPattern(): RegExp {", ts)
        # Each factory returns a RegExp literal.
        self.assertIn("return /", ts)


class BuiltinTypesGenerator(unittest.TestCase):
    def test_vscode_set(self) -> None:
        ts = gen_builtin.generate_vscode(BUILTIN)
        self.assertIn("export const LUMA_BUILTIN_TYPE_SET = new Set([", ts)
        self.assertIn('    "boolean",', ts)
        self.assertIn('    "xml",', ts)
        self.assertTrue(ts.rstrip().endswith("]);"))


# ── main() argument-dispatch wrappers ────────────────────────────────────────

# (module, script-name, expected write_file basenames for --all, calls update_package_json?)
_MAIN_CASES = [
    (gen_config, "generate-config.py", {"config.ts", "config.rs"}, False),
    (
        gen_config_code,
        "generate-config-code.py",
        {"config-accessor.ts", "config_defaults.rs"},
        True,
    ),
    (
        gen_download,
        "generate-download-code.py",
        {"download-constants.ts", "download_constants.rs"},
        False,
    ),
    (gen_platform, "generate-platform-code.py", {"platform.ts", "platform.rs"}, False),
    (gen_keys, "generate-keybindings.py", {"KEYBINDINGS.md"}, True),
    (gen_testdisc, "generate-test-discovery.py", {"test-discovery.ts"}, False),
    (gen_builtin, "generate-builtin-types.py", {"builtin-types.ts"}, False),
]


class GeneratorMainDispatch(unittest.TestCase):
    def test_all_flag_writes_expected_outputs(self) -> None:
        for module, script, expected, uses_pkg in _MAIN_CASES:
            with self.subTest(script=script):
                with (
                    mock.patch("codegen_common.write_file") as write_file,
                    mock.patch("codegen_common.update_package_json") as update_pkg,
                    mock.patch.object(sys, "argv", [script, "--all"]),
                    contextlib.redirect_stdout(io.StringIO()),
                ):
                    module.main()

                written = {Path(call.args[0]).name for call in write_file.call_args_list}
                self.assertEqual(written, expected)
                self.assertEqual(update_pkg.called, uses_pkg)

    def test_no_editor_selected_exits_with_help(self) -> None:
        for module, script, _expected, _uses_pkg in _MAIN_CASES:
            with (
                self.subTest(script=script),
                mock.patch.object(sys, "argv", [script]),
                contextlib.redirect_stdout(io.StringIO()),
                self.assertRaises(SystemExit),
            ):
                module.main()


class BatchRunners(unittest.TestCase):
    def test_generate_all_runs_every_generator(self) -> None:
        with (
            mock.patch("subprocess.run", return_value=SimpleNamespace(returncode=0)) as run,
            contextlib.redirect_stdout(io.StringIO()),
        ):
            gen_all.main()  # returns normally on success
        self.assertEqual(run.call_count, len(cc.GENERATORS))
        for call in run.call_args_list:
            self.assertEqual(call.args[0][-1], "--all")

    def test_generate_all_fails_when_a_generator_fails(self) -> None:
        with (
            mock.patch("subprocess.run", return_value=SimpleNamespace(returncode=1)),
            contextlib.redirect_stdout(io.StringIO()),
            self.assertRaises(SystemExit) as ctx,
        ):
            gen_all.main()
        self.assertEqual(ctx.exception.code, 1)

    def test_ci_check_passes_when_no_diff(self) -> None:
        def run(cmd, **_kwargs):
            return SimpleNamespace(returncode=0, stdout="", stderr="")

        with (
            mock.patch("subprocess.run", side_effect=run),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            ci_check.main()  # no SystemExit on a clean tree

    def test_ci_check_fails_on_stale_generated_files(self) -> None:
        def run(cmd, **_kwargs):
            if cmd[0] == "git":
                return SimpleNamespace(
                    returncode=0, stdout="extensions/vscode/package.json\n", stderr=""
                )
            return SimpleNamespace(returncode=0, stdout="", stderr="")

        with (
            mock.patch("subprocess.run", side_effect=run),
            contextlib.redirect_stdout(io.StringIO()),
            self.assertRaises(SystemExit) as ctx,
        ):
            ci_check.main()
        self.assertEqual(ctx.exception.code, 1)

    def test_ci_check_fails_when_a_generator_fails(self) -> None:
        def run(cmd, **_kwargs):
            if cmd[0] == "git":
                return SimpleNamespace(returncode=0, stdout="", stderr="")
            return SimpleNamespace(returncode=1, stdout="", stderr="boom")

        with (
            mock.patch("subprocess.run", side_effect=run),
            contextlib.redirect_stdout(io.StringIO()),
            self.assertRaises(SystemExit) as ctx,
        ):
            ci_check.main()
        self.assertEqual(ctx.exception.code, 1)


class GeneratorRegistry(unittest.TestCase):
    def test_generated_files_dedup_and_membership(self) -> None:
        repo_root = Path(cc.SCRIPT_DIR).parent.parent
        files = cc.generated_files(repo_root)
        # package.json is written by two generators but must be listed once.
        self.assertEqual(files.count("extensions/vscode/package.json"), 1)
        for expected in (
            "extensions/vscode/src/generated/config.ts",
            "extensions/zed/src/generated/config.rs",
            "extensions/vscode/src/generated/platform.ts",
            "extensions/zed/KEYBINDINGS.md",
            "extensions/vscode/src/generated/builtin-types.ts",
        ):
            self.assertIn(expected, files)


if __name__ == "__main__":
    unittest.main()
