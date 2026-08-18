// AUTO-GENERATED from extensions/shared/defaults.json
// Do not edit manually. Run: python generate-config-code.py --vscode

import * as vscode from "vscode";

import { CONFIG_SECTION } from "../utils/constants";
import { CONFIG_DEFAULTS } from "./config";

/**
 * Strongly-typed accessor for Luma extension configuration.
 *
 * Reads values from the `luma` configuration section. Each getter
 * corresponds to a setting declared in package.json (generated from
 * defaults.json) and returns the workspace-effective value
 * (user, then workspace, then the default).
 */
export class LumaConfig {
    private get config(): vscode.WorkspaceConfiguration {
        return vscode.workspace.getConfiguration(CONFIG_SECTION);
    }

    /** Absolute path to the luma_lsp binary. If empty, the extension downloads it automatically or searches PATH. */
    get lsp_path(): string {
        return this.config.get<string>("lsp.path", CONFIG_DEFAULTS["lsp.path"]);
    }

    /** Automatically check for language server updates on extension activation. */
    get lsp_auto_update(): boolean {
        return this.config.get<boolean>("lsp.autoUpdate", CONFIG_DEFAULTS["lsp.autoUpdate"]);
    }

    /** Absolute path to the luma interpreter binary. If empty, the extension searches PATH. */
    get interpreter_path(): string {
        return this.config.get<string>("path", CONFIG_DEFAULTS["path"]);
    }

    /** Absolute path to the luma_dap debug adapter binary. If empty, the extension searches PATH. */
    get dap_path(): string {
        return this.config.get<string>("dap.path", CONFIG_DEFAULTS["dap.path"]);
    }

    /** Only report linter warnings on save (not while typing). Syntax and type errors are always reported immediately. */
    get diagnostics_on_save(): boolean {
        return this.config.get<boolean>("diagnostics.onSave", CONFIG_DEFAULTS["diagnostics.onSave"]);
    }

    /** Show inferred type annotations and parameter names as inlay hints. Disabled by default to keep the source uncluttered. */
    get inlay_hints_enabled(): boolean {
        return this.config.get<boolean>("inlayHints.enabled", CONFIG_DEFAULTS["inlayHints.enabled"]);
    }

    /** Show reference counts above functions and types as code lenses. */
    get code_lens_enabled(): boolean {
        return this.config.get<boolean>("codeLens.enabled", CONFIG_DEFAULTS["codeLens.enabled"]);
    }

    /** Enable the Luma playground for interactive code execution. */
    get playground_enabled(): boolean {
        return this.config.get<boolean>("playground.enabled", CONFIG_DEFAULTS["playground.enabled"]);
    }

    /** Maximum execution time in milliseconds for playground snippets. */
    get playground_timeout(): number {
        return this.config.get<number>("playground.timeout", CONFIG_DEFAULTS["playground.timeout"]);
    }

    /** Maximum output buffer size in bytes for playground snippets. */
    get playground_max_output_size(): number {
        return this.config.get<number>("playground.maxOutputSize", CONFIG_DEFAULTS["playground.maxOutputSize"]);
    }
}

/** Singleton instance of the Luma configuration accessor. */
export const luma_config = new LumaConfig();
