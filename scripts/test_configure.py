#!/usr/bin/env python3
"""Unit tests for the preset resolution in ``scripts/configure.py``.

Covers the selection of the build preset that ``configure.py <preset> --build``
runs — including the multi-config ``msvc`` / ``xcode`` presets, whose build
presets are named ``<preset>-release`` / ``<preset>-debug`` — and checks the
real ``CMakePresets.json`` so every visible configure preset stays buildable.

Run from the repository root:
    python -m unittest discover -s scripts -p "test_configure.py"
"""

from __future__ import annotations

import unittest

import configure

BUILD_PRESETS = [
    {"name": "_base-build", "hidden": True, "jobs": 0},
    {
        "name": "default",
        "inherits": "_base-build",
        "configurePreset": "default",
        "configuration": "Release",
    },
    {
        "name": "msvc-release",
        "inherits": "_base-build",
        "configurePreset": "msvc",
        "configuration": "Release",
    },
    {
        "name": "msvc-debug",
        "inherits": "_base-build",
        "configurePreset": "msvc",
        "configuration": "Debug",
    },
    {"name": "_mc", "hidden": True, "configurePreset": "multi"},
    {"name": "multi-relwithdebinfo", "inherits": "_mc", "configuration": "RelWithDebInfo"},
]


class SelectBuildPresetTests(unittest.TestCase):
    def test_same_name_build_preset_is_preferred(self) -> None:
        self.assertEqual(configure.select_build_preset("default", BUILD_PRESETS), "default")

    def test_multi_config_preset_defaults_to_release(self) -> None:
        self.assertEqual(configure.select_build_preset("msvc", BUILD_PRESETS), "msvc-release")

    def test_config_selects_matching_build_preset_case_insensitively(self) -> None:
        self.assertEqual(
            configure.select_build_preset("msvc", BUILD_PRESETS, "debug"), "msvc-debug"
        )

    def test_unknown_config_returns_none(self) -> None:
        self.assertIsNone(configure.select_build_preset("msvc", BUILD_PRESETS, "MinSizeRel"))

    def test_preset_without_build_presets_returns_none(self) -> None:
        self.assertIsNone(configure.select_build_preset("missing", BUILD_PRESETS))

    def test_inherited_configure_preset_is_resolved(self) -> None:
        self.assertEqual(
            configure.build_presets_for("multi", BUILD_PRESETS),
            [("multi-relwithdebinfo", "RelWithDebInfo")],
        )
        self.assertEqual(
            configure.select_build_preset("multi", BUILD_PRESETS), "multi-relwithdebinfo"
        )

    def test_hidden_build_presets_are_ignored(self) -> None:
        names = [name for name, _ in configure.build_presets_for("multi", BUILD_PRESETS)]
        self.assertNotIn("_mc", names)


class RepositoryPresetsTests(unittest.TestCase):
    def test_every_configure_preset_has_a_default_build_preset(self) -> None:
        build_presets = configure.load_all_build_presets()
        for preset in configure.load_presets():
            with self.subTest(preset=preset["name"]):
                self.assertIsNotNone(configure.select_build_preset(preset["name"], build_presets))

    def test_multi_config_presets_build_release_by_default(self) -> None:
        build_presets = configure.load_all_build_presets()
        self.assertEqual(configure.select_build_preset("msvc", build_presets), "msvc-release")
        self.assertEqual(configure.select_build_preset("xcode", build_presets), "xcode-release")
        self.assertEqual(
            configure.select_build_preset("xcode", build_presets, "Debug"), "xcode-debug"
        )


if __name__ == "__main__":
    unittest.main()
