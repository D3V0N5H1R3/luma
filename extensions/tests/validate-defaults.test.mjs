#!/usr/bin/env node
/**
 * Extension Defaults Validation
 *
 * Validates that editor extension configuration defaults match the
 * canonical source of truth in extensions/shared/defaults.json.
 *
 * Covers VS Code (package.json) and Zed (config_defaults.rs).
 *
 * Usage:
 *   node extensions/tests/validate-defaults.test.mjs
 */

import { describe, it } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(__dirname, "..");

const defaults = JSON.parse(
    fs.readFileSync(path.join(root, "shared", "defaults.json"), "utf-8"),
);

/**
 * Maps canonical setting keys from defaults.json to VS Code property names.
 *
 * Most keys map directly as `luma.<key>`, but some have different names
 * in the VS Code extension (e.g. `interpreter.path` → `luma.path`).
 */
const CANONICAL_TO_VSCODE = {
    "interpreter.path": "luma.path",
};

function canonicalToVscodeKey(key) {
    return CANONICAL_TO_VSCODE[key] || `luma.${key}`;
}

/**
 * Returns the setting keys that are included in generated code.
 *
 * Settings with `"generated": false` are schema-only entries that document
 * the full configuration space but are not yet emitted by the code generators.
 * They are excluded from cross-editor validation.
 *
 * @returns {string[]}
 */
function generatedSettingKeys() {
    return Object.keys(defaults.settings).filter((key) => {
        const spec = defaults.settings[key];
        return spec.generated !== false;
    });
}

/**
 * Converts a canonical setting key to the Rust constant name pattern
 * used in config_defaults.rs.
 *
 * Examples:
 *   "lsp.path"           → "DEFAULT_LSP_PATH"
 *   "inlayHints.enabled" → "DEFAULT_INLAY_HINTS_ENABLED"
 *   "codeLens.enabled"   → "DEFAULT_CODE_LENS_ENABLED"
 */
function settingKeyToRustConst(key) {
    return "DEFAULT_" + key
        .replace(/\./g, "_")
        .replace(/([a-z])([A-Z])/g, "$1_$2")
        .toUpperCase();
}

describe("Extension defaults consistency", () => {
    const keys = generatedSettingKeys();

    it("VS Code package.json matches canonical defaults and types", () => {
        const pkg = JSON.parse(
            fs.readFileSync(path.join(root, "vscode", "package.json"), "utf-8"),
        );
        const vscodeConfig = pkg.contributes?.configuration?.properties || {};

        for (const key of keys) {
            const spec = defaults.settings[key];
            const vscodeKey = canonicalToVscodeKey(key);
            const property = vscodeConfig[vscodeKey];

            assert.ok(property, `MISSING in VS Code: ${vscodeKey}`);
            assert.deepEqual(
                property.default,
                spec.default,
                `DEFAULT MISMATCH for ${vscodeKey}: ` +
                `VS Code has ${JSON.stringify(property.default)}, ` +
                `canonical is ${JSON.stringify(spec.default)}`,
            );
            assert.equal(
                property.type,
                spec.type,
                `TYPE MISMATCH for ${vscodeKey}: ` +
                `VS Code has "${property.type}", canonical is "${spec.type}"`,
            );
        }
    });

    // Zed's generated config uses Rust constant names (e.g. "lsp.path" →
    // DEFAULT_LSP_PATH), so setting keys are converted before the lookup.
    it("Zed config_defaults.rs declares every generated setting key", () => {
        const configDefaultsRs = fs.readFileSync(
            path.join(root, "zed", "src", "generated", "config_defaults.rs"),
            "utf-8",
        );

        for (const key of keys) {
            const rustConst = settingKeyToRustConst(key);
            assert.ok(
                configDefaultsRs.includes(rustConst),
                `MISSING in Zed config_defaults.rs: ${rustConst}`,
            );
        }
    });
});
