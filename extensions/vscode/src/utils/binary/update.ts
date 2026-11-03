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
    binary_name: string;
    old_bin: string;
}

function getBinPaths(config: BinaryConfig, context: vscode.ExtensionContext): BinPaths {
    const binary_name = getBinaryFilename(config.name);
    const bin_dir = getBinDir(context);
    const old_bin = path.join(bin_dir, binary_name);
    return { bin_dir, binary_name, old_bin };
}

/** Infix of the backup a binary is moved to while its replacement downloads. */
const BACKUP_INFIX = ".old-";
/** Backup suffix used by earlier versions of the extension. */
const LEGACY_BACKUP_SUFFIX = ".tmp";

/**
 * Deletes a file, reporting rather than throwing on failure.
 *
 * On Windows a running executable can be renamed but not deleted, so the
 * backup of a binary that is still in use (the language server until it
 * restarts) cannot be removed yet. That must not turn a successful update into
 * a reported failure; the leftover is cleaned up by a later update.
 *
 * @returns Whether the file is gone.
 */
export function discardBackup(
    file_path: string,
    output: vscode.OutputChannel,
    remove: (file_path: string) => void = (p) => fs.rmSync(p, { force: true }),
): boolean {
    try {
        remove(file_path);
        return true;
    } catch (err: unknown) {
        output.appendLine(
            `Could not remove ${file_path} yet (${extractErrorMessage(err)}); ` +
                "it will be removed after the next update.",
        );
        return false;
    }
}

/** Removes backups left by earlier updates whose binary was still running. */
export function removeStaleBackups(
    bin_dir: string,
    binary_name: string,
    output: vscode.OutputChannel,
): void {
    let entries: string[];
    try {
        entries = fs.readdirSync(bin_dir);
    } catch {
        return;
    }
    for (const entry of entries) {
        if (
            entry.startsWith(binary_name + BACKUP_INFIX) ||
            entry === binary_name + LEGACY_BACKUP_SUFFIX
        ) {
            discardBackup(path.join(bin_dir, entry), output);
        }
    }
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
    const { bin_dir, binary_name, old_bin } = getBinPaths(config, context);

    removeStaleBackups(bin_dir, binary_name, output);

    // Move the current binary aside (a running executable can be renamed even
    // on Windows) so the download can take its place. The name is unique, so
    // a backup that is still locked from an earlier update never blocks this one.
    const backup = fs.existsSync(old_bin)
        ? path.join(bin_dir, `${binary_name}${BACKUP_INFIX}${Date.now()}`)
        : undefined;
    if (backup) {
        fs.renameSync(old_bin, backup);
    }

    let downloaded: string | undefined;
    try {
        downloaded = await downloadBinary(config, context, output, release);
    } finally {
        if (!downloaded && backup && !fs.existsSync(old_bin)) {
            fs.renameSync(backup, old_bin);
            output.appendLine("Update failed — restored previous binary.");
        }
    }

    if (!downloaded) {
        return;
    }

    if (backup) {
        discardBackup(backup, output);
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
        const msg = extractErrorMessage(err);
        const channel_message = `Update check failed: ${msg}`;
        if (mode === "interactive") {
            void reportError(output, channel_message, `Failed to check for updates: ${msg}`);
        } else {
            output.appendLine(channel_message);
        }
    }
}
