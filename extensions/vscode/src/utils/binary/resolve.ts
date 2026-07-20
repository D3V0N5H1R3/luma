// Binary download and resolution-order logic.
// Part of the binary-download module (see ../binary-download.ts).

import * as path from "node:path";
import * as fs from "node:fs";

import * as vscode from "vscode";

import { resolvePath, extractErrorMessage } from "../util";
import { reportError, reportWarning } from "../report";
import { fetchJson, downloadFile } from "../http";
import { verifyChecksum } from "../checksum";
import { CONFIG_SECTION } from "../constants";
import { GITHUB_REPO } from "../../generated/config";

import type { BinaryConfig, GithubAsset, GithubRelease } from "./types";
import { parseGithubRelease } from "./types";
import {
    getPlatformAssetName,
    getBinaryFilename,
    getBundledBinaryPath,
    getBinDir,
    isWindows,
} from "./platform";
import { extractArchive } from "./archive";

function capitalize(text: string): string {
    return text.charAt(0).toUpperCase() + text.slice(1);
}

/** Fetches and parses the latest GitHub release for the Luma repository. */
export async function fetchLatestRelease(): Promise<GithubRelease> {
    return parseGithubRelease(
        await fetchJson(`https://api.github.com/repos/${GITHUB_REPO}/releases/latest`),
    );
}

/** Finds the matching platform asset in a GitHub release. */
function resolveReleaseAsset(
    release: GithubRelease,
    asset_name: string,
    output: vscode.OutputChannel,
): GithubAsset | undefined {
    const asset = release.assets.find((a) => a.name === asset_name);
    if (!asset) {
        output.appendLine(
            `Release ${release.tag_name} does not contain ${asset_name}. ` +
                `Available: ${release.assets.map((a) => a.name).join(", ")}`,
        );
    }
    return asset;
}

/** Downloads an archive and verifies its checksum. Returns false on failure. */
async function downloadAndVerify(
    url: string,
    archive_path: string,
    asset_name: string,
    release: GithubRelease,
    config: BinaryConfig,
    output: vscode.OutputChannel,
): Promise<boolean> {
    await downloadFile(url, archive_path);

    const is_valid = await verifyChecksum(archive_path, asset_name, release, output);
    if (!is_valid) {
        fs.unlinkSync(archive_path);
        void reportError(
            output,
            "Download integrity check failed — aborting.",
            `${config.name} download failed integrity check. ` +
                "The file may have been tampered with. See output for details.",
        );
    }
    return is_valid;
}

/** Extracts an archive, cleans it up, and returns the binary path if found. */
async function extractAndInstall(
    archive_path: string,
    bin_dir: string,
    binary_filename: string,
    output: vscode.OutputChannel,
): Promise<string | undefined> {
    try {
        await extractArchive(archive_path, bin_dir);
    } finally {
        // Always remove the downloaded archive, even when extraction fails, so
        // partial downloads don't accumulate in global storage.
        fs.rmSync(archive_path, { force: true });
    }

    const bin_path = path.join(bin_dir, binary_filename);
    if (!fs.existsSync(bin_path)) {
        output.appendLine(`Extraction succeeded but ${binary_filename} not found in ${bin_dir}`);
        return undefined;
    }

    if (!isWindows()) {
        fs.chmodSync(bin_path, 0o755);
    }

    return bin_path;
}

/**
 * Downloads and installs a binary from a specific GitHub release.
 * Returns the path to the binary on success, or undefined on failure.
 */
