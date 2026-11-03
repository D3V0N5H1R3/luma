import * as assert from "node:assert";

import { buildExpandArchiveCommand, escapePowerShellLiteral } from "../../utils/binary/archive";

suite("buildExpandArchiveCommand", () => {
    test("passes the archive with -LiteralPath so brackets are not wildcards", () => {
        const command = buildExpandArchiveCommand(
            "C:\\Users\\dev[1]\\bin\\luma_lsp-windows-x86_64.zip",
            "C:\\Users\\dev[1]\\bin",
        );
        assert.strictEqual(
            command,
            "Expand-Archive -LiteralPath 'C:\\Users\\dev[1]\\bin\\luma_lsp-windows-x86_64.zip' " +
                "-DestinationPath 'C:\\Users\\dev[1]\\bin' -Force",
        );
    });

    test("escapes quotes in both paths", () => {
        const command = buildExpandArchiveCommand("C:\\O'Brien\\a.zip", "C:\\O'Brien");
        assert.ok(command.includes("'C:\\O''Brien\\a.zip'"));
        assert.ok(command.includes("'C:\\O''Brien'"));
    });
});

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
