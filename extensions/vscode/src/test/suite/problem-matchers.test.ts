import * as assert from "node:assert";
import * as fs from "node:fs";
import * as path from "node:path";

// Compiled tests run from out/test/suite; the sources sit three levels up.
const EXTENSION_ROOT = path.resolve(__dirname, "..", "..", "..");
const REPO_ROOT = path.resolve(EXTENSION_ROOT, "..", "..");

interface ProblemPattern {
    regexp: string;
    message?: number;
    severity?: number;
    code?: number;
    file?: number;
    line?: number;
    column?: number;
}

interface ProblemMatcher {
    name: string;
    base?: string;
    pattern?: ProblemPattern | ProblemPattern[];
}

function problemMatchers(): ProblemMatcher[] {
    const manifest = JSON.parse(
        fs.readFileSync(path.join(EXTENSION_ROOT, "package.json"), "utf-8"),
    ) as { contributes: { problemMatchers: ProblemMatcher[] } };
    return manifest.contributes.problemMatchers;
}

function lumaPattern(): ProblemPattern {
    const matcher = problemMatchers().find((m) => m.name === "luma");
    assert.ok(matcher?.pattern, "package.json must contribute the $luma problem matcher");
    assert.ok(
        !Array.isArray(matcher.pattern),
        "$luma must be a single-line pattern: the renderer prints a variable number " +
            "of source-gutter lines between the header and the message",
    );
    return matcher.pattern;
}

function field(match: RegExpExecArray, index: number | undefined): string | undefined {
    return index === undefined ? undefined : match[index];
}

suite("problem matchers", () => {
    test("$luma reads location, severity, and code from a diagnostic header", () => {
        const pattern = lumaPattern();
        const match = new RegExp(pattern.regexp).exec("TypeError[E0201] in /work/main.luma:3:5");
        assert.ok(match);
        assert.strictEqual(field(match, pattern.file), "/work/main.luma");
        assert.strictEqual(field(match, pattern.line), "3");
        assert.strictEqual(field(match, pattern.column), "5");
        assert.strictEqual(field(match, pattern.severity), "TypeError");
        assert.strictEqual(field(match, pattern.code), "E0201");
        assert.strictEqual(field(match, pattern.message), "TypeError[E0201]");
    });

    test("$luma accepts Windows paths and headers without a code", () => {
        const pattern = lumaPattern();
        const match = new RegExp(pattern.regexp).exec("Warning in C:\\work\\main.luma:12:1");
        assert.ok(match);
        assert.strictEqual(field(match, pattern.file), "C:\\work\\main.luma");
        assert.strictEqual(field(match, pattern.line), "12");
        assert.strictEqual(field(match, pattern.code), undefined);
    });

    test("$luma ignores the source-gutter lines below a header", () => {
        const regexp = new RegExp(lumaPattern().regexp);
        for (const line of ["     |", "   3 | integer x = y", "     | ^^^^"]) {
            assert.strictEqual(regexp.exec(line), null, `matched gutter line ${line}`);
        }
    });

    test("$luma-test reuses $luma, since FAIL lines carry no location", () => {
        const matcher = problemMatchers().find((m) => m.name === "luma-test");
        assert.ok(matcher, "package.json must keep the $luma-test matcher");
        assert.strictEqual(matcher.base, "$luma");
    });

    test("the header format matches the interpreter's diagnostic renderer", () => {
        // Keeps the matcher in step with core/analysis/diagnostics/renderer.cpp.
        const renderer = path.join(REPO_ROOT, "core", "analysis", "diagnostics", "renderer.cpp");
        const source = fs.readFileSync(renderer, "utf-8");
        assert.ok(source.includes('"{} in {}:{}:{}"'), "renderer header without a code");
        assert.ok(source.includes('"{}[{}] in {}:{}:{}"'), "renderer header with a code");
    });
});
