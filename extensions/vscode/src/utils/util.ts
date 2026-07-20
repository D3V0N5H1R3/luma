import * as fs from "node:fs";

import * as vscode from "vscode";

import { BINARY_NAMES } from "./constants";
import { luma_config } from "./config";
import { reportWarning } from "./report";

/** Extracts a human-readable message from an unknown thrown value. */
export function extractErrorMessage(err: unknown): string {
    return err instanceof Error ? err.message : String(err);
}

/** Escapes special characters in a string for use in a regular expression. */
export function escapeRegex(s: string): string {
    return s.replaceAll(/[.*+?^${}()|[\]\\]/g, String.raw`\$&`);
}

const WORKSPACE_FOLDER_PATTERN = /\$\{workspaceFolder(?::([^}]+))?\}/g;

/**
 * Expand `${workspaceFolder}` and `${workspaceFolder:name}` variables
 * in a path string.
 */
export function resolvePath(raw: string): string {
    return raw.replaceAll(WORKSPACE_FOLDER_PATTERN, (_match, name?: string) => {
        const folders = vscode.workspace.workspaceFolders;
        if (!folders || folders.length === 0) {
            throw new Error("Path uses ${workspaceFolder}, but no workspace folder is open.");
        }
        if (name) {
            const folder = folders.find((f) => f.name === name);
            if (!folder) {
                throw new Error(`Workspace folder '${name}' was not found.`);
            }
            return folder.uri.fsPath;
        }
        return folders[0].uri.fsPath;
    });
}

/**
 * Resolve the Luma interpreter binary path from the `luma.path` setting,
 * expanding workspace variables. Falls back to `"luma"` (PATH lookup).
 */
export function resolveInterpreterPath(): string {
    const configured = luma_config.interpreter_path;
    if (configured) {
        try {
            let resolved = resolvePath(configured);
            if (!fs.existsSync(resolved)) {
                void reportWarning(
                    undefined,
                    `Configured Luma path does not exist: ${resolved}.`,
                    `Configured Luma path does not exist: ${resolved}. Falling back to PATH lookup.`,
                );
                resolved = "";
            }
            if (resolved) {
                return resolved;
            }
        } catch (err: unknown) {
            const message = extractErrorMessage(err);
            void reportWarning(
                undefined,
                `Invalid luma.path: ${message}`,
                `Invalid luma.path: ${message}. Falling back to PATH lookup.`,
            );
        }
    }
    return BINARY_NAMES.INTERPRETER;
}
