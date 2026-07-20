#!/usr/bin/env python3
"""Unit tests for the pure helpers in ``codegen_common``.

These functions are the shared foundation of every editor code generator, so a
bug here silently corrupts *all* generated outputs. The staleness gate
(``ci-check-generated.py``) and the cross-editor validators only compare the
regenerated files against the checked-in copies or against the canonical JSON,
so a *consistent* logic bug (wrong in both the checked-in file and the fresh
output) slips through. These tests pin the expected transforms directly.

Run:
    python -m unittest test_codegen_common
"""

from __future__ import annotations

import argparse
import contextlib
import io
import unittest

import codegen_common as cc


class CaseHelpers(unittest.TestCase):
    def test_camel_to_snake(self) -> None:
        self.assertEqual(cc.camel_to_snake("inlayHints"), "inlay_hints")
        self.assertEqual(cc.camel_to_snake("autoUpdate"), "auto_update")
        self.assertEqual(cc.camel_to_snake("path"), "path")
        # A leading capital must not produce a leading underscore.
        self.assertEqual(cc.camel_to_snake("Foo"), "foo")
        self.assertEqual(cc.camel_to_snake("ABC"), "a_b_c")
        self.assertEqual(cc.camel_to_snake(""), "")

    def test_dotted_to_screaming_snake(self) -> None:
        self.assertEqual(cc.dotted_to_screaming_snake("lsp.autoUpdate"), "LSP_AUTO_UPDATE")
        self.assertEqual(cc.dotted_to_screaming_snake("inlayHints.enabled"), "INLAY_HINTS_ENABLED")
        self.assertEqual(cc.dotted_to_screaming_snake("interpreter.path"), "INTERPRETER_PATH")
        self.assertEqual(cc.dotted_to_screaming_snake("codeLens.enabled"), "CODE_LENS_ENABLED")
        # A dot already adjacent to a capital must not double the separator.
        self.assertEqual(cc.dotted_to_screaming_snake("a.Bc"), "A_BC")

    def test_snake_accessor(self) -> None:
        self.assertEqual(cc.snake_accessor("inlayHints.enabled"), "inlay_hints_enabled")
        self.assertEqual(cc.snake_accessor("lsp.autoUpdate"), "lsp_auto_update")
        self.assertEqual(cc.snake_accessor("interpreter.path"), "interpreter_path")


class LiteralEmitters(unittest.TestCase):
    def test_rust_type(self) -> None:
        self.assertEqual(cc.rust_type("boolean"), "bool")
        self.assertEqual(cc.rust_type("string"), "&str")
        self.assertEqual(cc.rust_type("integer"), "i64")
        self.assertEqual(cc.rust_type("number"), "f64")
        # Unknown type names fall back to the string type.
        self.assertEqual(cc.rust_type("array"), "&str")

    def test_rust_literal(self) -> None:
        self.assertEqual(cc.rust_literal(True, "boolean"), "true")
        self.assertEqual(cc.rust_literal(False, "boolean"), "false")
        self.assertEqual(cc.rust_literal(42, "integer"), "42")
        self.assertEqual(cc.rust_literal(3.5, "number"), "3.5")
        self.assertEqual(cc.rust_literal("info", "string"), '"info"')

    def test_ts_type(self) -> None:
        self.assertEqual(cc.ts_type("boolean"), "boolean")
        self.assertEqual(cc.ts_type("string"), "string")
        self.assertEqual(cc.ts_type("integer"), "number")
        self.assertEqual(cc.ts_type("number"), "number")
        self.assertEqual(cc.ts_type("unknown"), "string")

    def test_ts_literal(self) -> None:
        self.assertEqual(cc.ts_literal(True), "true")
        self.assertEqual(cc.ts_literal(False), "false")
        self.assertEqual(cc.ts_literal("hi"), '"hi"')
        self.assertEqual(cc.ts_literal(3000), "3000")
        self.assertEqual(cc.ts_literal([]), "[]")


