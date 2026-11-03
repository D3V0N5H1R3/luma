#!/usr/bin/env node
/**
 * Shared Snippet Validation
 *
 * Validates the canonical snippet set in extensions/shared/snippets/luma.json,
 * which both the VS Code and Zed extensions consume verbatim.
 *
 * Regression guard for the "Generic Type Alias" (`typeg`) snippet, which
 * previously emitted the invalid `type<T> Name = …` (generic parameters
 * before the alias name). In Luma, generic parameters follow the declared
 * name on type/record/choice/interface declarations — only `function`
 * declarations place them after the keyword — so the canonical form is
 * `type Name<T> = …`.
 *
 * Usage:
 *   node extensions/tests/validate-snippets.test.mjs
 */

import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const sharedDir = path.join(__dirname, "..", "shared");

const snippets = JSON.parse(
    fs.readFileSync(path.join(sharedDir, "snippets", "luma.json"), "utf-8"),
);

/**
 * Joins a snippet body (an array of lines, or a single string) into one
 * string, matching how editors assemble the inserted text.
 *
 * @param {string | string[]} body  The snippet `body` field.
 * @returns {string} The body as a single newline-joined string.
 */
function bodyText(body) {
    return Array.isArray(body) ? body.join("\n") : String(body);
}

// Declaration keywords that require the generic parameter list to follow the
// declared *name* (e.g. `record Name<T>`), never the keyword. `function` is
// intentionally excluded: `function<T> RetType name(…)` is the correct form.
const NAME_FIRST_KEYWORDS = ["type", "record", "choice", "interface"];
const paramsBeforeName = new RegExp(
    `\\b(${NAME_FIRST_KEYWORDS.join("|")})\\s*<`,
);

// Keywords that look like natural variable names but cannot be used as one.
const RESERVED_VARIABLE_NAMES = new Set([
    "result",
    "optional",
    "unique",
    "borrow",
    "type",
    "use",
    "with",
    "internal",
]);

// Declarations that may start a file; anything else is a statement and is
// checked inside a function body (`internal` ones inside a namespace).
const TOP_LEVEL_START = /^(@|function\b|record\b|choice\b|interface\b|namespace\b|type\b|include\b|use\b)/;

/**
 * Expands a snippet body the way an editor does when every placeholder is
 * accepted as-is: `${1:default}` becomes `default`, `${1}` / `$1` / `$0`
 * become empty, and an escaped `\$` becomes `$`.
 *
 * @param {string | string[]} body  The snippet `body` field.
 * @returns {string} The inserted text.
 */
function expandSnippet(body) {
    let text = bodyText(body);
    let previous;
    do {
        previous = text;
        text = text.replace(/(?<!\\)\$\{\d+:([^{}]*)\}/g, "$1");
    } while (text !== previous);
    return text
        .replace(/(?<!\\)\$\{\d+\}/g, "")
        .replace(/(?<!\\)\$\d+/g, "")
        .replace(/\\\$/g, "$");
}

/**
 * Wraps expanded snippet text so it forms a complete file for `luma --check`.
 *
 * @param {string} text  The expanded snippet.
 * @returns {string} A checkable Luma source file.
 */
function wrapForCheck(text) {
    const trimmed = text.trimStart();
    const indented = text
        .split("\n")
        .map((line) => `    ${line}`)
        .join("\n");
    if (trimmed.startsWith("internal")) {
        return `namespace SnippetCheck {\n${indented}\n}\n`;
    }
    if (TOP_LEVEL_START.test(trimmed)) {
        return `${text}\n`;
    }
    return `function void snippet_check() {\n${indented}\n}\n`;
}

/**
 * Locates a built `luma` interpreter: $LUMA_BIN, else the CMake build tree.
 *
 * @returns {string | undefined} The interpreter path, if one exists.
 */
function findInterpreter() {
    const exe = process.platform === "win32" ? "luma.exe" : "luma";
    const root = path.join(__dirname, "..", "..");
    const candidates = [
        process.env.LUMA_BIN,
        path.join(root, "build", "Release", exe),
        path.join(root, "build", exe),
    ];
    return candidates.find((candidate) => candidate && fs.existsSync(candidate));
}

// ─── Tests ────────────────────────────────────────────────────────

