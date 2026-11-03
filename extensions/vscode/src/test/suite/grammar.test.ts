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
        for (const valid of ["1000", "1.0", "3.14e10", "0xFF", "0o755", "0O17", "0b1010"]) {
            assert.ok(
                tokenisesAsNumber(valid),
                `expected "${valid}" to tokenise as a numeric literal`,
            );
        }

        // Invalid forms must not tokenise as a whole numeric literal.
        for (const invalid of ["1_000", "0xFF_FF", "0o7_55", "0o8", "0b10_10", "1."]) {
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

    test("Operator sub-patterns tokenise the error-propagation `?` distinctly from `??`/`?.`/`?[`", () => {
        const op = grammar.repository.operator;
        assert.ok(Array.isArray(op.patterns), "operator rule should have sub-patterns");

        // Simulate the oniguruma engine scanning left-to-right, taking the
        // first sub-pattern (in declared order) that matches at the cursor —
        // this mirrors how TextMate grammars actually tokenise, so pattern
        // ordering (longer alternatives before the standalone `?`) is exercised.
        const matchers = (op.patterns as { name: string; match: string }[]).map((p) => ({
            name: p.name,
            re: new RegExp(p.match, "y"),
        }));
        const tokenAt = (text: string, index: number): string | undefined => {
            for (const m of matchers) {
                m.re.lastIndex = index;
                if (m.re.test(text)) {
                    return m.name;
                }
            }
            return undefined;
        };

        const cases: Array<[string, number, string]> = [
            ["result?", 6, "keyword.operator.propagation.luma"],
            ["a ?? b", 2, "keyword.operator.coalescing.luma"],
            ["a?.b", 1, "keyword.operator.optional-chain.luma"],
            ["a?[0]", 1, "keyword.operator.optional-chain.luma"],
        ];

        for (const [text, index, expected] of cases) {
            assert.strictEqual(
                tokenAt(text, index),
                expected,
                `expected "${text}" at index ${index} to tokenise as ${expected}`,
            );
        }
    });

    test("String interpolation should be defined", () => {
        const interp = grammar.repository.interpolation;
        assert.ok(interp, "interpolation rule should exist for ${} syntax");
    });

    test("Turbofish `::<Type>` should be scoped as generic-argument punctuation, not comparisons", () => {
        const turbofish = grammar.repository.turbofish;
        assert.ok(turbofish, "turbofish rule should exist");

        const re = new RegExp(turbofish.match);
        const single = re.exec("::<String>");
        assert.ok(single, "turbofish pattern should match ::<String>");
        assert.strictEqual(single[2], "<", "group 2 should capture the opening angle bracket");
        assert.strictEqual(single[3], "String", "group 3 should capture the type argument");
        assert.strictEqual(single[4], ">", "group 4 should capture the closing angle bracket");

        assert.strictEqual(
            turbofish.captures["2"].name,
            "punctuation.definition.typeparameters.begin.luma",
            "opening angle bracket must not be scoped as a comparison operator",
        );
        assert.strictEqual(
            turbofish.captures["4"].name,
            "punctuation.definition.typeparameters.end.luma",
            "closing angle bracket must not be scoped as a comparison operator",
        );

        // #turbofish must precede #operator in the top-level pattern list: both
        // match at the same starting position for "::<...>", and TextMate
        // resolves such ties by declaration order.
        const includes = grammar.patterns
            .filter((p: { include?: string }) => p.include)
            .map((p: { include: string }) => p.include);
        assert.ok(
            includes.indexOf("#turbofish") < includes.indexOf("#operator"),
            "#turbofish must be listed before #operator to win the tie on '::'",
        );
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

    test("`when` match guards are scoped as control keywords", () => {
        const re = new RegExp(grammar.repository.keyword_control.match);
        assert.ok(re.test("case >= 0 when n == 0 {"));
    });

    test("Typed bindings accept one level of nested generics", () => {
        const re = new RegExp(grammar.repository.typed_binding.match);
        const match = re.exec("array<array<integer>> grid = []");
        assert.ok(match, "array<array<integer>> grid should be a typed binding");
        assert.strictEqual(
            match[2],
            "<array<integer>>",
            "group 2 should capture the generic arguments",
        );
        assert.strictEqual(match[3], "grid");
        assert.strictEqual(
            grammar.repository.typed_binding.captures["2"].patterns[0].include,
            "#type_arguments",
            "generic arguments are a type position",
        );
    });

    test("Spans known to hold only types scope every built-in type name", () => {
        const builtin = new RegExp(grammar.repository.builtin_type_name.match);
        const typeArguments = grammar.repository.type_arguments.patterns as { include?: string }[];
        assert.ok(typeArguments.some((p) => p.include === "#builtin_type_name"));
        assert.ok(builtin.test("(set, set)"), "a bare container name inside a type span is a type");

        const tupleBegin = new RegExp(grammar.repository.tuple_type_binding.begin);
        assert.ok(tupleBegin.test("(set, set) pair = Set.partition(s, f)"));
        assert.ok(!tupleBegin.test("(a, b) == pair"), "a comparison is not a tuple-typed binding");
        assert.ok(!tupleBegin.test("(number a) -> a"), "a lambda is not a tuple-typed binding");

        const functionBegin = new RegExp(grammar.repository.function_type.begin);
        assert.ok(functionBegin.test("function(socket) -> void"));
        assert.ok(
            !functionBegin.test("function void main() {"),
            "a declaration is not a function type",
        );

        // `function (T, U) name(` declares a function with a tuple return type;
        // #function_declaration must win that tie, and #function_type must still
        // claim `function(` before the bare keyword rule does.
        const includes = grammar.patterns.map((p: { include?: string }) => p.include);
        assert.ok(includes.indexOf("#function_declaration") < includes.indexOf("#function_type"));
        assert.ok(includes.indexOf("#function_type") < includes.indexOf("#keyword_declaration"));
    });

    test("Function declarations accept a namespace-qualified return type", () => {
        const re = new RegExp(grammar.repository.function_declaration.patterns[0].match);
        const match = re.exec("function DateTime.TimeParts parse(string text) {");
        assert.ok(match, "a qualified return type should still name the function");
        assert.strictEqual(match[2], "parse");
    });

    test("Lambda parameter lists match typed parameters (Luma lambdas are always typed)", () => {
        const lambda = grammar.repository.lambda;
        const begin = new RegExp(lambda.begin);
        assert.ok(begin.test("(number a, number b) -> a + b"));
        assert.ok(begin.test("() -> 42"));
        assert.ok(!begin.test("(number, number) pair = (1, 2)"), "a tuple type is not a lambda");

        const parameter = new RegExp(lambda.patterns[0].match, "g");
        const names = [..."mutable array<integer> values, number b".matchAll(parameter)].map(
            (m) => m[3],
        );
        assert.deepStrictEqual(names, ["values", "b"]);
        assert.strictEqual(
            lambda.patterns[0].captures["2"].patterns[0].include,
            "#builtin_type_name",
            "a lambda parameter's type capture is a known type position",
        );
    });

    test("Container/handle type names are types only in type position (they lex as identifiers)", () => {
        const patterns = grammar.repository.type.patterns as { match: string }[];
        const isType = (text: string, word: string): boolean => {
            const index = text.indexOf(word);
            return patterns.some((p) => {
                const re = new RegExp(p.match, "g");
                return [...text.matchAll(re)].some((m) => m.index === index && m[0] === word);
            });
        };

        const typePositions: Array<[string, string]> = [
            ["set<integer> s = Set.new()", "set"],
            ["socket s = conn", "socket"],
            ["function(integer) -> void", "void"],
            ["array<task<integer>> ts = []", "task"],
            ["x = integer", "integer"],
            ["decimal d = 1.5", "decimal"],
        ];
        for (const [text, word] of typePositions) {
            assert.ok(isType(text, word), `"${word}" should be a type in: ${text}`);
        }

        const variablePositions: Array<[string, string]> = [
            ["print(task)", "task"],
            ["for x in set {", "set"],
            ["job.task = 1", "task"],
            ["queue with { size = 1 }", "queue"],
            ["return stack", "stack"],
        ];
        for (const [text, word] of variablePositions) {
            assert.ok(!isType(text, word), `"${word}" should not be a type in: ${text}`);
        }
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
