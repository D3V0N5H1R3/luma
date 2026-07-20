import * as assert from "node:assert";

import {
    testFunctionPattern,
    testAnnotationPattern,
    mainAnnotationPattern,
} from "../../generated/test-discovery";

function firstTestName(content: string): string | undefined {
    const match = testFunctionPattern().exec(content);
    return match?.[1];
}

suite("testFunctionPattern", () => {
    test("matches @test with the function on the next line", () => {
        assert.strictEqual(firstTestName("@test\nfunction void test_a() {\n}\n"), "test_a");
    });

    test("matches @test with the function on the same line", () => {
        assert.strictEqual(firstTestName("@test function void test_b() {\n}\n"), "test_b");
    });

    test("matches a generic return type", () => {
        assert.strictEqual(
            firstTestName("@test\nfunction result<string> test_parse() {\n}\n"),
            "test_parse",
        );
    });

    test("matches an optional return type", () => {
        assert.strictEqual(
            firstTestName("@test\nfunction optional<integer> test_find() {\n}\n"),
            "test_find",
        );
    });

    test("matches a tuple return type", () => {
        assert.strictEqual(
            firstTestName("@test\nfunction (integer, string) test_pair() {\n}\n"),
            "test_pair",
        );
    });

    test("matches an array return type", () => {
        assert.strictEqual(
            firstTestName("@test\nfunction array<string> test_list() {\n}\n"),
            "test_list",
        );
    });

    test("matches when other annotations precede @test", () => {
        assert.strictEqual(firstTestName("@slow\n@test\nfunction void test_c() {\n}\n"), "test_c");
    });

    test("discovers every @test function in a file", () => {
        const content = [
            "@test",
            "function void test_first() {}",
            "",
            "@test",
            "function void test_second() {}",
        ].join("\n");

        const regex = testFunctionPattern();
        const names: string[] = [];
        let match: RegExpExecArray | null;
        while ((match = regex.exec(content)) !== null) {
            names.push(match[1]);
        }

        assert.deepStrictEqual(names, ["test_first", "test_second"]);
    });

    test("does not match a function without @test", () => {
        assert.strictEqual(firstTestName("function void helper() {}\n"), undefined);
    });

    test("does not match an @main-annotated function", () => {
        assert.strictEqual(firstTestName("@main\nfunction void main() {}\n"), undefined);
    });
});

suite("testAnnotationPattern", () => {
    test("matches a standalone @test line anywhere in the file", () => {
        assert.ok(testAnnotationPattern().test("code\n@test\nmore code"));
    });

    test("does not match a file without an @test annotation", () => {
        assert.ok(!testAnnotationPattern().test("function void helper() {}\n"));
    });
});

suite("mainAnnotationPattern", () => {
    test("matches a standalone @main line", () => {
        assert.ok(mainAnnotationPattern().test("@main\nfunction void main() {}\n"));
    });

    test("does not match a file without an @main annotation", () => {
        assert.ok(!mainAnnotationPattern().test("@test\nfunction void test_a() {}\n"));
    });
});
