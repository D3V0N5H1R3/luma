// ─── Checksum / integrity verification ────────────────────────────

import * as fs from "node:fs";
import * as crypto from "node:crypto";

import * as vscode from "vscode";

import { extractErrorMessage } from "./util";
import { fetchText } from "./http";
import { CHECKSUMS_FILENAME } from "../generated/download-constants";
import type { GithubRelease } from "./binary/types";

// Re-exported so call sites and tests can import the filename from this module
// while the value itself stays generated from download-constants.json.
export { CHECKSUMS_FILENAME };

/** Computes the SHA-256 hash of a file and returns the hex digest. */
export async function computeSha256(file_path: string): Promise<string> {
    return new Promise((resolve, reject) => {
        const hash = crypto.createHash("sha256");
        const stream = fs.createReadStream(file_path);
        stream.on("data", (data) => hash.update(data));
        stream.on("end", () => resolve(hash.digest("hex")));
        stream.on("error", reject);
    });
}

/** Parses a SHA-256 checksums manifest into a filename-to-hash map. */
export function parseChecksums(text: string): Map<string, string> {
    const result = new Map<string, string>();
    for (const line of text.split(/\r?\n/)) {
        const trimmed = line.trim();
        if (!trimmed) {
            continue;
        }
        // SHA256SUMS format: "<64-hex-hash> <separator> <filename>", where the
        // separator is one or more spaces and the filename may carry a leading
        // "*" binary-mode marker. The hash is the first whitespace-delimited token.
        const match = /^([0-9a-fA-F]{64})\s+\*?(.+)$/.exec(trimmed);
        if (match) {
            result.set(match[2].trim(), match[1].toLowerCase());
        } else {
            console.warn(`[luma] Ignoring malformed checksum line: ${trimmed}`);
        }
    }
    return result;
}

export async function verifyChecksum(
    archive_path: string,
    asset_name: string,
    release: GithubRelease,
    output: vscode.OutputChannel,
): Promise<boolean> {
    const checksums_asset = release.assets.find((a) => a.name === CHECKSUMS_FILENAME);
    if (!checksums_asset) {
        output.appendLine("Release does not include SHA256SUMS — refusing download.");
        return false;
    }

    try {
        const checksums_text = await fetchText(checksums_asset.browser_download_url);
        const checksums = parseChecksums(checksums_text);
        return validateChecksum(archive_path, asset_name, checksums, output);
    } catch (err: unknown) {
        const msg = extractErrorMessage(err);
        output.appendLine(`Checksum verification failed: ${msg}`);
        return false;
    }
}

/**
 * Validates the SHA-256 checksum of a file against a pre-parsed checksums map.
 * Use this when the checksums have already been fetched and parsed, to avoid
 * redundant network requests when verifying multiple assets from the same release.
 */
export async function validateChecksum(
    archive_path: string,
    asset_name: string,
    checksums: Map<string, string>,
    output: vscode.OutputChannel,
): Promise<boolean> {
    const expected = checksums.get(asset_name);
    if (!expected) {
        output.appendLine(`SHA256SUMS does not contain an entry for ${asset_name}.`);
        return false;
    }

    const actual = await computeSha256(archive_path);
    if (actual !== expected) {
        output.appendLine(
            `Checksum mismatch for ${asset_name}!\n` +
                `  Expected: ${expected}\n` +
                `  Actual:   ${actual}`,
        );
        return false;
    }

    output.appendLine(`Checksum verified for ${asset_name}.`);
    return true;
}
