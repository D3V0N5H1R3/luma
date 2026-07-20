// Release, asset, and binary-configuration types plus GitHub release parsing.
// Part of the binary-download module (see ../binary-download.ts).

import { BINARY_NAMES, CONFIG_SECTION, CONFIG_KEYS } from "../constants";

/** Represents a single asset in a GitHub release. */
export interface GithubAsset {
    name: string;
    browser_download_url: string;
}

/** Represents a GitHub release with its tag and assets. */
export interface GithubRelease {
    tag_name: string;
    assets: GithubAsset[];
}

/** Configuration for a downloadable binary. */
export interface BinaryConfig {
    /** Binary name without extension, e.g. "luma_lsp" or "luma_dap". */
    readonly name: string;
    /** Human-readable label for progress/error messages, e.g. "language server". */
    readonly display_name: string;
    /** Settings key to suggest in fallback messages, e.g. "luma.lsp.path". */
    readonly settings_key: string;
    /** Global state key for tracking the installed version tag. */
    readonly installed_tag_key: string;
}

/** Pre-built configuration for the Luma language server binary. */
export const LSP_CONFIG: BinaryConfig = {
    name: BINARY_NAMES.LSP,
    display_name: "language server",
    settings_key: `${CONFIG_SECTION}.${CONFIG_KEYS.LSP_PATH}`,
    installed_tag_key: "luma.lsp.installedTag",
};

/** Pre-built configuration for the Luma debug adapter binary. */
export const DAP_CONFIG: BinaryConfig = {
    name: BINARY_NAMES.DAP,
    display_name: "debug adapter",
    settings_key: `${CONFIG_SECTION}.${CONFIG_KEYS.DAP_PATH}`,
    installed_tag_key: "luma.dap.installedTag",
};

export function parseGithubRelease(data: unknown): GithubRelease {
    if (
        typeof data !== "object" ||
        data === null ||
        !("tag_name" in data) ||
        !("assets" in data) ||
        typeof (data as GithubRelease).tag_name !== "string" ||
        !Array.isArray((data as GithubRelease).assets)
    ) {
        throw new Error("Invalid GitHub release response");
    }
    return data as GithubRelease;
}
