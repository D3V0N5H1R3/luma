#!/usr/bin/env python3
"""Generate editor-native configuration code from defaults.json.

Complements generate-config.py (runtime constants) by producing the
configuration each editor embeds natively. Run both via: python generate-all.py

Outputs:
    --vscode → splices contributes.configuration.properties into
               extensions/vscode/package.json (in place)
               + extensions/vscode/src/generated/config-accessor.ts
                 (LumaConfig typed accessor + luma_config singleton)
    --zed    → extensions/zed/src/generated/config_defaults.rs
               Rust typed default constants
    --all    → all of the above

Usage:
    python generate-config-code.py --all
    python generate-config-code.py --vscode
"""

from __future__ import annotations

import codegen_common as cc

ROOT_DIR = cc.resolve_root()
DEFAULTS_PATH = cc.SCRIPT_DIR / "defaults.json"


def _vscode_property(spec: dict) -> dict:
    """Build a VS Code configuration property descriptor for a setting.

    Field order matches the manifest convention: type, default, [minimum],
    [maximum], [enum], markdownDescription. Numeric constraints and enums fall
    back to the canonical spec when not overridden in the ``vscode`` block.
    """
    vscode = spec["vscode"]
    entry: dict = {
        "type": vscode.get("type", spec["type"]),
        "default": vscode.get("default", spec["default"]),
    }
    for field in ("minimum", "maximum", "enum"):
        if field in vscode:
            entry[field] = vscode[field]
        elif field in spec:
            entry[field] = spec[field]
    entry["markdownDescription"] = vscode.get("markdownDescription", spec["description"])
    return entry


def generate_vscode(defaults: dict) -> None:
    """Splice the generated configuration properties into package.json."""
    properties = {
        property_key: _vscode_property(spec)
        for _key, property_key, spec in cc.vscode_settings(defaults)
    }

    def mutate(data: dict) -> None:
        configuration = data["contributes"]["configuration"]
        # Drop the legacy TODO marker now that generation is implemented.
        configuration.pop("$comment", None)
        configuration["properties"] = properties

    cc.update_package_json(ROOT_DIR, mutate)


def generate_vscode_config_accessor(defaults: dict) -> str:
    """Generate the strongly-typed ``LumaConfig`` accessor and ``luma_config`` singleton.

    One getter is emitted per VS Code-exposed setting. The getter name is the
    snake_case form of the canonical key (``inlayHints.enabled`` ->
    ``inlay_hints_enabled``); the lookup/default key is the VS Code property name
    with the ``luma.`` section prefix stripped (so ``interpreter.path`` ->
    ``luma.path`` -> ``path``), matching ``CONFIG_DEFAULTS`` in config.ts.
    """
    lines = [
        *cc.banner("//", "defaults.json", "generate-config-code.py", "vscode"),
        "",
        'import * as vscode from "vscode";',
        "",
        'import { CONFIG_SECTION } from "../utils/constants";',
        'import { CONFIG_DEFAULTS } from "./config";',
        "",
        "/**",
        " * Strongly-typed accessor for Luma extension configuration.",
        " *",
        " * Reads values from the `luma` configuration section. Each getter",
        " * corresponds to a setting declared in package.json (generated from",
        " * defaults.json) and returns the workspace-effective value",
        " * (user, then workspace, then the default).",
        " */",
        "export class LumaConfig {",
        "    private get config(): vscode.WorkspaceConfiguration {",
        "        return vscode.workspace.getConfiguration(CONFIG_SECTION);",
        "    }",
    ]
    for key, property_key, spec in cc.vscode_settings(defaults):
        getter = cc.snake_accessor(key)
        config_key = cc.vscode_config_key(property_key)
        ts = cc.ts_type(spec["type"])
        lines.append("")
        lines.append(f"    /** {spec['description']} */")
        lines.append(f"    get {getter}(): {ts} {{")
        lines.append(
            f'        return this.config.get<{ts}>("{config_key}", CONFIG_DEFAULTS["{config_key}"]);'  # noqa: E501
        )
        lines.append("    }")
    lines.append("}")
    lines.append("")
    lines.append("/** Singleton instance of the Luma configuration accessor. */")
    lines.append("export const luma_config = new LumaConfig();")
    lines.append("")

    return "\n".join(lines)


def generate_zed(defaults: dict) -> str:
    """Generate Rust typed default constants for each setting."""
    lines = [
        *cc.banner("//", "defaults.json", "generate-config-code.py", "zed", notice="DO NOT EDIT."),
        "",
        "#![allow(dead_code)]",
        "",
    ]
    for key, spec in cc.generated_settings(defaults):
        const_name = f"DEFAULT_{cc.dotted_to_screaming_snake(key)}"
        lines.append(f"/// {spec['description']}")
        lines.append(
            f"pub const {const_name}: {cc.rust_type(spec['type'])} = {cc.rust_literal(spec['default'], spec['type'])};"  # noqa: E501
        )
        lines.append("")

    return "\n".join(lines)


GENERATED_OUTPUTS = [
    ROOT_DIR / "vscode" / "package.json",
    ROOT_DIR / "vscode" / "src" / "generated" / "config-accessor.ts",
    ROOT_DIR / "zed" / "src" / "generated" / "config_defaults.rs",
]


def main() -> None:
    parser = cc.editor_arg_parser("Generate editor-native config code from defaults.json")
    args = parser.parse_args()

    editors = cc.require_editors(parser, cc.selected_editors(args))

    defaults = cc.load_json(DEFAULTS_PATH)

    if "vscode" in editors:
        generate_vscode(defaults)
        cc.write_file(
            ROOT_DIR / "vscode" / "src" / "generated" / "config-accessor.ts",
            generate_vscode_config_accessor(defaults),
        )
    if "zed" in editors:
        cc.write_file(
            ROOT_DIR / "zed" / "src" / "generated" / "config_defaults.rs", generate_zed(defaults)
        )

    print("Config code generation complete.")


if __name__ == "__main__":
    main()
