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

    test("TextMate `type` patterns together match the generated built-in type set", () => {
        // Reserved keywords and the contextual (identifier-lexed) names live in
        // separate sub-patterns; together they must cover the whole set.
        const patterns = grammar.repository.type.patterns as { match: string }[];
        const types = [...new Set(patterns.flatMap((p) => extractTypeAlternation(p.match)))].sort();
        assert.deepStrictEqual(
            types,
            expected,
            "luma.tmLanguage.json `type` patterns have drifted from builtin-types.json — " +
                "re-run generate-builtin-types.py and update the grammar in tandem.",
        );
    });

    test("TextMate `builtin_type_name` pattern matches the generated built-in type set", () => {
        const types = extractTypeAlternation(grammar.repository.builtin_type_name.match).sort();
        assert.deepStrictEqual(
            types,
            expected,
            "luma.tmLanguage.json `builtin_type_name` pattern has drifted from builtin-types.json — " +
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
