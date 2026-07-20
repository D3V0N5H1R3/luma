// Binary names sourced from the generated config (single source of truth).
export { BINARY_NAMES } from "../generated/config";

// Configuration schema source of truth: extensions/vscode/package.json.
// Shared defaults: extensions/shared/defaults.json.
// Error severity contract: extensions/shared/error-handling.md.
// Zed (lib.rs) maintains its own defaults — see cross-references in that file.
export const CONFIG_SECTION = "luma" as const;

export const CONFIG_KEYS = {
    LSP_PATH: "lsp.path",
    LSP_AUTO_UPDATE: "lsp.autoUpdate",
    DAP_PATH: "dap.path",
    INTERPRETER_PATH: "path",
} as const;

export const GLOBAL_FOLDER_KEY = "__global__" as const;

export const COMMANDS = {
    restartServer: "luma.restartServer",
    showOutputChannel: "luma.showOutputChannel",
    updateServer: "luma.updateServer",
    showReferences: "luma.showReferences",
    openPlayground: "luma.openPlayground",
    runFile: "luma.runFile",
    runTests: "luma.runTests",
    visualizeVariable: "luma.visualizeVariable",
} as const;

/**
 * Stable identifiers registered with VS Code (test controller and language
 * client ids). Centralised alongside COMMANDS so the raw strings live in one
 * place rather than inline at each registration site.
 */
export const IDS = {
    testController: "lumaTests",
    lspClient: "luma-lsp",
} as const;

/** Builds the per-folder language client id (`luma-lsp-<folder>`). */
export function folderLspClientId(folder_name: string): string {
    return `${IDS.lspClient}-${folder_name}`;
}

// Built-in type set sourced from the generated builtin-types module
// (single source of truth: extensions/shared/builtin-types.json).
export { LUMA_BUILTIN_TYPE_SET } from "../generated/builtin-types";

export const LUMA_TYPE_PATTERN = /^(\s*)(\w+)\b/;
