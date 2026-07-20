#!/usr/bin/env node
/**
 * Highlight Test Runner
 *
 * Parses all .luma fixture files using the tree-sitter grammar and
 * reports any parse errors. This ensures the grammar covers all
 * language constructs without producing ERROR nodes.
 *
 * Usage:
 *   cd extensions/zed/grammars/tree-sitter-luma
 *   npm install
 *   node ../../tests/parse_fixtures.js
 *
 * Requires: tree-sitter-cli installed in the grammar directory.
 */

const { execSync } = require("child_process");
const path = require("path");
const fs = require("fs");

const FIXTURES_DIR = path.resolve(__dirname, "fixtures");
const GRAMMAR_DIR = path.resolve(
    __dirname,
    "../zed/grammars/tree-sitter-luma",
);

function main() {
    if (!fs.existsSync(GRAMMAR_DIR)) {
        console.error(`Grammar directory not found: ${GRAMMAR_DIR}`);
        process.exit(1);
    }

    const fixtures = fs
        .readdirSync(FIXTURES_DIR)
        .filter((f) => f.endsWith(".luma"));

    if (fixtures.length === 0) {
        console.error("No fixture files found");
        process.exit(1);
    }

    console.log(`Parsing ${fixtures.length} fixture file(s)…\n`);

    let failures = 0;

    for (const fixture of fixtures) {
        const file_path = path.join(FIXTURES_DIR, fixture);
        try {
            const output = execSync(
                `npx tree-sitter parse "${file_path}" 2>&1`,
                { cwd: GRAMMAR_DIR, encoding: "utf-8" },
            );

            if (output.includes("ERROR") || output.includes("MISSING")) {
                console.log(`  FAIL  ${fixture}`);
                // Print first few error lines.
                const error_lines = output
                    .split("\n")
                    .filter((l) => l.includes("ERROR") || l.includes("MISSING"))
                    .slice(0, 5);
                for (const line of error_lines) {
                    console.log(`        ${line.trim()}`);
                }
                failures++;
            } else {
                console.log(`  PASS  ${fixture}`);
            }
        } catch (err) {
            console.log(`  FAIL  ${fixture} (parse command failed)`);
            if (err.stderr) {
                console.log(`        ${err.stderr.toString().trim()}`);
            }
            failures++;
        }
    }

    console.log(
        `\n${fixtures.length - failures}/${fixtures.length} passed.`,
    );

    if (failures > 0) {
        process.exit(1);
    }
}

main();
