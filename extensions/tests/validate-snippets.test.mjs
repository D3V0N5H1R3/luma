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
import fs from "node:fs";
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
});