export async function downloadBinary(
    config: BinaryConfig,
    context: vscode.ExtensionContext,
    output: vscode.OutputChannel,
    release: GithubRelease,
): Promise<string | undefined> {
    const binary_filename = getBinaryFilename(config.name);

    return vscode.window.withProgress(
        {
            location: vscode.ProgressLocation.Notification,
            title: "Luma",
            cancellable: false,
        },
        async (progress) => {
            progress.report({ message: `Downloading ${config.display_name}…` });
            output.appendLine(`Downloading ${config.name} binary…`);

            try {
                const asset_name = getPlatformAssetName(config.name);
                const asset = resolveReleaseAsset(release, asset_name, output);
                if (!asset) {
                    return undefined;
                }

                const bin_dir = getBinDir(context);
                fs.mkdirSync(bin_dir, { recursive: true });

                const archive_path = path.join(bin_dir, asset_name);
                const verified = await downloadAndVerify(
                    asset.browser_download_url,
                    archive_path,
                    asset_name,
                    release,
                    config,
                    output,
                );
                if (!verified) {
                    return undefined;
                }

                progress.report({ message: `Extracting ${config.display_name}…` });
                const bin_path = await extractAndInstall(
                    archive_path,
                    bin_dir,
                    binary_filename,
                    output,
                );
                if (!bin_path) {
                    return undefined;
                }

                await context.globalState.update(config.installed_tag_key, release.tag_name);

                progress.report({ message: `${capitalize(config.display_name)} ready.` });
                output.appendLine(`Downloaded ${config.name} to ${bin_path}`);
                return bin_path;
            } catch (err: unknown) {
                const msg = extractErrorMessage(err);
                void reportWarning(
                    output,
                    `Failed to download ${config.name}: ${msg}`,
                    `Failed to download ${config.display_name}: ${msg}. ` +
                        `Install it manually or set "${config.settings_key}" in settings.`,
                );
                return undefined;
            }
        },
    );
}

/**
 * Resolves a binary path using the standard resolution order:
 *   1. User-configured path (from settings)
 *   2. Bundled binary (extension storage)
 *   3. Auto-download from GitHub releases
 *   4. System PATH fallback
 *
 * @param config - Binary configuration (LSP_CONFIG or DAP_CONFIG)
 * @param config_key - The settings key to read (e.g. CONFIG_KEYS.LSP_PATH)
 * @param context - Extension context for storage paths
 * @param output - Output channel for logging (if undefined, skips download step)
 * @returns The resolved binary path
 */
export async function resolveBinaryCommand(
    config: BinaryConfig,
    config_key: string,
    context: vscode.ExtensionContext,
    output?: vscode.OutputChannel,
): Promise<string> {
    // Step 1: User-configured path.
    const vs_config = vscode.workspace.getConfiguration(CONFIG_SECTION);
    const configured = vs_config.get<string>(config_key, "");

    if (configured) {
        try {
            return resolvePath(configured);
        } catch (err: unknown) {
            const message = extractErrorMessage(err);
            void reportWarning(
                output,
                `Invalid ${CONFIG_SECTION}.${config_key}: ${message}`,
                `Invalid ${CONFIG_SECTION}.${config_key}: ${message}. Falling back to bundled binary or PATH lookup.`,
            );
        }
    }

    // Step 2: Bundled binary.
    const bundled = getBundledBinaryPath(config, context);
    if (bundled) {
        return bundled;
    }

    // Step 3: Auto-download.
    if (output) {
        const downloaded = await downloadLatestBinary(config, context, output);
        if (downloaded) {
            return downloaded;
        }
    }

    // Step 4: System PATH fallback.
    return config.name;
}

/**
 * Resolution step 3: downloads the binary from the latest GitHub release.
 *
 * The caller (`resolveBinaryCommand`) owns the full resolution order and has
 * already checked the user-configured path (step 1) and the bundled binary
 * (step 2) before reaching here, so this function only performs the download.
 * Returns the path to the downloaded binary, or undefined on failure.
 */
async function downloadLatestBinary(
    config: BinaryConfig,
    context: vscode.ExtensionContext,
    output: vscode.OutputChannel,
): Promise<string | undefined> {
    // Validate platform support before hitting the network.
    try {
        getPlatformAssetName(config.name);
    } catch (err: unknown) {
        const msg = extractErrorMessage(err);
        output.appendLine(`${msg}. Please build from source or set "${config.settings_key}".`);
        return undefined;
    }

    try {
        const release = await fetchLatestRelease();
        return downloadBinary(config, context, output, release);
    } catch (err: unknown) {
        const msg = extractErrorMessage(err);
        output.appendLine(`Failed to fetch latest release: ${msg}`);
        return undefined;
    }
}
