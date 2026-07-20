import * as assert from "node:assert";
import * as path from "node:path";
import * as fs from "node:fs";

import { LUMA_BUILTIN_TYPE_SET } from "../../generated/builtin-types";

/**
 * Extracts the first lowercase `(a|b|c)` alternation group from a TextMate
 * `match` pattern — i.e. the built-in type list embedded in the grammar rule.
 */
function extractTypeAlternation(pattern: string): string[] {
    const match = /\(([a-z_]+(?:\|[a-z_]+)+)\)/.exec(pattern);
    assert.ok(match, `Expected a type alternation group in pattern: ${pattern}`);
    return match[1].split("|");
}

suite("Built-in type list", () => {
    const grammarPath = path.resolve(__dirname, "../../../syntaxes/luma.tmLanguage.json");
    const grammar = JSON.parse(fs.readFileSync(grammarPath, "utf-8"));

    const expected = [...LUMA_BUILTIN_TYPE_SET].sort();

    test("generated set is non-empty", () => {
        assert.ok(LUMA_BUILTIN_TYPE_SET.size > 0, "LUMA_BUILTIN_TYPE_SET should not be empty");
    });

    test("TextMate `type` pattern matches the generated built-in type set", () => {
        const types = extractTypeAlternation(grammar.repository.type.match).sort();
        assert.deepStrictEqual(
            types,
            expected,
            "luma.tmLanguage.json `type` pattern has drifted from builtin-types.json — " +
                "re-run generate-builtin-types.py and update the grammar in tandem.",
        );
    });

    test("TextMate `typed_binding` pattern matches the generated built-in type set", () => {
        const types = extractTypeAlternation(grammar.repository.typed_binding.match).sort();
        assert.deepStrictEqual(
            types,
            expected,
            "luma.tmLanguage.json `typed_binding` pattern has drifted from builtin-types.json — " +
                "re-run generate-builtin-types.py and update the grammar in tandem.",
        );
    });
});
