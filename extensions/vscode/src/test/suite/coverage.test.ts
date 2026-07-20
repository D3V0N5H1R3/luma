import * as assert from "node:assert";
import * as fs from "node:fs";
import * as path from "node:path";

/**
 * Regression guard for the removed "Run with Coverage" test profile.
 *
 * The interpreter has no `--coverage` flag and emits no coverage data, so the
 * VS Code Coverage profile only ever ran `luma --test --coverage <file>`, which
 * the interpreter rejects as an unknown option — turning every coverage run into
 * a wall of "unknown option '--coverage'" failures and producing no coverage.
 * The feature was removed; these tests ensure the extension never reintroduces
 * the unsupported flag or a coverage run profile while core lacks support.
 */
suite("coverage feature removed", () => {
    const testing_dir = path.resolve(__dirname, "../../../src/testing");

    function readTestingSources(): string {
        return fs
            .readdirSync(testing_dir)
            .filter((file) => file.endsWith(".ts"))
            .map((file) => fs.readFileSync(path.join(testing_dir, file), "utf-8"))
            .join("\n");
    }

    test("the testing module never passes --coverage to the interpreter", () => {
        assert.ok(
            !readTestingSources().includes("--coverage"),
            "extension must not invoke the unsupported --coverage interpreter flag",
        );
    });

    test("no Coverage run profile is registered", () => {
        const testing_src = fs.readFileSync(path.join(testing_dir, "testing.ts"), "utf-8");

        assert.ok(
            !testing_src.includes("TestRunProfileKind.Coverage"),
            "the Coverage run profile must not be registered until core supports coverage",
        );
    });

    test("the coverage module was removed", () => {
        assert.ok(
            !fs.existsSync(path.join(testing_dir, "coverage.ts")),
            "src/testing/coverage.ts should not exist",
        );
    });
});
