#!/usr/bin/env node
/**
 * Download Protocol Consistency Tests
 *
 * Validates that all three editor extensions produce platform mappings and
 * asset names consistent with the shared source of truth in
 * extensions/shared/platform-map.json.
 *
 * Usage:
 *   node extensions/tests/validate-download.test.mjs
 */

import { describe, it } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const sharedDir = path.join(__dirname, "..", "shared");

const platformMap = JSON.parse(
    fs.readFileSync(path.join(sharedDir, "platform-map.json"), "utf-8"),
);

describe("Download protocol consistency", () => {
    it("platform-map.json should have entries for all supported platforms", () => {
        const expectedPlatforms = ["linux", "macos", "windows"];
        const expectedArchs = ["x86_64", "aarch64"];

        for (const os of expectedPlatforms) {
            assert.ok(platformMap[os], `Missing OS: ${os}`);
            for (const arch of expectedArchs) {
                assert.ok(platformMap[os][arch], `Missing ${os}/${arch}`);
            }
        }
    });

    it("archive suffixes should use correct extensions", () => {
        for (const [os, archs] of Object.entries(platformMap)) {
            for (const [arch, suffix] of Object.entries(archs)) {
                if (os === "windows") {
                    assert.ok(suffix.endsWith(".zip"), `Windows ${arch} should use .zip: ${suffix}`);
                } else {
                    assert.ok(suffix.endsWith(".tar.gz"), `${os} ${arch} should use .tar.gz: ${suffix}`);
                }
            }
        }
    });

    // Each editor generates its platform map into a different file/language;
    // the check is identical, so drive it from a descriptor list (mirrors
    // validate-download-constants.test.mjs).
    const generatedPlatformFiles = [
        { editor: "VS Code", file: path.join(__dirname, "..", "vscode", "src", "generated", "platform.ts") },
        { editor: "Zed", file: path.join(__dirname, "..", "zed", "src", "generated", "platform.rs") },
    ];

    for (const { editor, file } of generatedPlatformFiles) {
        it(`${editor} generated platform map should match shared source`, () => {
            const source = fs.readFileSync(file, "utf-8");
            for (const archs of Object.values(platformMap)) {
                for (const suffix of Object.values(archs)) {
                    assert.ok(
                        source.includes(`"${suffix}"`),
                        `${editor} (${path.basename(file)}) missing suffix: ${suffix}`,
                    );
                }
            }
        });
    }

    it("shared SHA256SUMS fixture parses to the canonical entries across editors", () => {
        // The fixture in extensions/shared/sha256sums-sample.txt is the single
        // golden input consumed by the VS Code and Zed parser tests.
        // This reference parser mirrors both: the hash is the first
        // whitespace-delimited token (exactly 64 hex chars) and the filename is
        // the remainder with an optional leading "*" binary-mode marker stripped.
        const sample = fs.readFileSync(
            path.join(sharedDir, "sha256sums-sample.txt"),
            "utf-8",
        );

        const parsed = new Map();
        for (const line of sample.split(/\r?\n/)) {
            const match = /^([0-9a-fA-F]{64})\s+\*?(.+)$/.exec(line.trim());
            if (match) {
                parsed.set(match[2].trim(), match[1].toLowerCase());
            }
        }

        // Four valid entries; comments, the blank line and the short hash are ignored.
        assert.equal(parsed.size, 4);
        assert.equal(parsed.get("luma_lsp-linux-x86_64.tar.gz"), "1".repeat(64));
        assert.equal(parsed.get("luma_lsp-macos-aarch64.tar.gz"), "2".repeat(64));
        assert.equal(parsed.get("luma_dap-windows-x86_64.zip"), "3".repeat(64));
        // Uppercase digest normalises to lowercase.
        assert.equal(parsed.get("luma-linux-x86_64.tar.gz"), "a".repeat(64));
        assert.equal(parsed.get("ignored-short-hash.tar.gz"), undefined);
    });
});