class Banner(unittest.TestCase):
    def test_line_comment_banner(self) -> None:
        self.assertEqual(
            cc.banner("//", "defaults.json", "generate-config.py", "vscode"),
            [
                "// AUTO-GENERATED from extensions/shared/defaults.json",
                "// Do not edit manually. Run: python generate-config.py --vscode",
            ],
        )

    def test_wrapping_comment_and_notice_override(self) -> None:
        self.assertEqual(
            cc.banner(
                "<!--",
                "keybindings.json",
                "generate-keybindings.py",
                "zed",
                notice="DO NOT EDIT.",
                comment_close="-->",
            ),
            [
                "<!-- AUTO-GENERATED from extensions/shared/keybindings.json -->",
                "<!-- DO NOT EDIT. Run: python generate-keybindings.py --zed -->",
            ],
        )


class VscodeKeyHelpers(unittest.TestCase):
    def test_vscode_property_key_default(self) -> None:
        self.assertEqual(cc.vscode_property_key("lsp.path", {"vscode": {}}), "luma.lsp.path")
        self.assertEqual(cc.vscode_property_key("x", {}), "luma.x")

    def test_vscode_property_key_override(self) -> None:
        spec = {"vscode": {"key": "luma.path"}}
        self.assertEqual(cc.vscode_property_key("interpreter.path", spec), "luma.path")

    def test_vscode_config_key(self) -> None:
        self.assertEqual(cc.vscode_config_key("luma.path"), "path")
        self.assertEqual(cc.vscode_config_key("luma.lsp.path"), "lsp.path")
        # Property keys that are not under the luma section are returned as-is.
        self.assertEqual(cc.vscode_config_key("editor.tabSize"), "editor.tabSize")


class SettingsFilters(unittest.TestCase):
    DEFAULTS = {
        "settings": {
            "a": {"type": "string", "default": "x", "description": "d"},
            "b": {
                "type": "boolean",
                "default": True,
                "description": "d",
                "generated": False,
                "vscode": {},
            },
            "interpreter.path": {
                "type": "string",
                "default": "",
                "description": "d",
                "vscode": {"key": "luma.path"},
            },
        }
    }

    def test_is_generated(self) -> None:
        self.assertTrue(cc.is_generated({"type": "string"}))
        self.assertTrue(cc.is_generated({"generated": True}))
        self.assertFalse(cc.is_generated({"generated": False}))

    def test_generated_settings_excludes_schema_only(self) -> None:
        keys = [key for key, _spec in cc.generated_settings(self.DEFAULTS)]
        self.assertEqual(keys, ["a", "interpreter.path"])

    def test_vscode_settings_requires_vscode_block(self) -> None:
        emitted = {key: prop for key, prop, _spec in cc.vscode_settings(self.DEFAULTS)}
        # "a" has no vscode block, so it is not exposed to VS Code.
        self.assertEqual(emitted, {"b": "luma.b", "interpreter.path": "luma.path"})


class EditorSelection(unittest.TestCase):
    def test_selected_editors_all(self) -> None:
        args = argparse.Namespace(all=True, vscode=False, zed=False)
        self.assertEqual(cc.selected_editors(args), ["vscode", "zed"])

    def test_selected_editors_subset(self) -> None:
        args = argparse.Namespace(all=False, vscode=True, zed=False)
        self.assertEqual(cc.selected_editors(args), ["vscode"])

    def test_selected_editors_none(self) -> None:
        args = argparse.Namespace(all=False, vscode=False, zed=False)
        self.assertEqual(cc.selected_editors(args), [])

    def test_require_editors_passes_through_truthy(self) -> None:
        parser = argparse.ArgumentParser()
        self.assertEqual(cc.require_editors(parser, ["vscode"]), ["vscode"])

    def test_require_editors_exits_when_empty(self) -> None:
        parser = argparse.ArgumentParser()
        # require_editors prints the parser help before exiting; swallow it so
        # the test output stays clean.
        with self.assertRaises(SystemExit), contextlib.redirect_stdout(io.StringIO()):
            cc.require_editors(parser, [])


if __name__ == "__main__":
    unittest.main()
