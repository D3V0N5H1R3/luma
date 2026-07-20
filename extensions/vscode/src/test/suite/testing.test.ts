import * as assert from "node:assert";
import * as fs from "node:fs";
import * as path from "node:path";

import type * as vscode from "vscode";

import { parseFailedTestMessages } from "../../testing/testing";
import { collectItems, extractErrorOutput, groupLeafTestItemsByFile } from "../../testing/utils";

function createLeafTestItem(label: string, file_key: string): vscode.TestItem {
    return {
        label,
        uri: {
            toString: () => file_key,
        },
        children: new Map(),
    } as unknown as vscode.TestItem;
}

function createParentTestItem(...children: vscode.TestItem[]): vscode.TestItem {
    return {
        children: new Map(children.map((child) => [child.label, child])),
    } as unknown as vscode.TestItem;
}

suite("parseFailedTestMessages", () => {
    test("parses real `luma --test` FAIL lines and captures messages", () => {
        const output = [
            "  pass  test_ok",
            "  FAIL  test_alpha: expected true",
            "  FAIL  test_beta: expected false",
            "",
            "3 test(s): 1 passed, 2 failed",
        ].join("\n");

        const failures = parseFailedTestMessages(output);

        assert.strictEqual(failures.size, 2);
        assert.strictEqual(failures.get("test_alpha"), "expected true");
        assert.strictEqual(failures.get("test_beta"), "expected false");
    });

    test("captures messages that themselves contain a colon", () => {
        const output = "  FAIL  test_gamma: assertion failed: 1 != 2";

        const failures = parseFailedTestMessages(output);

        assert.strictEqual(failures.get("test_gamma"), "assertion failed: 1 != 2");
    });

    test("falls back to the full output when a failure line carries no message", () => {
        const output = ["  FAIL  test_alpha:", "stack trace line"].join("\n");

        const failures = parseFailedTestMessages(output);

        assert.strictEqual(failures.get("test_alpha"), output);
    });

    test("keeps the first failure message for duplicate labels", () => {
        const output = [
            "  FAIL  test_alpha: first failure",
            "  FAIL  test_alpha: second failure",
        ].join("\n");

        const failures = parseFailedTestMessages(output);

        assert.strictEqual(failures.get("test_alpha"), "first failure");
    });
});

suite("luma-test problem matcher", () => {
    // `luma --test` prints a failing test as `  FAIL  <name>: <message>` (see
    // the VM test reporter). The `$luma-test` problem matcher used by the
    // "Luma: Run Tests" task must match that exact shape, or the Problems panel
    // is never populated on a test failure.
    function lumaTestPattern(): RegExp {
        const manifest_path = path.resolve(__dirname, "../../../package.json");
        const manifest = JSON.parse(fs.readFileSync(manifest_path, "utf-8"));
        const matcher = manifest.contributes.problemMatchers.find(
            (m: { name: string }) => m.name === "luma-test",
        );
        assert.ok(matcher, "luma-test problem matcher should be contributed");
        return new RegExp(matcher.pattern.regexp);
    }

    test("matches a real FAIL line and captures the name and message", () => {
        const match = lumaTestPattern().exec("  FAIL  test_alpha: expected true");

        assert.ok(match, "regexp should match real interpreter FAIL output");
        assert.strictEqual(match[1], "test_alpha");
        assert.strictEqual(match[2], "expected true");
    });

    test("does not match the interpreter's pass line", () => {
        assert.strictEqual(lumaTestPattern().exec("  pass  test_ok"), null);
    });
});

suite("groupLeafTestItemsByFile", () => {
    test("groups nested leaf items by file without re-filtering", () => {
        const alpha_one = createLeafTestItem("alpha_one", "file:///alpha.luma");
        const alpha_two = createLeafTestItem("alpha_two", "file:///alpha.luma");
        const beta_one = createLeafTestItem("beta_one", "file:///beta.luma");

        const file_map = groupLeafTestItemsByFile([
            createParentTestItem(alpha_one, alpha_two),
            createParentTestItem(beta_one),
        ]);

        assert.deepStrictEqual(file_map.get("file:///alpha.luma"), [alpha_one, alpha_two]);
        assert.deepStrictEqual(file_map.get("file:///beta.luma"), [beta_one]);
    });
});

suite("collectItems", () => {
    function createController(items: vscode.TestItem[]): vscode.TestController {
        return {
            items: {
                forEach: (cb: (item: vscode.TestItem) => void) => items.forEach(cb),
            },
        } as unknown as vscode.TestController;
    }

    test("returns the request's included items when present", () => {
        const included = [
            createLeafTestItem("a", "file:///a.luma"),
            createLeafTestItem("b", "file:///b.luma"),
        ];
        const controller = createController([createLeafTestItem("all", "file:///all.luma")]);
        const request = { include: included } as unknown as vscode.TestRunRequest;

        assert.deepStrictEqual(collectItems(controller, request), included);
    });

    test("falls back to all controller items when include is absent", () => {
        const all = [
            createLeafTestItem("a", "file:///a.luma"),
            createLeafTestItem("b", "file:///b.luma"),
        ];
        const controller = createController(all);
        const request = {} as unknown as vscode.TestRunRequest;

        assert.deepStrictEqual(collectItems(controller, request), all);
    });
});

suite("extractErrorOutput", () => {
    test("concatenates stdout, stderr and message from an exec error", () => {
        const err = { stdout: "out-", stderr: "err-", message: "msg" };
        assert.strictEqual(extractErrorOutput(err), "out-err-msg");
    });

    test("uses only the fields that are strings", () => {
        const err = { stdout: "only-out", stderr: 123, message: undefined };
        assert.strictEqual(extractErrorOutput(err), "only-out");
    });

    test("stringifies a plain string error", () => {
        assert.strictEqual(extractErrorOutput("boom"), "boom");
    });

    test("stringifies a non-object, non-string value", () => {
        assert.strictEqual(extractErrorOutput(42), "42");
    });

    test("stringifies null", () => {
        assert.strictEqual(extractErrorOutput(null), "null");
    });
});
