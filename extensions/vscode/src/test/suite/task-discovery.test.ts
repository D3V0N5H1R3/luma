import * as assert from "node:assert";
import * as fs from "node:fs";
import * as path from "node:path";

import { discoverAnnotations, isLumaTaskCommand } from "../../task-discovery";

suite("discoverAnnotations", () => {
    test("finds @main and @test functions", () => {
        assert.deepStrictEqual(
            discoverAnnotations("@main\nfunction void main() {}\n@test\nfunction void t() {}\n"),
            { runnable: true, has_tests: true },
        );
    });

    test("finds @test functions indented inside a namespace", () => {
        assert.deepStrictEqual(
            discoverAnnotations("namespace Ns {\n    @test\n    function void t() {}\n}\n"),
            { runnable: false, has_tests: true },
        );
    });

    test("finds nothing in a library file", () => {
        assert.deepStrictEqual(discoverAnnotations("function void helper() {}\n"), {
            runnable: false,
            has_tests: false,
        });
    });

    test("gives the same answer when called repeatedly", () => {
        // A shared /g regex would carry lastIndex between calls.
        const text = "@test\nfunction void t() {}\n";
        assert.strictEqual(discoverAnnotations(text).has_tests, true);
        assert.strictEqual(discoverAnnotations(text).has_tests, true);
    });
});

suite("isLumaTaskCommand", () => {
    test("accepts the supported commands", () => {
        for (const command of ["run", "test", "check"]) {
            assert.ok(isLumaTaskCommand(command), command);
        }
    });

    test("rejects anything else instead of crashing the provider", () => {
        for (const command of ["build", "", undefined, 42]) {
            assert.ok(!isLumaTaskCommand(command), String(command));
        }
    });
});

suite("task provider source", () => {
    test("reads files without opening them as documents", () => {
        // openTextDocument sends didOpen to the language server for every
        // workspace file, so discovery must read the bytes instead.
        const source = fs.readFileSync(
            path.resolve(__dirname, "..", "..", "..", "src", "tasks.ts"),
            "utf-8",
        );
        assert.ok(!source.includes("openTextDocument"));
        assert.ok(source.includes("workspace.fs.readFile"));
    });
});