describe("shared snippets (luma.json)", () => {
    it("should be a non-empty object of well-formed snippets", () => {
        const entries = Object.entries(snippets);
        assert.ok(entries.length > 0, "snippet set should not be empty");

        for (const [name, snippet] of entries) {
            assert.ok(
                typeof snippet.prefix === "string" && snippet.prefix.length > 0,
                `snippet "${name}" must have a non-empty prefix`,
            );
            assert.ok(
                snippet.body !== undefined,
                `snippet "${name}" must have a body`,
            );
        }
    });

    it("never places generic parameters before the name on name-first declarations", () => {
        for (const [name, snippet] of Object.entries(snippets)) {
            const text = bodyText(snippet.body);
            assert.ok(
                !paramsBeforeName.test(text),
                `snippet "${name}" places generic parameters before the name ` +
                    `(matched ${paramsBeforeName}); type/record/choice/interface ` +
                    `declarations must be written "Keyword Name<T>", e.g. "type Name<T> = …"`,
            );
        }
    });

    it("the Generic Type Alias (typeg) snippet emits `type Name<T> = …`", () => {
        const alias = Object.values(snippets).find((s) => s.prefix === "typeg");
        assert.ok(alias, "expected a snippet with prefix 'typeg'");

        const text = bodyText(alias.body);
        assert.match(
            text,
            /^type\s+\$\{\d+:[^}]+\}<\$\{\d+:[^}]+\}>\s*=/,
            `typeg body should be "type <name-stop><<param-stop>> = …", got: ${text}`,
        );
    });

    it("never defaults a placeholder to a reserved keyword", () => {
        for (const [name, snippet] of Object.entries(snippets)) {
            for (const [, value] of bodyText(snippet.body).matchAll(/\$\{\d+:([^}]*)\}/g)) {
                // A placeholder that names a variable must not be a keyword:
                // `integer result = …` is a SyntaxError (result spells result<T>).
                if (!RESERVED_VARIABLE_NAMES.has(value)) {
                    continue;
                }
                const text = expandSnippet(snippet.body);
                assert.ok(
                    !new RegExp(`\\b[\\w<>]+\\s+${value}\\s*=`).test(text),
                    `snippet "${name}" names a variable "${value}", a reserved keyword`,
                );
            }
        }
    });

    it("writes dictionary literals with `\"key\": value`", () => {
        const dict = Object.values(snippets).find((s) => s.prefix === "dict");
        assert.ok(dict, "expected a snippet with prefix 'dict'");
        assert.match(expandSnippet(dict.body), /"key":\s*value/);
    });

    it("never prefixes a result arm with `case`", () => {
        for (const [name, snippet] of Object.entries(snippets)) {
            assert.doesNotMatch(
                bodyText(snippet.body),
                /\bcase\s+(success|failure)\s*\(/,
                `snippet "${name}": result arms are written "success(v) { … }", without "case"`,
            );
        }
    });

    it("declares interfaces as field lists", () => {
        for (const [name, snippet] of Object.entries(snippets)) {
            const text = expandSnippet(snippet.body);
            const body = /^interface\s[^{]*\{([\s\S]*)\}\s*$/.exec(text);
            if (body) {
                assert.ok(
                    !body[1].includes("("),
                    `snippet "${name}": an interface holds "type field" entries, not methods`,
                );
            }
        }
    });

    it("only spawns inside a task_scope", () => {
        for (const [name, snippet] of Object.entries(snippets)) {
            const text = bodyText(snippet.body);
            if (/\bspawn\b/.test(text)) {
                assert.match(
                    text,
                    /^task_scope\s*\{/,
                    `snippet "${name}": a bare spawn outside task_scope is a compile error`,
                );
            }
        }
    });

    // Runs only where an interpreter is available (local builds; set LUMA_BIN
    // to point at one). Placeholder names are undefined, so type errors
    // (exit 2) are expected — only a syntax error (exit 3) is a defect.
    const luma = findInterpreter();
    it("expands every snippet to syntactically valid Luma", { skip: !luma && "no luma binary" }, () => {
        const dir = fs.mkdtempSync(path.join(os.tmpdir(), "luma-snippets-"));
        // The Include snippet names "file.luma"; provide it so only syntax is judged.
        fs.writeFileSync(path.join(dir, "file.luma"), "");
        try {
            for (const [name, snippet] of Object.entries(snippets)) {
                const file = path.join(dir, "snippet.luma");
                fs.writeFileSync(file, wrapForCheck(expandSnippet(snippet.body)));
                const run = spawnSync(luma, ["--check", file], { encoding: "utf-8" });
                assert.notEqual(
                    run.status,
                    3,
                    `snippet "${name}" expands to a syntax error:\n${run.stderr}`,
                );
            }
        } finally {
            fs.rmSync(dir, { recursive: true, force: true });
        }
    });
});

describe("editor snippet copies stay in sync with the canonical shared set", () => {
    // Each editor bundles its own copy inside its extension folder and references
    // "./snippets/luma.json" — never "../shared/…", which resolves outside the
    // packaged extension and makes it non-portable. The copies must stay identical
    // to the canonical shared set consumed above.
    for (const editor of ["vscode", "zed"]) {
        it(`${editor}/snippets/luma.json matches shared/snippets/luma.json`, () => {
            const copyPath = path.join(__dirname, "..", editor, "snippets", "luma.json");
            assert.ok(
                fs.existsSync(copyPath),
                `${editor} must bundle its own copy at ${editor}/snippets/luma.json ` +
                    `(a "../shared/…" reference resolves outside the packaged extension)`,
            );

            const copy = JSON.parse(fs.readFileSync(copyPath, "utf-8"));
            assert.deepEqual(
                copy,
                snippets,
                `${editor}/snippets/luma.json has drifted from the canonical ` +
                    `shared/snippets/luma.json — re-copy the shared file into ${editor}/snippets/`,
            );
        });
    }
});
