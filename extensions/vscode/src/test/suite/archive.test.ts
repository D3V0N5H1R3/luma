import * as assert from "node:assert";

import { escapePowerShellLiteral } from "../../utils/binary/archive";

suite("escapePowerShellLiteral", () => {
    test("leaves an ordinary path unchanged", () => {
        assert.strictEqual(
            escapePowerShellLiteral("C:\\Users\\dev\\luma.zip"),
            "C:\\Users\\dev\\luma.zip",
        );
    });

    test("doubles single quotes to keep the literal terminated", () => {
        assert.strictEqual(
            escapePowerShellLiteral("C:\\O'Brien\\bin.zip"),
            "C:\\O''Brien\\bin.zip",
        );
        assert.strictEqual(escapePowerShellLiteral("''"), "''''");
    });

    test("does not treat dollar signs or backslashes as special (single-quoted context)", () => {
        assert.strictEqual(escapePowerShellLiteral("C:\\tmp\\$env\\x.zip"), "C:\\tmp\\$env\\x.zip");
    });

    test("rejects a backtick to prevent escape-sequence injection", () => {
        assert.throws(
            () => escapePowerShellLiteral("C:\\tmp\\evil`n.zip"),
            /unsafe characters for PowerShell/,
        );
    });

    test("rejects an embedded null byte that could truncate the command", () => {
        assert.throws(
            () => escapePowerShellLiteral("C:\\tmp\\evil\0.zip"),
            /unsafe characters for PowerShell/,
        );
    });

    test("escapes a quote used to break out into an injected command", () => {
        // A naive interpolation would let this close the quote and run code;
        // doubling the quote keeps it a single literal string.
        const malicious = "x'; Remove-Item C:\\ -Recurse; '";
        const escaped = escapePowerShellLiteral(malicious);
        assert.strictEqual(escaped, "x''; Remove-Item C:\\ -Recurse; ''");
        assert.ok(!/(^|[^'])'([^']|$)/.test(escaped), "no unescaped single quote should remain");
    });
});
