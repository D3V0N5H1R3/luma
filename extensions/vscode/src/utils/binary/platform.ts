// Platform asset naming and bundled-binary path resolution.
// Part of the binary-download module (see ../binary-download.ts).

import * as path from "node:path";
import * as fs from "node:fs";

import * as vscode from "vscode";

import { getPlatformSuffix } from "../../generated/platform";
import type { BinaryConfig } from "./types";

// Asset naming convention — see extensions/BINARY_ASSETS.md for the canonical table.
// Canonical platform→suffix mapping: extensions/shared/platform-map.json
// Each editor extension maps its native platform identifiers to the canonical
// OS/arch keys used in the shared JSON.  See also:
//   Zed:    extensions/zed/src/lib.rs               (platform_suffix())

export function getPlatformAssetName(binary_prefix: string): string {
    const suffix = getPlatformSuffix();
    if (!suffix) {
        throw new Error(`Unsupported platform: ${process.platform}/${process.arch}`);
    }
    return `${binary_prefix}-${suffix}`;
}

/** Whether the extension is running on Windows. */
export function isWindows(): boolean {
    return process.platform === "win32";
}

export function getBinaryFilename(name: string): string {
    return isWindows() ? `${name}.exe` : name;
}

/** Returns the per-extension directory where managed binaries are installed. */
export function getBinDir(context: vscode.ExtensionContext): string {
    return path.join(context.globalStorageUri.fsPath, "bin");
}

/**
 * Returns the path to a bundled binary managed by this extension,
 * or undefined if not yet downloaded.
 */
export function getBundledBinaryPath(
    config: BinaryConfig,
    context: vscode.ExtensionContext,
): string | undefined {
    const bin_path = path.join(getBinDir(context), getBinaryFilename(config.name));
    if (fs.existsSync(bin_path)) {
        return bin_path;
    }
    return undefined;
}
