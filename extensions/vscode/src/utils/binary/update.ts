// Binary update checking and atomic replacement.
// Part of the binary-download module (see ../binary-download.ts).

import * as path from "node:path";
import * as fs from "node:fs";

import * as vscode from "vscode";

import { extractErrorMessage } from "../util";
import { reportError, reportInfo } from "../report";

import type { BinaryConfig, GithubRelease } from "./types";
import {
    getPlatformAssetName,
    getBinaryFilename,
    getBundledBinaryPath,
    getBinDir,
} from "./platform";
import { downloadBinary, fetchLatestRelease } from "./resolve";

/**
 * How an update check was triggered. `"interactive"` checks are user-initiated
 * (e.g. the "Luma: Update Server" command) and surface every outcome, including
 * "up to date" and failure notifications; `"silent"` checks run automatically
 * (e.g. on activation) and keep those outcomes to the output log only.
 */
export type UpdateMode = "interactive" | "silent";

interface BinPaths {
    bin_dir: string;
    tmp_bin: string;
    old_bin: string;
}

function getBinPaths(config: BinaryConfig, context: vscode.ExtensionContext): BinPaths {
    const binary_name = getBinaryFilename(config.name);
    const bin_dir = getBinDir(context);
    const tmp_bin = path.join(bin_dir, binary_name + ".tmp");
    const old_bin = path.join(bin_dir, binary_name);
    return { bin_dir, tmp_bin, old_bin };
}

export function shouldUpdateBinary(
    config: BinaryConfig,
    context: vscode.ExtensionContext,
    release: GithubRelease,
    mode: UpdateMode,
): boolean {
    const installed_tag = context.globalState.get<string>(config.installed_tag_key);

    if (installed_tag && installed_tag === release.tag_name) {
        if (mode === "interactive") {
            void vscode.window.showInformationMessage(
                `${config.name} is up to date (${release.tag_name}).`,
            );
        }
        return false;
    }

    if (!installed_tag && !getBundledBinaryPath(config, context)) {
        return false;
    }

    return true;
}

function promptForBinaryUpdate(
    config: BinaryConfig,
    context: vscode.ExtensionContext,
    release: GithubRelease,
): Thenable<string | undefined> {
    const installed_tag = context.globalState.get<string>(config.installed_tag_key);
    return vscode.window.showInformationMessage(
        `A new ${config.name} release is available: ${release.tag_name}` +
            (installed_tag ? ` (current: ${installed_tag})` : ""),
        "Update",
        "Later",
    );
}

async function performBinaryUpdate(
    config: BinaryConfig,
    context: vscode.ExtensionContext,
    release: GithubRelease,
    output: vscode.OutputChannel,
    restart_command?: string,
): Promise<void> {
    const { tmp_bin, old_bin } = getBinPaths(config, context);

    if (fs.existsSync(tmp_bin)) {
        fs.unlinkSync(tmp_bin);
    }
    if (fs.existsSync(old_bin)) {
        fs.renameSync(old_bin, tmp_bin);
    }

    const downloaded = await downloadBinary(config, context, output, release);
    if (downloaded) {
        if (fs.existsSync(tmp_bin)) {
            fs.unlinkSync(tmp_bin);
        }
        // downloadBinary already recorded installed_tag_key on success.
        if (restart_command) {
            const restart = await vscode.window.showInformationMessage(
                `Updated ${config.name} to ${release.tag_name}. Restart?`,
                "Restart",
                "Later",
            );
            if (restart === "Restart") {
                await vscode.commands.executeCommand(restart_command);
            }
        } else {
            const done = `Updated ${config.name} to ${release.tag_name}.`;
            void reportInfo(output, done, done);
        }
    } else if (fs.existsSync(tmp_bin)) {
        fs.renameSync(tmp_bin, old_bin);
        output.appendLine("Update failed — restored previous binary.");
    }
}

function restoreBinaryOnError(config: BinaryConfig, context: vscode.ExtensionContext): void {
    const { tmp_bin, old_bin } = getBinPaths(config, context);
    if (fs.existsSync(tmp_bin) && !fs.existsSync(old_bin)) {
        fs.renameSync(tmp_bin, old_bin);
    }
}

/**
 * Checks GitHub for a newer binary release. In `"interactive"` mode every
 * outcome is surfaced (including "up to date"); in `"silent"` mode only an
 * available update is shown and other outcomes go to the output log.
 *
 * @param config - Binary configuration
 * @param context - Extension context
 * @param output - Output channel for logging
 * @param mode - Whether the check was user-initiated (`"interactive"`) or automatic (`"silent"`)
 * @param restart_command - Optional VS Code command to execute after update
 */
export async function checkForBinaryUpdate(
    config: BinaryConfig,
    context: vscode.ExtensionContext,
    output: vscode.OutputChannel,
    mode: UpdateMode,
    restart_command?: string,
): Promise<void> {
    try {
        getPlatformAssetName(config.name);
    } catch {
        if (mode === "interactive") {
            void vscode.window.showInformationMessage(
                `No pre-built ${config.name} binary available for this platform.`,
            );
        }
        return;
    }

    try {
        const release = await fetchLatestRelease();

        if (!shouldUpdateBinary(config, context, release, mode)) {
            return;
        }

        const action = await promptForBinaryUpdate(config, context, release);
        if (action !== "Update") {
            return;
        }

        await performBinaryUpdate(config, context, release, output, restart_command);
    } catch (err: unknown) {
        restoreBinaryOnError(config, context);
        const msg = extractErrorMessage(err);
        const channel_message = `Update check failed: ${msg}`;
        if (mode === "interactive") {
            void reportError(output, channel_message, `Failed to check for updates: ${msg}`);
        } else {
            output.appendLine(channel_message);
        }
    }
}
