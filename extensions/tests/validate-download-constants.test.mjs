#!/usr/bin/env node
/**
 * Download Constants Consistency Tests
 *
 * Validates that the per-editor generated download constants match the shared
 * source of truth in extensions/shared/download-constants.json. The checksum
 * manifest filename in particular was previously hardcoded independently in
 * each editor; these tests guard against drift.
 *
 * Usage:
 *   node extensions/tests/validate-download-constants.test.mjs
 */

import { describe, it } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(__dirname, "..");

const constants = JSON.parse(
    fs.readFileSync(path.join(root, "shared", "download-constants.json"), "utf-8"),
);

const checksumsFilename = constants.checksums.filename;

const generatedFiles = {
    "VS Code": path.join(root, "vscode", "src", "generated", "download-constants.ts"),
    Zed: path.join(root, "zed", "src", "generated", "download_constants.rs"),
};

describe("Download constants consistency", () => {
    it("download-constants.json declares a non-empty checksums filename", () => {
        assert.ok(checksumsFilename, "checksums.filename must be set");
        assert.equal(typeof checksumsFilename, "string");
    });

    for (const [editor, file] of Object.entries(generatedFiles)) {
        it(`${editor} generated constants embed the checksums filename`, () => {
            const source = fs.readFileSync(file, "utf-8");
            assert.ok(
                source.includes(`"${checksumsFilename}"`),
                `${editor} (${path.basename(file)}) should contain "${checksumsFilename}"`,
            );
            assert.ok(
                source.includes("CHECKSUMS_FILENAME"),
                `${editor} (${path.basename(file)}) should export CHECKSUMS_FILENAME`,
            );
        });
    }
});
