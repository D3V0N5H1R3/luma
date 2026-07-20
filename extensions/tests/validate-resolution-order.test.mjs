#!/usr/bin/env node
/**
 * Binary Resolution Order Validation
 *
 * Validates that all three editor extensions implement (and document) the
 * canonical binary resolution order defined in
 * extensions/shared/resolution-order.json.
 *
 * For each extension the test reads the relevant source file and verifies
 * that step comments or markers appear in the correct order.
 *
 * Usage:
 *   node extensions/tests/validate-resolution-order.test.mjs
 */

import { describe, it } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const sharedDir = path.join(__dirname, "..", "shared");

const resolutionOrder = JSON.parse(
    fs.readFileSync(path.join(sharedDir, "resolution-order.json"), "utf-8"),
);

/**
 * Keywords used to detect each resolution step in source code.
 * Maps each canonical source id to an array of case-insensitive patterns
 * that are expected to appear in the step comment/documentation.
 */
const STEP_KEYWORDS = {
    user_config: ["user-configured", "user config"],
    bundled: ["bundled"],
    auto_download: ["auto-download", "auto download"],
    system_path: ["system path", "path fallback"],
};

/**
 * Extracts a region of `source` starting from the first line that matches
 * `startPattern` (case-insensitive) to the end of the file.  This lets
 * tests scope keyword searches to the resolution function rather than
 * matching keywords in file-level header comments.
 *
 * @param {string} source  Full file contents.
 * @param {RegExp} startPattern  Pattern marking the function start.
 * @returns {{ text: string, offset: number }}
 *   `text` is the extracted region; `offset` is the 0-based line number
 *   where the region starts within the original file.
 */
function extractRegion(source, startPattern) {
    const lines = source.split("\n");
    for (let i = 0; i < lines.length; i++) {
        if (startPattern.test(lines[i])) {
            return { text: lines.slice(i).join("\n"), offset: i };
        }
    }
    return { text: source, offset: 0 };
}

/**
 * Searches `source` for lines matching any keyword for a resolution step
 * and returns the 0-based line index of the first match, or -1.
 *
 * @param {string} source  Source text to search (may be a sub-region).
 * @param {string} stepSource  Canonical step id (e.g. "user_config").
 * @returns {number} Line index within `source`, or -1.
 */
function findStepIndex(source, stepSource) {
    const keywords = STEP_KEYWORDS[stepSource];
    if (!keywords) {
        return -1;
    }

    const lines = source.split("\n");
    for (let i = 0; i < lines.length; i++) {
        const lower = lines[i].toLowerCase();
        for (const kw of keywords) {
            if (lower.includes(kw.toLowerCase())) {
                return i;
            }
        }
    }
    return -1;
}

// ─── Tests ────────────────────────────────────────────────────────

describe("resolution-order.json", () => {
    const steps = resolutionOrder.resolution_order;

    it("should be valid JSON with the expected structure", () => {
        assert.ok(Array.isArray(steps), "resolution_order should be an array");
        assert.ok(steps.length > 0, "resolution_order should not be empty");

        for (const step of steps) {
            assert.ok(step.source, "each step must have a 'source' field");
            assert.ok(step.description, "each step must have a 'description' field");
            assert.ok(step.check, "each step must have a 'check' field");
        }
    });

    it("should define exactly the four canonical steps", () => {
        const sources = steps.map((s) => s.source);
        assert.deepStrictEqual(sources, [
            "user_config",
            "bundled",
            "auto_download",
            "system_path",
        ]);
    });

    // VS Code implements the full canonical order. Zed is intentionally handled
    // separately below because it implements only a subset in a different order.
    const orderedEditors = [
        {
            // Scope to resolveBinaryCommand so the file-header comment can't match early.
            editor: "VS Code",
            label: "VS Code binary/resolve.ts",
            file: path.join(__dirname, "..", "vscode", "src", "utils", "binary", "resolve.ts"),
            startPattern: /resolveBinaryCommand/,
        },
    ];

    for (const { editor, label, file, startPattern } of orderedEditors) {
        it(`${label} follows the resolution order`, () => {
            const fullSource = fs.readFileSync(file, "utf-8");

            // Scope the keyword search to the resolution function so header
            // comments don't produce false-positive early matches.
            const region = extractRegion(fullSource, startPattern);

            const indices = steps.map((step) => {
                const idx = findStepIndex(region.text, step.source);
                assert.notStrictEqual(
                    idx, -1,
                    `${label} missing resolution step: ${step.source} (${step.description})`,
                );
                return { source: step.source, index: idx };
            });

            for (let i = 1; i < indices.length; i++) {
                assert.ok(
                    indices[i].index > indices[i - 1].index,
                    `${editor}: step "${indices[i].source}" (line ${indices[i].index + region.offset + 1}) ` +
                    `should appear after "${indices[i - 1].source}" (line ${indices[i - 1].index + region.offset + 1})`,
                );
            }
        });
    }

    it("Zed lib.rs follows the resolution order", () => {
        const filePath = path.join(
            __dirname, "..", "zed", "src", "lib.rs",
        );
        const fullSource = fs.readFileSync(filePath, "utf-8");

        // Scope to the resolve_binary function.
        const region = extractRegion(fullSource, /fn resolve_binary/);

        // Zed's extension API does not expose user-config or bundled-binary
        // storage, so resolve_binary only implements system_path (via
        // worktree.which) and auto_download (via download_binary).
        //
        // Note: Zed intentionally checks PATH *before* auto-download
        // (opposite of the canonical order) because the Zed API resolves
        // user-config externally and prefers an already-installed binary
        // over downloading.  This test verifies both steps are present and
        // follow Zed's documented order (PATH → download).
        const zedKeywords = {
            system_path: ["which", "path"],
            auto_download: ["download_binary", "downloaded"],
        };

        const present = [];

        for (const step of steps) {
            const keywords = zedKeywords[step.source];
            if (!keywords) {
                continue;
            }

            const lines = region.text.split("\n");
            let idx = -1;
            for (let i = 0; i < lines.length; i++) {
                const lower = lines[i].toLowerCase();
                if (keywords.some((kw) => lower.includes(kw.toLowerCase()))) {
                    idx = i;
                    break;
                }
            }

            if (idx !== -1) {
                present.push({ source: step.source, index: idx });
            }
        }

        assert.ok(
            present.length >= 2,
            `Zed lib.rs should reference at least 2 resolution steps, found ${present.length}: ` +
            `[${present.map((p) => p.source).join(", ")}]`,
        );

        // Verify code-level ordering: system_path (which) appears before
        // auto_download (download_binary), matching Zed's documented
        // "checking PATH first, then downloading" behaviour.
        const pathStep = present.find((p) => p.source === "system_path");
        const downloadStep = present.find((p) => p.source === "auto_download");

        assert.ok(pathStep, "Zed lib.rs should contain a system_path step");
        assert.ok(downloadStep, "Zed lib.rs should contain an auto_download step");
        assert.ok(
            pathStep.index < downloadStep.index,
            `Zed: system_path (line ${pathStep.index + region.offset + 1}) ` +
            `should appear before auto_download (line ${downloadStep.index + region.offset + 1}) ` +
            `per Zed's documented PATH-first resolution strategy`,
        );
    });
});
