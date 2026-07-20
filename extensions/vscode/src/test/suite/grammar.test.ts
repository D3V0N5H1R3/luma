import * as assert from "node:assert";
import * as path from "node:path";
import * as fs from "node:fs";

suite("Grammar Structure", () => {
    // Load and parse the grammar JSON
    const grammarPath = path.resolve(__dirname, "../../../syntaxes/luma.tmLanguage.json");
    const grammar = JSON.parse(fs.readFileSync(grammarPath, "utf-8"));

    test("Grammar file should be valid JSON with required fields", () => {
        assert.strictEqual(grammar.scopeName, "source.luma");
        assert.ok(grammar.name, "Grammar should have a name");
        assert.ok(Array.isArray(grammar.patterns), "Grammar should have patterns array");
        assert.ok(grammar.repository, "Grammar should have a repository");
    });

    test("Grammar should include all expected top-level pattern references", () => {
        const includes = grammar.patterns
            .filter((p: { include?: string }) => p.include)
            .map((p: { include: string }) => p.include);

        const required = [
            "#comment",
            "#annotation",
            "#number",
            "#constant",
            "#keyword_control",
            "#function_declaration",
            "#type_declaration",
            "#keyword_declaration",
            "#storage_modifier",
            "#operator",
            "#string",
        ];

        for (const ref of required) {
            assert.ok(includes.includes(ref), `Missing top-level pattern: ${ref}`);
        }
    });

    test("Comment pattern should match hash-style comments", () => {
        const comment = grammar.repository.comment;
        assert.ok(comment, "Comment rule should exist");
        assert.ok(comment.match || comment.begin, "Comment should have match or begin pattern");
        assert.ok(
            comment.name?.includes("comment"),
            `Comment scope should contain 'comment', got: ${comment.name}`,
        );
    });

    test("String pattern should handle double-quoted strings", () => {
        const str = grammar.repository.string;
        assert.ok(str, "String rule should exist");
        const hasBeginEnd = str.begin && str.end;
        const hasMatch = str.match;
        assert.ok(hasBeginEnd || hasMatch, "String should have begin/end or match pattern");
    });

    test("Annotation pattern should match @-prefixed tokens", () => {
        const annotation = grammar.repository.annotation;
        assert.ok(annotation, "Annotation rule should exist");
        assert.ok(annotation.match?.includes("@"), "Annotation pattern should match @ prefix");
        assert.ok(
            annotation.name?.includes("annotation") || annotation.name?.includes("tag"),
            `Annotation scope should contain 'annotation' or 'tag', got: ${annotation.name}`,
        );
    });

    test("Number pattern should exist", () => {
        const num = grammar.repository.number;
        assert.ok(num, "Number rule should exist");
        assert.ok(
            num.name?.includes("numeric") ||
                num.name?.includes("number") ||
                num.patterns?.length > 0,
            "Number should have a numeric scope or sub-patterns",
        );
    });

    test("Number patterns reject digit separators and trailing-dot floats", () => {
        const num = grammar.repository.number;
        assert.ok(Array.isArray(num.patterns), "number rule should have sub-patterns");

        // Anchor each numeric sub-pattern so we test a *whole-token* match:
        // TextMate scopes the span it matches, so a partial match on "1_000"
        // or "1." would still colour the invalid literal as numeric.
        const matchers = (num.patterns as { name: string; match: string }[]).map((p) => ({
            name: p.name,
            re: new RegExp(`^(?:${p.match})$`),
        }));
        const tokenisesAsNumber = (text: string) => matchers.some((m) => m.re.test(text));

        // Valid Luma numerics still tokenise (canonical grammar: no separators,
        // digits required on both sides of the decimal point).
        for (const valid of ["1000", "1.0", "3.14e10", "0xFF", "0b1010"]) {
            assert.ok(
                tokenisesAsNumber(valid),
                `expected "${valid}" to tokenise as a numeric literal`,
            );
        }

        // Invalid forms must not tokenise as a whole numeric literal.
        for (const invalid of ["1_000", "0xFF_FF", "0b10_10", "1."]) {
            assert.ok(
                !tokenisesAsNumber(invalid),
                `"${invalid}" must not tokenise as a numeric literal`,
            );
        }
    });

    test("Keyword control patterns should exist", () => {
        const kw = grammar.repository.keyword_control;
        assert.ok(kw, "keyword_control rule should exist");
        const hasMatch = kw.match || kw.patterns;
        assert.ok(hasMatch, "keyword_control should have match or patterns");
    });

    test("Function declaration pattern should exist", () => {
        const fn = grammar.repository.function_declaration;
        assert.ok(fn, "function_declaration rule should exist");
    });

    test("Type declaration pattern should exist", () => {
        const td = grammar.repository.type_declaration;
        assert.ok(td, "type_declaration rule should exist");
    });

    test("Operator pattern should exist", () => {
        const op = grammar.repository.operator;
        assert.ok(op, "operator rule should exist");
    });

    test("String interpolation should be defined", () => {
        const interp = grammar.repository.interpolation;
        assert.ok(interp, "interpolation rule should exist for ${} syntax");
    });

    test("Triple-quoted string pattern should exist", () => {
        const triple = grammar.repository.string_triple;
        assert.ok(triple, "string_triple rule should exist");
        assert.ok(triple.begin?.includes('"""'), "Triple string should begin with triple quotes");
        assert.ok(triple.end?.includes('"""'), "Triple string should end with triple quotes");
    });

    test("All pattern references should resolve to existing repository entries", () => {
        const allRefs: string[] = [];

        function collectRefs(obj: unknown): void {
            if (typeof obj !== "object" || obj === null) return;
            if (Array.isArray(obj)) {
                for (const item of obj) collectRefs(item);
                return;
            }
            const record = obj as Record<string, unknown>;
            if (typeof record.include === "string" && record.include.startsWith("#")) {
                allRefs.push(record.include.slice(1));
            }
            for (const value of Object.values(record)) {
                collectRefs(value);
            }
        }

        collectRefs(grammar.patterns);
        collectRefs(grammar.repository);

        const repoKeys = new Set(Object.keys(grammar.repository));
        const missing = allRefs.filter((ref) => !repoKeys.has(ref));
        assert.deepStrictEqual(missing, [], `Unresolved pattern references: ${missing.join(", ")}`);
    });
});

suite("Markdown Injection Grammar", () => {
    const injectionPath = path.resolve(__dirname, "../../../syntaxes/luma.markdown-injection.json");

    test("Injection grammar should exist and have correct scopeName", () => {
        assert.ok(fs.existsSync(injectionPath), "Markdown injection grammar file should exist");
        const injection = JSON.parse(fs.readFileSync(injectionPath, "utf-8"));
        assert.strictEqual(injection.scopeName, "markdown.luma.codeblock");
    });

    test("Injection grammar should inject into markdown", () => {
        const injection = JSON.parse(fs.readFileSync(injectionPath, "utf-8"));
        assert.ok(
            injection.injectionSelector || injection.injectTo,
            "Should have injection selector or injectTo",
        );
    });
});
