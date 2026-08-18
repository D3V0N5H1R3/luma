// AUTO-GENERATED from extensions/shared/defaults.json
// Do not edit manually. Run: python generate-config.py --vscode

export const GITHUB_REPO = "d3v0n5h1r3/luma";

export const BINARY_NAMES = {
    LSP: "luma_lsp",
    DAP: "luma_dap",
    INTERPRETER: "luma",
} as const;

/** Default values for VS Code settings, keyed by their `luma.`-relative name. */
export const CONFIG_DEFAULTS = {
    "lsp.path": "",
    "lsp.autoUpdate": true,
    "path": "",
    "dap.path": "",
    "diagnostics.onSave": false,
    "inlayHints.enabled": false,
    "codeLens.enabled": false,
    "playground.enabled": true,
    "playground.timeout": 10000,
    "playground.maxOutputSize": 1048576,
} as const;
