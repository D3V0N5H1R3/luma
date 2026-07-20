#!/usr/bin/env python3
"""Generate platform detection code from platform-map.json.

Produces type-safe platform mappings for each editor extension language.

Outputs:
    --vscode → extensions/vscode/src/generated/platform.ts
    --zed    → extensions/zed/src/generated/platform.rs
    --all    → all of the above

Usage:
    python generate-platform-code.py --all
    python generate-platform-code.py --vscode
"""

from __future__ import annotations

import codegen_common as cc

ROOT_DIR = cc.resolve_root()
PLATFORM_MAP_PATH = cc.SCRIPT_DIR / "platform-map.json"


def generate_vscode(platform_map: dict) -> str:
    """Generate the TypeScript platform mapping module."""
    lines = [
        *cc.banner("//", "platform-map.json", "generate-platform-code.py", "vscode"),
        "",
        "export const PLATFORM_MAP: Record<string, Record<string, string>> = {",
    ]
    for os_name, arches in platform_map.items():
        lines.append(f'    "{os_name}": {{')
        for arch, suffix in arches.items():
            lines.append(f'        "{arch}": "{suffix}",')
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("/** Maps Node.js process.platform to canonical OS name. */")
    lines.append("export const OS_MAP: Record<string, string> = {")
    lines.append('    linux: "linux",')
    lines.append('    darwin: "macos",')
    lines.append('    win32: "windows",')
    lines.append("};")
    lines.append("")
    lines.append("/** Maps Node.js process.arch to canonical architecture name. */")
    lines.append("export const ARCH_MAP: Record<string, string> = {")
    lines.append('    x64: "x86_64",')
    lines.append('    arm64: "aarch64",')
    lines.append("};")
    lines.append("")
    lines.append("/** Get the platform-specific archive suffix for the current platform. */")
    lines.append("export function getPlatformSuffix(): string | undefined {")
    lines.append("    const os = OS_MAP[process.platform];")
    lines.append("    const arch = ARCH_MAP[process.arch];")
    lines.append("    return os && arch ? PLATFORM_MAP[os]?.[arch] : undefined;")
    lines.append("}")
    lines.append("")

    return "\n".join(lines)


def generate_zed(platform_map: dict) -> str:
    """Generate the Rust platform mapping module using a match expression.

    A `match (os, arch)` on `(&str, &str)` is preferred over a slice + linear
    scan: it is exhaustive, zero-overhead, and the compiler warns if the JSON
    adds a new platform that the generated code does not cover.
    """
    lines = [
        *cc.banner("//", "platform-map.json", "generate-platform-code.py", "zed"),
        "",
        "/// Look up the archive suffix for a given OS and architecture.",
        "pub fn platform_suffix_for(os: &str, arch: &str) -> Option<&'static str> {",
        "    match (os, arch) {",
    ]
    for os_name, arches in platform_map.items():
        for arch, suffix in arches.items():
            lines.append(f'        ("{os_name}", "{arch}") => Some("{suffix}"),')
    lines.append("        _ => None,")
    lines.append("    }")
    lines.append("}")
    lines.append("")

    return "\n".join(lines)


OUTPUTS = {
    "vscode": (ROOT_DIR / "vscode" / "src" / "generated" / "platform.ts", generate_vscode),
    "zed": (ROOT_DIR / "zed" / "src" / "generated" / "platform.rs", generate_zed),
}

GENERATED_OUTPUTS = [path for path, _ in OUTPUTS.values()]


def main() -> None:
    cc.run_generator(
        "Generate platform code from platform-map.json",
        PLATFORM_MAP_PATH,
        OUTPUTS,
        "Platform code generation complete.",
    )


if __name__ == "__main__":
    main()
