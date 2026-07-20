#!/usr/bin/env python3
"""Generate runtime configuration constants from defaults.json.

Ensures VS Code and Zed extensions all share the same repository,
binary names, and default values.

Companion script: generate-config-code.py generates editor-native
configuration fragments (package.json properties, defaults tables).
Run both via: python generate-all.py

Outputs:
    --vscode → extensions/vscode/src/generated/config.ts
    --zed    → extensions/zed/src/generated/config.rs
    --all    → all of the above

Usage:
    python generate-config.py --all
"""

from __future__ import annotations

import codegen_common as cc

ROOT_DIR = cc.resolve_root()
DEFAULTS_PATH = cc.SCRIPT_DIR / "defaults.json"


def generate_vscode(defaults: dict) -> str:
    """Generate TypeScript configuration constants for the VS Code extension."""
    lines = [
        *cc.banner("//", "defaults.json", "generate-config.py", "vscode"),
        "",
        f'export const GITHUB_REPO = "{defaults["github_repo"]}";',
        "",
        "export const BINARY_NAMES = {",
    ]
    for key, value in defaults["binaries"].items():
        lines.append(f'    {key.upper()}: "{value}",')
    lines.append("} as const;")
    lines.append("")

    lines.append(
        "/** Default values for VS Code settings, keyed by their `luma.`-relative name. */"
    )
    lines.append("export const CONFIG_DEFAULTS = {")
    for _key, property_key, spec in cc.vscode_settings(defaults):
        config_key = cc.vscode_config_key(property_key)
        lines.append(f'    "{config_key}": {cc.ts_literal(spec["default"])},')
    lines.append("} as const;")
    lines.append("")

    return "\n".join(lines)


def generate_zed(defaults: dict) -> str:
    """Generate Rust configuration constants for the Zed extension.

    Only emits what the Zed extension actually uses: binary names, the GitHub
    repo, and the auto-download flag.
    """
    lines = [
        *cc.banner("//", "defaults.json", "generate-config.py", "zed"),
        "",
        f'pub const GITHUB_REPO: &str = "{defaults["github_repo"]}";',
        "",
    ]
    for key, value in defaults["binaries"].items():
        # BINARY_INTERPRETER is generated for completeness (future run configurations)
        # but the Zed extension does not currently reference it.
        allow = "#[allow(dead_code)]\n" if key == "interpreter" else ""
        lines.append(f'{allow}pub const BINARY_{key.upper()}: &str = "{value}";')
    lines.append("")

    auto_dl = "true" if defaults["auto_download"]["enabled"]["zed"] else "false"
    lines.append(f"pub const AUTO_DOWNLOAD_ENABLED: bool = {auto_dl};")
    lines.append("")

    return "\n".join(lines)


OUTPUTS = {
    "vscode": (ROOT_DIR / "vscode" / "src" / "generated" / "config.ts", generate_vscode),
    "zed": (ROOT_DIR / "zed" / "src" / "generated" / "config.rs", generate_zed),
}

GENERATED_OUTPUTS = [path for path, _ in OUTPUTS.values()]


def main() -> None:
    cc.run_generator(
        "Generate config constants from defaults.json",
        DEFAULTS_PATH,
        OUTPUTS,
        "Config generation complete.",
    )


if __name__ == "__main__":
    main()
