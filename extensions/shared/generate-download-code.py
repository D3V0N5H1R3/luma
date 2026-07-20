#!/usr/bin/env python3
"""Generate download-protocol constants from download-constants.json.

The download protocol literals (currently the SHA256SUMS manifest filename)
were previously hardcoded independently in each editor: VS Code's
``CHECKSUMS_FILENAME`` and Zed's ``SHA256SUMS_ASSET_NAME``. This generator emits
them from the single source of truth so the implementations can never drift.

Outputs:
    --vscode → extensions/vscode/src/generated/download-constants.ts
    --zed    → extensions/zed/src/generated/download_constants.rs
    --all    → all of the above

Usage:
    python generate-download-code.py --all
"""

from __future__ import annotations

import codegen_common as cc

ROOT_DIR = cc.resolve_root()
DOWNLOAD_CONSTANTS_PATH = cc.SCRIPT_DIR / "download-constants.json"


def _checksums_filename(constants: dict) -> str:
    """Return the checksums manifest filename from the shared constants."""
    return constants["checksums"]["filename"]


def generate_vscode(constants: dict) -> str:
    """Generate TypeScript download constants for the VS Code extension."""
    filename = _checksums_filename(constants)
    lines = [
        *cc.banner("//", "download-constants.json", "generate-download-code.py", "vscode"),
        "",
        "/** Filename of the SHA-256 checksum manifest published with each release. */",
        f"export const CHECKSUMS_FILENAME = {cc.ts_literal(filename)};",
        "",
    ]
    return "\n".join(lines)


def generate_zed(constants: dict) -> str:
    """Generate Rust download constants for the Zed extension."""
    filename = _checksums_filename(constants)
    lines = [
        *cc.banner("//", "download-constants.json", "generate-download-code.py", "zed"),
        "",
        "#![allow(dead_code)]",
        "",
        "/// Filename of the SHA-256 checksum manifest published with each release.",
        f"pub const CHECKSUMS_FILENAME: &str = {cc.rust_literal(filename, 'string')};",
        "",
    ]
    return "\n".join(lines)


OUTPUTS = {
    "vscode": (
        ROOT_DIR / "vscode" / "src" / "generated" / "download-constants.ts",
        generate_vscode,
    ),
    "zed": (ROOT_DIR / "zed" / "src" / "generated" / "download_constants.rs", generate_zed),
}

GENERATED_OUTPUTS = [path for path, _ in OUTPUTS.values()]


def main() -> None:
    cc.run_generator(
        "Generate download constants from download-constants.json",
        DOWNLOAD_CONSTANTS_PATH,
        OUTPUTS,
        "Download constant generation complete.",
    )


if __name__ == "__main__":
    main()
